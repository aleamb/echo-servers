# Echo server example.

This example is an implementation of a echo-server in C Language using Winsock 2 and WSAPoll function.

Based on example from book *Network Programming for Microsoft Windows, 2ed, by Anthony Jones and Jim Ohlund*

## Build

### Visual Studio command prompt

```

cl /W4 winsock2-iocp-thread.c /link ws2_32.lib

```

### Mingw64

```

gcc winsock2-iocp-thread.c -o winsock2-iocp-thread.exe -lws2_32

```

## Usage

```
winsock2-iocp-thread <port>

```

0