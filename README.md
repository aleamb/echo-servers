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
