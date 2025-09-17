/*
    winsock2-iocp-thread.c

    This is a simple echo server using Winsock2 API with IOCP and worker threads.

    Based on example from book Network Programming for Microsoft Windows, 2ed, by Anthony Jones and Jim Ohlund

    author: Alejandro Ambroa (jandroz@gmail.com)

    To compile (using Visual Studio command prompt):
    cl /W4 winsock2-iocp-thread.c /link ws2_32.lib

    or with mingw:
    gcc -Wall -o winsock2-iocp-thread.exe winsock2-iocp-thread.c -lws2_32

    Tested with Visual Studio 2022 and mingw64, Windows 11.
*/

#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define PROGRAM_VERSION "echo-servers/c_winsock_iocp_thread v1.0.0"

#define DATA_BUFSIZE 1024
#define MAX_CLIENTS 256
#define MAX_WORKERS 16
#define MAX_BUF_WIN_STR_ERROR 32
#define MAX_BUF_LOG_STR INET_ADDRELEN + 6 + 48

#pragma comment(lib, "ws2_32")

enum OVERLAPPED_EVENT_TYPE
{
    EVENT_READ,
    EVENT_SEND
};

typedef struct
{
    OVERLAPPED overlapped;
    enum OVERLAPPED_EVENT_TYPE eventType;
    INT bytesReceived;
    INT bytesSent;

} EOVERLAPPED, *LPEOVERLAPPED;

typedef struct
{
    EOVERLAPPED overlapped;
    SOCKET socket;
    SOCKADDR_IN clientAddr;
    WSABUF wsaBuf;
    char addressStr[INET_ADDRSTRLEN]; // help with logging
    USHORT port;
} CLIENT_INFO, *LPCLIENT_INFO;

typedef struct
{
    INT nClients;
    SOCKET listenSocket;
    HANDLE completionPort;
    CRITICAL_SECTION criticalSection;
    LPCLIENT_INFO *clients;
} SERVER_INFO, *LPSERVER_INFO;

INT CreateWorkerThreads(LPSERVER_INFO lpServerInfo);
DWORD WINAPI ServerWorkerThread(LPVOID completionPort);
LPSERVER_INFO CreateServer();
LPCLIENT_INFO RegisterClient(LPSERVER_INFO lpServerInfo, SOCKET clientSocket, LPSOCKADDR_IN clientSockaddr, int remoteLen);
void CloseClient(LPSERVER_INFO lpServerInfo, LPCLIENT_INFO clientInfo);
INT GetNumClients(LPSERVER_INFO lpServerInfo);
void ExitHandler(void);
BOOL WINAPI CtrlHandler(DWORD fdwCtrlType);
void Cleanup();
void Usage(const char *programName);

LPSERVER_INFO gServerInfo = NULL;

void Usage(const char *programName)
{
    printf("%s\nUsage: %s <port>\n", PROGRAM_VERSION, programName);
}

LPSERVER_INFO CreateServer()
{

    LPSERVER_INFO server;
    server = (LPSERVER_INFO)GlobalAlloc(GPTR, sizeof(SERVER_INFO));
    ZeroMemory(server, sizeof(SERVER_INFO));
    server->clients = (LPCLIENT_INFO *)GlobalAlloc(GPTR, MAX_CLIENTS * sizeof(LPCLIENT_INFO));
    return server;
}

LPCLIENT_INFO RegisterClient(LPSERVER_INFO pServerInfo, SOCKET clientSocket, LPSOCKADDR_IN remoteClientAddrInfo, int remoteLen)
{
    LPCLIENT_INFO clientInfo = (LPCLIENT_INFO)GlobalAlloc(GPTR, sizeof(CLIENT_INFO));
    ZeroMemory(clientInfo, sizeof(CLIENT_INFO));

    clientInfo->wsaBuf.buf = (CHAR *)GlobalAlloc(GPTR, DATA_BUFSIZE);
    clientInfo->wsaBuf.len = DATA_BUFSIZE;
    ZeroMemory(clientInfo->wsaBuf.buf, sizeof(DATA_BUFSIZE));

    clientInfo->socket = clientSocket;
    CopyMemory(&clientInfo->clientAddr, remoteClientAddrInfo, remoteLen);

    EnterCriticalSection(&pServerInfo->criticalSection);
    for (INT c = 0; c < MAX_CLIENTS; c++)
    {
        if (pServerInfo->clients[c] == NULL)
        {
            pServerInfo->clients[c] = clientInfo;
            break;
        }
    }
    pServerInfo->nClients++;
    LeaveCriticalSection(&pServerInfo->criticalSection);

    return clientInfo;
}

INT GetNumClients(LPSERVER_INFO lpServerInfo)
{
    INT clients;
    EnterCriticalSection(&lpServerInfo->criticalSection);
    clients = lpServerInfo->nClients;
    LeaveCriticalSection(&lpServerInfo->criticalSection);
    return clients;
}

