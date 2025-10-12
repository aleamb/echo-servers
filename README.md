# Implementations of an echo server

This repo contains multiple implementations of an echo-servers using differents languages/frameworks.

## Implementations

| Dir                       | Contents                                                                                   | Language | Platform | Technologies       |
|---------------------------|--------------------------------------------------------------------------------------------|----------|----------|--------------------|
| c\_winsock\_iocp\_thread  | Winsock 2 implementation using I/O Completion Ports, Overlapped sockets and worker threads | C        | Windows  | Winsock 2, IOCP    |
| c\_winsock\_wsapoll       | Winsock 2 implementation using WSAPoll and single thread                                   | C        | Windows  | Winsock 2, WSAPoll |

## WIP:

| Dir                      | Contents                                                                                   | Language | Platform | Technologies       |
|--------------------------|--------------------------------------------------------------------------------------------|----------|----------|--------------------|
| c\_linux\_epoll          | Linux epoll implementation                                                                 | C        | Linux    | epoll              |
| c\_bsd\_kqueue           | BSD kqueue implementation                                                                  | C        | BSD      | kqueue             |
| c\_libuv                 | Libuv implementation                                                                       | C        | Multi    | libuv              |
| Java NIO                 | Java 21 using NIO single threaded                                                          | Java     | Multi    | Java NIO           |
| Java Netty               | Java 21 using Netty                                                                        | Java     | Multi    | Java, Netty        |
| Java NIO MT              | Java 21 using NIO and virtual threads                                                      | Java     | Multi    | Java NIO, VT       |
| Python                   |                                                                                            | Python   | Multi    | Python             |
| C++ Boost.Asio           | C++ using Boost.Asio                                                                       | C++      | Multi    | C++, Boost.Asio    |


## Testing

test\_echo\_server.py is a simple utility to test echo servers. Run with Python >= 3.8.

> [!NOTE]  
> Script will attempt to use poll() mechanism to manage client connections. If not poll() available (i.e Windows) it will to use select() method. 

Description of utility:

```
usage: test_echo_server [-h] [-l LENGTH] [-i INTERVAL_RANGE] [-n NUM] [-p THREADS] [-c CONNECTIONS] host port

Tests echo servers sending generated variable data.

positional arguments:
  host                  host or ip of the  server to test
  port                  port

options:
  -h, --help            show this help message and exit
  -l LENGTH, --length LENGTH
                        length of string to send.
  -i INTERVAL_RANGE, --interval_range INTERVAL_RANGE
                        interval range (format: [min-]<max>), in miliseconds, between each send of a string. This includes connections too. Script selects a random number between min and max
  -n NUM, --num NUM     Num messages to send
  -p THREADS, --threads THREADS
                        Num. of threads
  -c CONNECTIONS, --connections CONNECTIONS
                        Connections by thread.

Table format is:

Send timestamp,finish send timestamp,timestamp of receiving response data,response time,length of data sent,length of data received,thread id,client_id,error flag (0 if no error)

The error is marked when sent data and received data are not equals. Last column set to 1 if error occurs.

Timestamp values are Unix Time in miliseconds. Time values are in miliseconds.

```
