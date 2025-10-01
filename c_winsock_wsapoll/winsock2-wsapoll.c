/*
    winsock2-wsapoll.c

    This is a simple echo server using Winsock2 API and WSAPoll

    author: Alejandro Ambroa (jandroz@gmail.com)

    To compile (using Visual Studio command prompt):
    cl /W4 winsock2-wsapoll.c /link Mswsock.lib

    or with mingw:
    gcc -Wall -o winsock2-wsapoll.exe winsock2-wsapoll.c -lws2_32

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
#define POLLCLOSE (POLLERR | POLLHUP | POLLNVAL)
#define START_CLIENT_CONNETIONS 2

#pragma comment(lib, "ws2_32")
#pragma comment(lib, "Mswsock")

typedef enum
{
    SERVER_TYPE,
    CLIENT_TYPE,
    CONTROL_TYPE
} CONNECTION_TYPE;

typedef struct
{
    WSABUF wsaBuf;
    OVERLAPPED overlapped;
    LPSOCKADDR_IN clientAddr;
    DWORD bytesSent;
    DWORD bytesReceived;
    CONNECTION_TYPE type;
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
void ServerLog(LPCONNECTION lpConnection, const char *msg, ...);
void Usage(const char *programName);
LPSERVER CreateServer(SOCKET listenerSocket);
LPCONNECTION RegisterConnection(
    LPSERVER server,
    SOCKET listenerSocket,
    LPSOCKADDR_IN clientAddr,
    CONNECTION_TYPE type);
void UnregisterConnection(LPSERVER lpServer, int index);
void RebuildServerConnectionsFds(LPSERVER lpServer);
void RequestCloseServer(LPSERVER lpServer);
void CloseServer(LPSERVER lpServer);
BOOL WINAPI CtrlHandler(DWORD fdwCtrlType);

static LPSERVER server;

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

void ServerLog(LPCONNECTION lpConnection, const char *msg, ...)
{
    char addrStr[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &lpConnection->clientAddr->sin_addr, addrStr, INET_ADDRSTRLEN);
    USHORT port = ntohs(lpConnection->clientAddr->sin_port);

    va_list args;
    va_start(args, msg);
    printf("%s:%d -> ", addrStr, port);
    vprintf(msg, args);
    printf("\n");
    va_end(args);
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
    RegisterConnection(lpServer, listenSocket, NULL, SERVER_TYPE);
    // Register a control socket to stopping WSAPoll when needed.
    SOCKET controlSock = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, WSA_FLAG_OVERLAPPED);
    RegisterConnection(lpServer, controlSock, NULL, CONTROL_TYPE);
    return lpServer;
}

LPCONNECTION RegisterConnection(LPSERVER lpServer, SOCKET socket, LPSOCKADDR_IN clientAddr, CONNECTION_TYPE type)
{
    if (lpServer->capacity < lpServer->nConnections + 1)
    {
        DWORD new_capacity = lpServer->capacity + CONNECTION_REALLOC_SIZE;

        lpServer->pollFds = (LPWSAPOLLFD)realloc((LPWSAPOLLFD)lpServer->pollFds, sizeof(WSAPOLLFD) * new_capacity);
        lpServer->connectionsData = (LPCONNECTION)realloc((LPCONNECTION)lpServer->connectionsData, sizeof(CONNECTION) * new_capacity);

        lpServer->capacity = new_capacity;
    }
    LPWSAPOLLFD pfd = &lpServer->pollFds[lpServer->nConnections];
    LPCONNECTION pConn = &lpServer->connectionsData[lpServer->nConnections];
    ZeroMemory(pConn, sizeof(CONNECTION));

    lpServer->nConnections++;

    pfd->fd = socket;
    pfd->events = POLLRDNORM;
    pfd->revents = 0;

    pConn->wsaBuf.buf = (CHAR *)malloc(DATA_BUFSIZE);
    pConn->wsaBuf.len = DATA_BUFSIZE;
    pConn->bytesReceived = 0;
    pConn->bytesSent = 0;
    if (clientAddr)
    {
        pConn->clientAddr = (LPSOCKADDR_IN)malloc(sizeof(SOCKADDR_IN));
        CopyMemory(pConn->clientAddr, clientAddr, sizeof(SOCKADDR_IN));
    }
    pConn->type = type;
    return pConn;
}

void UnregisterConnection(LPSERVER lpServer, int index)
{
    if (index < 0 || index >= lpServer->nConnections)
        return;

    LPCONNECTION pConn = &lpServer->connectionsData[index];
    closesocket(lpServer->pollFds[index].fd);
    lpServer->pollFds[index].fd = INVALID_SOCKET;
    free(pConn->wsaBuf.buf);
    if (pConn->clientAddr) {
        free(pConn->clientAddr);
    }
    ZeroMemory(pConn, sizeof(CONNECTION));
    
}

void RebuildServerConnectionsFds(LPSERVER lpServer)
{
    int j = START_CLIENT_CONNETIONS; // skip listener socket and wsapoll control socket, both in beginning of array.
    for (int i = START_CLIENT_CONNETIONS; i < lpServer->nConnections; i++)
    {
        if (lpServer->pollFds[i].fd != INVALID_SOCKET)
        {
            if (j != i)
            {
                lpServer->pollFds[j] = lpServer->pollFds[i];
                lpServer->connectionsData[j] = lpServer->connectionsData[i];
                lpServer->pollFds[i].fd = INVALID_SOCKET;
            }
            j++;
        }
    }
    lpServer->nConnections = j;
}

void CloseServer(LPSERVER lpServer)
{
    if (!lpServer)
        return;

    for (int i = lpServer->nConnections - 1; i >= 0; i--)
    {
        UnregisterConnection(lpServer, i);
    }

    free(lpServer->pollFds);
    free(lpServer->connectionsData);
    free(lpServer);
}

void RequestCloseServer(LPSERVER lpServer)
{
    closesocket(lpServer->pollFds[1].fd);
}

BOOL WINAPI CtrlHandler(DWORD fdwCtrlType)
{
    switch (fdwCtrlType)
    {
    case CTRL_C_EVENT:
        puts("Received Ctrl-C signal");
        RequestCloseServer(server);
        return TRUE;
    default:
        return FALSE;
    }
}

int main(int argc, char *argv[])
{
    WSADATA wsaData;
    SOCKADDR_IN localAddr;
    INT port;
    SOCKET listenSocket;
    INT wsaOpResult;
    SOCKADDR_IN remoteAddr;
    INT remoteLen;
    INT pollReturn;
    BOOL rebuild;
    CHAR winErrorMsgBuf[MAX_BUF_WIN_STR_ERROR];
    BOOL finish;

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
    listenSocket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);

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

    rebuild = FALSE;
    finish = FALSE;

    SetConsoleCtrlHandler(CtrlHandler, TRUE);

    printf("Server listening on port %d\n", port);

    while (!finish)
    {
        if (rebuild)
        {
            RebuildServerConnectionsFds(server);
            rebuild = FALSE;
        }

        if ((pollReturn = WSAPoll(server->pollFds, server->nConnections, -1)) != SOCKET_ERROR)
        {
            DWORD nConnections = server->nConnections;
            INT processedEvents = 0;

            for (DWORD connIndex = 0; connIndex < nConnections && processedEvents < pollReturn; connIndex++)
            {
                LPWSAPOLLFD pollFd = &server->pollFds[connIndex];
                LPCONNECTION connData = &server->connectionsData[connIndex];

                if (pollFd->fd == INVALID_SOCKET)
                    continue;

                if (connData->type == CONTROL_TYPE && (pollFd->revents & (POLLHUP | POLLERR | POLLNVAL)))
                {
                    finish = TRUE;
                }
                else if ((pollFd->revents & POLLRDNORM) && (connData->type == SERVER_TYPE))
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
                        LPCONNECTION newConnection = RegisterConnection(server, acceptSocket, &remoteAddr, CLIENT_TYPE);
                        ServerLog(newConnection, "Accepted connection");
                    }
                    processedEvents++;
                }
                else if (pollFd->revents & POLLRDNORM)
                {
                    ZeroMemory(connData->wsaBuf.buf, DATA_BUFSIZE);
                    connData->wsaBuf.len = DATA_BUFSIZE;
                    DWORD flags = 0;
                    DWORD received = 0;
                    if (WSARecv(pollFd->fd, &(connData->wsaBuf), 1, &received, &flags, NULL, NULL) == SOCKET_ERROR)
                    {
                        ServerLog(connData, "Error fetching data: %s", StrWinError(WSAGetLastError(), winErrorMsgBuf));
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
                                ServerLog(connData, "Error sending data: %s", StrWinError(wsaLastError, winErrorMsgBuf));
                            }
                            else
                            {
                                pollFd->revents = POLLWRNORM;
                            }
                        }
                    }
                    processedEvents++;
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
                                    ServerLog(connData, "Error sending data: %s", StrWinError(wsaLastError, winErrorMsgBuf));
                                }
                            }
                        }
                    }
                    else
                    {
                        DWORD overlappedOpResult = WSAGetLastError();
                        if (overlappedOpResult != WSA_IO_INCOMPLETE)
                        {
                            ServerLog(connData, "Error in overlapped send: %s", StrWinError(overlappedOpResult, winErrorMsgBuf));
                        }
                    }
                    processedEvents++;
                }
                else if (pollFd->revents & (POLLHUP | POLLERR | POLLNVAL))
                {
                    ServerLog(connData, "Closing connection.");
                    UnregisterConnection(server, connIndex);
                    rebuild = TRUE;
                    processedEvents++;
                }
            }
        }
        else
        {
            PWError("Error calling WSAPoll", winErrorMsgBuf);
            finish = TRUE;
        }
    }

    puts("Closing server...");
    CloseServer(server);
    WSACleanup();

    return EXIT_SUCCESS;
}