void CloseClient(LPSERVER_INFO lpServerInfo, LPCLIENT_INFO clientInfo)
{
    closesocket(clientInfo->socket);
    GlobalFree(clientInfo->wsaBuf.buf);
    GlobalFree(clientInfo);

    EnterCriticalSection(&lpServerInfo->criticalSection);
    lpServerInfo->nClients--;
    for (INT c = 0; c < MAX_CLIENTS; c++)
    {
        if (lpServerInfo->clients[c] == clientInfo)
        {
            lpServerInfo->clients[c] = NULL;
        }
    }
    LeaveCriticalSection(&lpServerInfo->criticalSection);
}

DWORD WINAPI ServerWorkerThread(LPVOID pParameter)
{
    LPCLIENT_INFO clientInfo;
    LPSERVER_INFO serverInfo;
    DWORD bytesTransferred;
    LPOVERLAPPED overlappedResult;
    BOOL iocpOk;
    BOOL finish;
    CHAR winErrorMsgBuf[MAX_BUF_WIN_STR_ERROR];

    serverInfo = (LPSERVER_INFO)pParameter;
    finish = FALSE;

    while (!finish)
    {
        iocpOk = GetQueuedCompletionStatus(serverInfo->completionPort,
                                           &bytesTransferred,
                                           (PULONG_PTR)&clientInfo,
                                           (LPOVERLAPPED *)&overlappedResult,
                                           INFINITE);

        DWORD iocpLastError = WSAGetLastError();

        if (iocpOk)
        {

            if (bytesTransferred == 0)
            {
                CloseClient(serverInfo, clientInfo);
            }
            else
            {

                LPEOVERLAPPED lpClientOverlapped = (LPEOVERLAPPED)overlappedResult;

                if (lpClientOverlapped->eventType == EVENT_READ || (lpClientOverlapped->eventType == EVENT_SEND && bytesTransferred > lpClientOverlapped->bytesSent))
                {
                    CopyMemory(clientInfo->wsaBuf.buf, clientInfo->wsaBuf.buf, bytesTransferred);
                    clientInfo->wsaBuf.len = bytesTransferred;
                    ZeroMemory(&(clientInfo->overlapped), sizeof(EOVERLAPPED));
                    clientInfo->overlapped.eventType = EVENT_SEND;
                    clientInfo->overlapped.bytesSent = bytesTransferred;
                    if (WSASend(clientInfo->socket, &(clientInfo->wsaBuf), 1, NULL, 0, (LPOVERLAPPED) & (clientInfo->overlapped), NULL) == SOCKET_ERROR)
                    {
                        DWORD wsaLastError = WSAGetLastError();
                        if (wsaLastError != WSA_IO_PENDING)
                        {
                            CloseClient(serverInfo, clientInfo);
                        }
                    }
                }
                else
                {

                    ZeroMemory(&(clientInfo->overlapped), sizeof(EOVERLAPPED));
                    ZeroMemory(clientInfo->wsaBuf.buf, DATA_BUFSIZE);
                    clientInfo->wsaBuf.len = DATA_BUFSIZE;
                    clientInfo->overlapped.eventType = EVENT_READ;
                    DWORD flags;
                    if (WSARecv(clientInfo->socket, &(clientInfo->wsaBuf), 1, NULL,
                                &flags, (LPOVERLAPPED) & (clientInfo->overlapped), NULL) == SOCKET_ERROR)
                    {
                        DWORD wsaLastError = WSAGetLastError();
                        if (wsaLastError != WSA_IO_PENDING)
                        {
                            CloseClient(serverInfo, clientInfo);
                        }
                    }
                }
            }
        }
        else
        {
            CloseClient(serverInfo, clientInfo);
        }
    }
    return 0;
}

INT CreateWorkerThreads(LPSERVER_INFO lpServerInfo)
{

    SYSTEM_INFO systemInfo;
    INT workersToCreate;
    INT workersCreated = 0;
    CHAR winErrorMsgBuf[MAX_BUF_WIN_STR_ERROR];

    // Determine how many processors are on the system
    GetSystemInfo(&systemInfo);

    workersToCreate = min(systemInfo.dwNumberOfProcessors, MAX_WORKERS);

    // Create the worker threads. One for each processor.
    for (INT i = 0; i < workersToCreate; i++)
    {
        if (CreateThread(NULL, 0, ServerWorkerThread, (LPVOID)lpServerInfo, 0, NULL))
        {

            workersCreated++;
        }
    }
    return workersCreated;
}

void Cleanup()
{
    if (gServerInfo != NULL)
    {
        CloseHandle(gServerInfo->completionPort);
        DeleteCriticalSection(&gServerInfo->criticalSection);
        for (INT c = 0; c < MAX_CLIENTS; c++)
        {
            LPCLIENT_INFO clientInfo = gServerInfo->clients[c];
            if (clientInfo)
            {
                CloseClient(gServerInfo, clientInfo);
            }
        }
        closesocket(gServerInfo->listenSocket);
        GlobalFree(gServerInfo->clients);
        GlobalFree(gServerInfo);
        WSACleanup();
    }
}

