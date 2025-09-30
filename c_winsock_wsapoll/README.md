# Echo server example.

This example is an implementation of a echo-server in C Language using Winsock 2 with WSAPoll function and a single thread.

## Build

### Visual Studio command prompt

```

cl /W4 winsock2-wsapoll.c /link ws2_32.lib

```

### Mingw64

```

gcc winsock2-wsapoll.c -o winsock2-wsapoll.exe -lws2_32

```

## Usage

```
winsock2-wsapoll.exe <port>

```
