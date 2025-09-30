/*
    winsock2-wsapoll.c

    This is a simple echo server using Winsock2 API and WSAPoll

    author: Alejandro Ambroa (jandroz@gmail.com)

    To compile (using Visual Studio command prompt):
    cl /W4 winsock2-wsapoll.c /link ws2_32.lib,Mswsock.lib

    or with mingw:
    gcc -Wall -o winsock2-iocp-thread.exe winsock2-iocp-thread.c -lws2_32

    Tested with Visual Studio 2022 and mingw64, Windows 11.
*/

#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>

#define PROGRAM_VERSION "v1.0.0"
#define MAX_BUF_WIN_STR_ERROR 64
#define DATA_BUFSIZE 2048
#define CONNECTION_REALLOC_SIZE 10

#define _STRG(a) a
#define LOG_FORMAT(a) "%s:%d -> " _STRG(a) ".\n"

#pragma comment(lib, "ws2_32")
#pragma comment(lib, "Mswsock")

typedef struct
{
    WSABUF wsaBuf;
    OVERLAPPED overlapped;
    DWORD bytesSent;
    DWORD bytesReceived;
} CONNECTION, *LPCONNECTION;

typedef struct
{
    LPWSAPOLLFD pollFds;
    LPCONNECTION connectionsData;
    int nConnections;
    int capacity;

} SERVER, *LPSERVER;

char *StrWinError(DWORD errorCode, char *winErrorMsgBuffer);
void PWError(const char *mainMsg, char *winErrorMsgBuffer);
void Usage(const char *programName);
LPSERVER CreateServer(SOCKET listenerSocket);
LPCONNECTION RegisterConnection(LPSERVER server, SOCKET listenerSocket);
void UnregisterClient(LPSERVER lpServer, int index);

static BOOL finish = FALSE;

char *StrWinError(DWORD errorCode, char *winErrorMsgBuffer)
{
    if (FormatMessage(
            FORMAT_MESSAGE_FROM_SYSTEM,
            NULL,
            errorCode,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            winErrorMsgBuffer, MAX_BUF_WIN_STR_ERROR, NULL) == 0)
    {
        snprintf(winErrorMsgBuffer, MAX_BUF_WIN_STR_ERROR, "Unknown error %ld", errorCode);
    }
    return winErrorMsgBuffer;
}

void PWError(const char *mainMsg, char *winErrorMsgBuffer)
{
    DWORD lastError = GetLastError();
    fprintf(stderr, "%s : %s (%ld)\n", mainMsg, StrWinError(lastError, winErrorMsgBuffer), lastError);
}

void Usage(const char *programName)
{
    printf("Usage: %s <port>\n", programName);
}

LPSERVER CreateServer(SOCKET listenSocket)
{
    LPSERVER lpServer = (LPSERVER)malloc(sizeof(SERVER));
    ZeroMemory(lpServer, sizeof(SERVER));
    RegisterConnection(lpServer, listenSocket);
    return lpServer;
}

LPCONNECTION RegisterConnection(LPSERVER lpServer, SOCKET socket)
{
    if (lpServer->capacity < lpServer->nConnections + 1)
    {

        DWORD new_capacity = lpServer->capacity + CONNECTION_REALLOC_SIZE;

        lpServer->pollFds = (LPWSAPOLLFD)realloc((LPWSAPOLLFD)lpServer->pollFds, sizeof(WSAPOLLFD) * new_capacity);
        ZeroMemory(&lpServer->pollFds[lpServer->nConnections], sizeof(WSAPOLLFD) * (new_capacity - lpServer->capacity));
        lpServer->connectionsData = (LPCONNECTION)realloc((LPCONNECTION)lpServer->connectionsData, sizeof(CONNECTION) * new_capacity);

        lpServer->capacity = new_capacity;
    }
    LPWSAPOLLFD pfd = &lpServer->pollFds[lpServer->nConnections];
    LPCONNECTION pConn = &lpServer->connectionsData[lpServer->nConnections];
    lpServer->nConnections++;

    pfd->fd = socket;
    pfd->events = POLLRDNORM;
    pfd->revents = 0;

    pConn->wsaBuf.buf = (CHAR *)malloc(DATA_BUFSIZE);
    pConn->wsaBuf.len = DATA_BUFSIZE;
    pConn->bytesReceived = 0;
    pConn->bytesSent = 0;
    return pConn;
}