void ExitHandler(void)
{
    puts("Closing server...");
    Cleanup();
}

BOOL WINAPI CtrlHandler(DWORD fdwCtrlType)
{
    switch (fdwCtrlType)
    {
    case CTRL_C_EVENT:
        ExitHandler();
        return FALSE;
    default:
        return TRUE;
    }
}

int main(int argc, char *argv[])
{

    WSADATA wsaData;
    SOCKADDR_IN internetAddr;
    INT serverPort;
    SOCKET listenSocket;
    INT wsaOpResult;

    CRITICAL_SECTION criticalSection;
    HANDLE completionPort;

    INT workersCreated;
    INT exitStatus = EXIT_SUCCESS;
    CHAR winErrorMsgBuf[MAX_BUF_WIN_STR_ERROR];
    LPSERVER_INFO serverInfo;

    puts(PROGRAM_VERSION);

    if (argc < 2)
    {
        Usage(argv[0]);
        return EXIT_FAILURE;
    }

    serverPort = atoi(argv[1]);

    if (serverPort <= 0)
    {
        fprintf(stderr, "Invalid port number\n");
        return EXIT_FAILURE;
    }

    atexit(ExitHandler);
    SetConsoleCtrlHandler(CtrlHandler, TRUE);

    gServerInfo = serverInfo = CreateServer();

    // This critical section is used to protect access to the clientsStats structure and avoid to merge log messages.
    InitializeCriticalSection(&criticalSection);

    // Initialize Winsock
    wsaOpResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaOpResult != 0)
    {
        return EXIT_FAILURE;
    }

    // Creating completion port
    completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (completionPort == NULL)
    {
        return EXIT_FAILURE;
    }

    // Create socket with overlapped I/O enabled.
    listenSocket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);

    if (listenSocket == INVALID_SOCKET)
    {

        return EXIT_FAILURE;
    }

    serverInfo->completionPort = completionPort;
    serverInfo->criticalSection = criticalSection;
    serverInfo->listenSocket = listenSocket;

    workersCreated = CreateWorkerThreads(serverInfo);

    // if no worker threads were created, exit.
    if (!workersCreated)
    {
        return EXIT_FAILURE;
    }

    printf("Created %d worker threads.\n", workersCreated);

    serverInfo->listenSocket = listenSocket;

    // bind and set listening
    internetAddr.sin_family = AF_INET;
    internetAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    internetAddr.sin_port = htons((USHORT)serverPort);

    // Set SO_REUSEADDR option to allow reuse of the address
    BOOL bOptVal = TRUE;
    int bOptLen = sizeof(BOOL);
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (char *)&bOptVal, bOptLen);

    wsaOpResult = bind(listenSocket, (SOCKADDR *)&internetAddr, sizeof(SOCKADDR));
    if (wsaOpResult == SOCKET_ERROR)
    {
        return EXIT_FAILURE;
    }

    wsaOpResult = listen(listenSocket, SOMAXCONN);
    if (wsaOpResult == SOCKET_ERROR)
    {
        return EXIT_FAILURE;
    }

    printf("Server listening on port %d\n", serverPort);

    // Server code. Accepting connections and send (echo) receiving data.
    while (TRUE)
    {
        SOCKADDR_IN saRemote;
        SOCKET acceptSocket;
        int remoteLen = sizeof(saRemote);
        char addressStr[INET_ADDRSTRLEN];
        USHORT clientPort;

        acceptSocket = WSAAccept(listenSocket, (SOCKADDR *)&saRemote, &remoteLen, NULL, (DWORD_PTR)NULL);

        if (acceptSocket == INVALID_SOCKET)
        {
            continue;
        }

        inet_ntop(AF_INET, &saRemote.sin_addr, addressStr, INET_ADDRSTRLEN);
        clientPort = htons(saRemote.sin_port);

        if (GetNumClients(serverInfo) >= MAX_CLIENTS)
        {
            closesocket(acceptSocket);
            continue;
        }

        LPCLIENT_INFO clientInfo = RegisterClient(serverInfo, acceptSocket, &saRemote, remoteLen);

        if (CreateIoCompletionPort((HANDLE)acceptSocket, completionPort, (ULONG_PTR)clientInfo, 0) == NULL)
        {
            CloseClient(serverInfo, clientInfo);
            continue;
        }

        DWORD wsaRecvFlags = 0;
        clientInfo->overlapped.eventType = EVENT_READ;
        if (WSARecv(clientInfo->socket,
                    &(clientInfo->wsaBuf),
                    1,
                    NULL,
                    (LPDWORD)&wsaRecvFlags,
                    (LPOVERLAPPED)&clientInfo->overlapped, NULL) == SOCKET_ERROR)
        {

            DWORD wsaLastError = WSAGetLastError();

            if (wsaLastError != WSA_IO_PENDING)
            {
                CloseClient(serverInfo, clientInfo);
            }
        }
    }

    return exitStatus;
}
