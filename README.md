# Implementations of a echo server

This repo contains a echo-servers implemented using differents languages/frameworks.

## Implementations

| Dir                    | Contents                                                                                   | Language | Platform | Technologies    |
|------------------------|--------------------------------------------------------------------------------------------|----------|----------|-----------------|
| c_winsock_iocp_thread  | Winsock 2 implementation using I/O Completion Ports, Overlapped sockets and worker threads | C        | Windows  | Winsock 2, IOCP |

## WIP:

| Dir                    | Contents                                                                                   | Language | Platform | Technologies       |
|------------------------|--------------------------------------------------------------------------------------------|----------|----------|--------------------|
| c_winsock_wsapoll      | Winsock 2 implementation using WSAPoll and single thread                                   | C        | Windows  | Winsock 2, WSAPoll |
| c_linux_epoll          | Linux epoll implementation                                                                 | C        | Linux    | epoll              |
| c_bsd_kqueue           | BSD kqueue implementation                                                                  | C        | BSD      | kqueue             |
| c_libuv                | Libuv implementation                                                                       | C        | Multi    | libuv              |
| Java NIO               | Java 21 using NIO single threaded                                                          | Java     | Multi    | Java NIO           |
| Java Netty             | Java 21 using Netty                                                                        | Java     | Multi    | Java, Netty        |
| Java NIO MT            | Java 21 using NIO and virtual threads                                                      | Java     | Multi    | Java NIO, VT       |
| Python                 |                                                                                            | Python   | Multi    | Python             |
| C++ Boost.Asio         | C++ using Boost.Asio                                                                       | C++      | Multi    | C++, Boost.Asio    |


## Testing

test_echo_server.py is a simple utility to test echo servers. Run with Python >= 3.9.

Description of utility:

```
usage: test_echo_server [-h] [-l LENGTH] [-i INTERVAL] [-r NUM] [-t] [-c CONCURRENCY] host port

Tests echo servers sending generated variable data.

positional arguments:
  host                  host or ip of the  server to test
  port                  port

options:
  -h, --help            show this help message and exit
  -l LENGTH, --length LENGTH
                        length of string to send. If value is zero, tester connect and disconnect.
  -i INTERVAL, --interval INTERVAL
                        interval, in miliseconds, between each send of a string. Can be zero.
  -r NUM, --num NUM     Num messages to send
  -t, --table           Generate table data (num_message, response time) instead prints average response time
  -c CONCURRENCY, --concurrency CONCURRENCY
                        Concurrent connections

Table format is:

Send timestamp,finish send timestamp,timestamp of receiving response data,response time,length of data sent,length of data received,thread id,error flag (0 if no error)

The error is marked when sent data and received data are not equals. Last column set to 1 if error occurs.

```