void UnregisterClient(LPSERVER lpServer, int index)
{
    if (index < 0 || index >= lpServer->nConnections)
        return;

    LPCONNECTION pConn = &lpServer->connectionsData[index];
    closesocket(lpServer->pollFds[index].fd);
    free(pConn->wsaBuf.buf);
    lpServer->pollFds[index].fd = INVALID_SOCKET;
}

int main(int argc, char *argv[])
{
    WSADATA wsaData;
    SOCKADDR_IN localAddr;
    INT port;
    SOCKET listenSocket;
    INT wsaOpResult;
    INT exitStatus;
    SOCKADDR_IN remoteAddr;
    INT remoteLen;
    INT pollReturn;
    LPSERVER server;
    BOOL rebuild;
    CHAR winErrorMsgBuf[MAX_BUF_WIN_STR_ERROR];

    puts(PROGRAM_VERSION);

    if (argc < 2)
    {
        Usage(argv[0]);
        return EXIT_FAILURE;
    }

    port = atoi(argv[1]);

    if (port <= 0)
    {
        fprintf(stderr, "Invalid port number\n");
        return EXIT_FAILURE;
    }

    // Initialize Winsock
    wsaOpResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaOpResult != 0)
    {
        SetLastError(wsaOpResult);
        PWError("Error initializing Winsock", winErrorMsgBuf);
        return EXIT_FAILURE;
    }

    // Create socket with overlapped I/O enabled.
    listenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);

    if (listenSocket == INVALID_SOCKET)
    {
        PWError("Error creating server socket", winErrorMsgBuf);
        return EXIT_FAILURE;
    }

    // bind and set listening
    localAddr.sin_family = AF_INET;
    localAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    localAddr.sin_port = htons((USHORT)port);

    // Set SO_REUSEADDR option to allow reuse of the address
    BOOL bOptVal = TRUE;
    int bOptLen = sizeof(BOOL);
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (char *)&bOptVal, bOptLen);

    wsaOpResult = bind(listenSocket, (SOCKADDR *)&localAddr, sizeof(SOCKADDR));
    if (wsaOpResult == SOCKET_ERROR)
    {
        PWError("Error binding server socket", winErrorMsgBuf);
        return EXIT_FAILURE;
    }

    wsaOpResult = listen(listenSocket, SOMAXCONN);
    if (wsaOpResult == SOCKET_ERROR)
    {
        PWError("Error listening on server socket", winErrorMsgBuf);
        return EXIT_FAILURE;
    }

    remoteLen = sizeof(remoteAddr);

    server = CreateServer(listenSocket);

    exitStatus = EXIT_SUCCESS;
    rebuild = FALSE;

    printf("Server listening on port %d\n", port);

  
    while (!finish)
    {
        if (rebuild)
        {
            int j = 1;
            for (int i = 1; i < server->nConnections; i++)
            {
                if (server->pollFds[i].fd == INVALID_SOCKET)
                {
                    if (server->pollFds[j].fd != INVALID_SOCKET)
                        j = i;
                } else {
                    if (j != i) {
                        server->pollFds[j] = server->pollFds[i];
                        server->connectionsData[j] = server->connectionsData[i];
                        server->pollFds[i].fd = INVALID_SOCKET;
                    } 
                    j++;
                }
            }
            server->nConnections = j;
            rebuild = FALSE;
         
        }

        if (SOCKET_ERROR != (pollReturn = WSAPoll(server->pollFds, server->nConnections, -1)))
        {

            DWORD nConnections = server->nConnections;

            for (int connIndex = 0; connIndex < nConnections; connIndex++)
            {

                LPWSAPOLLFD pollFd = &server->pollFds[connIndex];
                LPCONNECTION connData = &server->connectionsData[connIndex];

                if (pollFd->fd == INVALID_SOCKET)
                    continue;

                if ((pollFd->revents & POLLRDNORM) && (pollFd->fd == listenSocket))
                {

                    SOCKET acceptSocket = WSAAccept(listenSocket, (SOCKADDR *)&remoteAddr, &remoteLen, NULL, (DWORD_PTR)NULL);

                    if (acceptSocket == INVALID_SOCKET)
                    {
                        DWORD wsaError = WSAGetLastError();
                        SetLastError(wsaError);
                        PWError("Error accepting connection.", winErrorMsgBuf);
                    }
                    else
                    {
                        printf("Accepted connection from %s:%d\n", inet_ntoa(remoteAddr.sin_addr), ntohs(remoteAddr.sin_port));
                        RegisterConnection(server, acceptSocket);
                    }
                }
                else if (pollFd->revents & POLLRDNORM)
                {
                    ZeroMemory(connData->wsaBuf.buf, DATA_BUFSIZE);
                    connData->wsaBuf.len = DATA_BUFSIZE;
                    DWORD flags = 0;
                    DWORD received = 0;
                    if (WSARecv(pollFd->fd, &(connData->wsaBuf), 1, &received, &flags, NULL, NULL) == SOCKET_ERROR)
                    {
                        printf("Error fetching data: %s\n", StrWinError(WSAGetLastError(), winErrorMsgBuf));
                    }
                    else
                    {
                        DWORD bytesSent = 0;
                        connData->wsaBuf.len = received;
                        flags = 0;
                        ZeroMemory(&connData->overlapped, sizeof(OVERLAPPED));
                        INT sendReturn = WSASend(pollFd->fd, &(connData->wsaBuf), 1, &bytesSent, flags, (LPOVERLAPPED)&connData->overlapped, NULL);
                        if (sendReturn == SOCKET_ERROR)
                        {
                            DWORD wsaLastError = WSAGetLastError();
                            if (wsaLastError != WSA_IO_PENDING)
                            {
                                printf("Error sending data: %s\n", StrWinError(wsaLastError, winErrorMsgBuf));
                            }
                            else
                            {
                                pollFd->revents = POLLWRNORM;
                            }
                        }
                    }
                }
                else if (pollFd->revents & POLLWRNORM)
                {

                    if (WSAGetOverlappedResult(pollFd->fd, (LPOVERLAPPED)&connData->overlapped, &connData->bytesSent, FALSE, NULL))
                    {
                        if (connData->bytesSent == connData->wsaBuf.len)
                        {
                            pollFd->events = POLLRDNORM;
                        }
                        else
                        {
                            connData->wsaBuf.buf += connData->bytesSent;
                            connData->wsaBuf.len -= connData->bytesSent;
                            DWORD bytesSent = 0;
                            DWORD flags = 0;
                            ZeroMemory(&connData->overlapped, sizeof(OVERLAPPED));
                            INT sendReturn = WSASend(pollFd->fd, &(connData->wsaBuf), 1, &bytesSent, flags, (LPOVERLAPPED)&connData->overlapped, NULL);
                            if (sendReturn == SOCKET_ERROR)
                            {
                                DWORD wsaLastError = WSAGetLastError();
                                if (wsaLastError != WSA_IO_PENDING)
                                {
                                    printf("Error sending data: %s\n", StrWinError(wsaLastError, winErrorMsgBuf));
                                }
                            }
                        }
                    }
                    else 
                    {
                        DWORD overlappedOpResult = WSAGetLastError();
                        if (overlappedOpResult != WSA_IO_INCOMPLETE)
                        {
                            printf("Error in overlapped send: %s\n", StrWinError(overlappedOpResult, winErrorMsgBuf));
                        }
                    }
                } else if (pollFd->revents & (POLLHUP | POLLERR | POLLNVAL))
                {
                    printf("Closing connection on socket %d\n", (int)pollFd->fd);
                    UnregisterClient(server, connIndex);
                    rebuild = TRUE;
                }
            }
        }
        else
        {
            PWError("Error CALLING WSAPoll", winErrorMsgBuf);
            finish = TRUE;
        }
    }

    closesocket(listenSocket);
    WSACleanup();

    return exitStatus;
}
