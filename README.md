# 351-redirect
empty for project

Create a server that listens on a port (default: 80, changeable with a "-p" option).

Accept HTTP/1.1. One message at a time, no pipelining. 

Implement "/static" which allows the requester to specify the name of a file in a "/static" directory. Ex: "/static/images/rex.png", returns the binary file that is there accessible to your server.

Implement "/stats" which returns a properly formatted HTML doc that lists the number of requests received so far, and the total of received bytes and sent bytes.

Implement "/calc" which returns text or HTML, summing the value of two query params in the request named "a" and "b" (both numeric).

Make it multithreaded!

