#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>


#define BUFFER_SIZE 1024
#define PATH_SIZE 256

void error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

int request_count = 0;
int total_bytes_received = 0;
int total_bytes_sent = 0;
pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

void handle_static(int client_socket, const char *filepath) {
    char full_path[512] = "./static";
    strcat(full_path, filepath);
    FILE *file = fopen(full_path, "rb");
    char buffer[BUFFER_SIZE];
    char response[128];
    
    if (!file) {
        sprintf(response, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
        send(client_socket, response, strlen(response), 0);
        return;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    sprintf(response, "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\n\r\n", file_size);
    send(client_socket, response, strlen(response), 0);
    pthread_mutex_lock(&stats_mutex);
    total_bytes_sent += strlen(response);
    pthread_mutex_unlock(&stats_mutex);

    while (!feof(file)) {
        int bytes_read = fread(buffer, 1, BUFFER_SIZE, file);
        send(client_socket, buffer, bytes_read, 0);

        pthread_mutex_lock(&stats_mutex);
        total_bytes_sent += bytes_read;
        pthread_mutex_unlock(&stats_mutex);
    }
    fclose(file);
}

void handle_stats(int client_socket) {
    char response[BUFFER_SIZE];

    pthread_mutex_lock(&stats_mutex);
    sprintf(response,
            "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
            "<html><body><h1>Server Statistics</h1>"
            "<p>Requests received: %d</p>"
            "<p>Total bytes received: %d</p>"
            "<p>Total bytes sent: %d</p>"
            "</body></html>",
            request_count, total_bytes_received, total_bytes_sent);
    pthread_mutex_unlock(&stats_mutex);

    send(client_socket, response, strlen(response), 0);

    pthread_mutex_lock(&stats_mutex);
    total_bytes_sent += strlen(response);
    pthread_mutex_unlock(&stats_mutex);
}

void handle_calc(int client_socket, const char *path) {
    char response[BUFFER_SIZE];
    int a = 0, b = 0;

    int parsed = sscanf(path, "/calc?a=%d&b=%d", &a, &b);
    if (parsed != 2) {
        sprintf(response, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
        send(client_socket, response, strlen(response), 0);
        return;
    }

    int sum = a + b;
    sprintf(response,
            "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n"
            "The sum of %d and %d is %d", a, b, sum);

    send(client_socket, response, strlen(response), 0);

    pthread_mutex_lock(&stats_mutex);
    total_bytes_sent += strlen(response);
    pthread_mutex_unlock(&stats_mutex);
}

void *handle_client(void *arg) {
    int client_socket = *((int *)arg);
    free(arg);
    char buffer[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    char path[PATH_SIZE];
    int bytes_received;

    printf("Connected to client on thread %lu\n", (unsigned long)pthread_self());

    bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
    total_bytes_received += bytes_received;

    if (bytes_received <= 0) {
        close(client_socket);
        return NULL;    
    }

    pthread_mutex_lock(&stats_mutex);
    request_count++;
    total_bytes_received += bytes_received;
    pthread_mutex_unlock(&stats_mutex);

    buffer[bytes_received] = '\0';
    printf("Request received:\n%s\n", buffer);

    sscanf(buffer, "GET %255s HTTP/1.1", path);

    if (strncmp(path, "/static", 7) == 0) {
        handle_static(client_socket, path + 7);
    }
    else if (strcmp(path, "/stats") == 0) {
        handle_stats(client_socket);
    }
    else if(strncmp(path, "/calc", 5) == 0) {
        handle_calc(client_socket, path + 5);
    }
    else {
        sprintf(response, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
        send(client_socket, response, strlen(response), 0);
    }

   printf("Client on thread %ld disconnected\n", (unsigned long)pthread_self());
   close(client_socket);
   return NULL;


}

int main(int argc, char *argv[]) {
    int opt;
    int port = 0;

    while ((opt = getopt(argc, argv, "p:")) != -1) {
        switch (opt) {
            case 'p':
                port = atoi(optarg);
                break;
            default:
                fprintf(stderr, "Usage: %s -p [port]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    /*if (port == 0) {
        fprintf(stderr, "Port number is required. \n Usage: %s -p [port]\n", argv[0]);
        exit(EXIT_FAILURE);
    }*/

    int server_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        error("Error opening socket");
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(server_socket);
        error("Error binding socket");
    }

    if (listen(server_socket, 5) < 0) {
        close(server_socket);
        error("Error listening on socket");
    }

    printf("Server listening on port %d\n", port);
    
    while(1) {
        int *client_socket = malloc(sizeof(int));
        if (!client_socket) {
            error("Error allocating memory for client socket");
            continue;
        }

        *client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (*client_socket < 0) {
            perror("Error accepting client connection");
            free(client_socket);
            continue;
        }

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, (void *)client_socket) != 0) {
            perror("Error creating thread");
            free(client_socket);
            continue;
        }
        else {
            pthread_detach(thread_id);
        }
    }

    close(server_socket);
    return 0;
}