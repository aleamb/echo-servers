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

#define PROGRAM_VERSION "v1.0.0"

#define DATA_BUFSIZE 2048
#define MAX_CLIENTS 15000
#define MAX_WORKERS 16
#define MAX_BUF_WIN_STR_ERROR 64

//#pragma comment(lib, "ws2_32")

enum OVERLAPPED_EVENT_TYPE
{
    EVENT_READ,
    EVENT_SEND
};

typedef struct
{
    OVERLAPPED overlapped;
    enum OVERLAPPED_EVENT_TYPE eventType;
    DWORD bytesReceived;
    DWORD bytesSent;

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
LPSERVER_INFO CreateServer(SOCKET listenSocket);
LPCLIENT_INFO RegisterClient(LPSERVER_INFO lpServerInfo, SOCKET clientSocket, LPSOCKADDR_IN clientSockaddr, int remoteLen);
void UnregisterClient(LPSERVER_INFO lpServerInfo, LPCLIENT_INFO clientInfo);
void CloseServer(LPSERVER_INFO lpServerInfo);
INT GetNumClients(LPSERVER_INFO lpServerInfo);
BOOL WINAPI CtrlHandler(DWORD fdwCtrlType);
void Cleanup();
void Usage(const char *programName);
char *StrWinError(DWORD errorCode, char *winErrorMsgBuffer);
void PWError(const char *mainMsg, char *winErrorMsgBuffer);
BOOL OverlappedOperationError(DWORD overlappedResultCode, LPDWORD wsaError);

#define _STRG(a) a
#define LOG_FORMAT(a) "%s:%d -> " _STRG(a) ".\n"

LPSERVER_INFO gServerInfo = NULL;

void Usage(const char *programName)
{
    printf("%s\nUsage: %s <port>\n", PROGRAM_VERSION, programName);
}

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

BOOL OverlappedOperationError(DWORD overlappedResultCode, LPDWORD wsaError)
{
    if (overlappedResultCode == 0)
    {
        return FALSE;
    }
    if (overlappedResultCode == SOCKET_ERROR)
    {
        *wsaError = WSAGetLastError();
    }
    return overlappedResultCode == SOCKET_ERROR && *wsaError != WSA_IO_PENDING;
}

LPSERVER_INFO CreateServer(SOCKET listenSocket)
{
    LPSERVER_INFO server;
    server = (LPSERVER_INFO)malloc(sizeof(SERVER_INFO));
    ZeroMemory(server, sizeof(SERVER_INFO));
    server->clients = (LPCLIENT_INFO *)malloc(MAX_CLIENTS * sizeof(LPCLIENT_INFO));
    ZeroMemory(server->clients, MAX_CLIENTS * sizeof(LPCLIENT_INFO));
    server->listenSocket = listenSocket;
    InitializeCriticalSection(&server->criticalSection);
    return server;
}

void CloseServer(LPSERVER_INFO lpServerInfo)
{
    if (!lpServerInfo)
        return;
    EnterCriticalSection(&lpServerInfo->criticalSection);
    // unregister clients.
    for (INT c = 0; c < MAX_CLIENTS; c++)
    {
        LPCLIENT_INFO clientInfo = gServerInfo->clients[c];
        if (clientInfo)
        {
            UnregisterClient(gServerInfo, clientInfo);
        }
    }
    LeaveCriticalSection(&lpServerInfo->criticalSection);
    DeleteCriticalSection(&lpServerInfo->criticalSection);
    closesocket(lpServerInfo->listenSocket);
    free(lpServerInfo->clients);
    free(lpServerInfo);
}

LPCLIENT_INFO RegisterClient(LPSERVER_INFO pServerInfo, SOCKET clientSocket, LPSOCKADDR_IN remoteClientAddrInfo, int remoteLen)
{
    LPCLIENT_INFO clientInfo = (LPCLIENT_INFO)malloc(sizeof(CLIENT_INFO));
    ZeroMemory(clientInfo, sizeof(CLIENT_INFO));

    clientInfo->wsaBuf.buf = (CHAR *)malloc(DATA_BUFSIZE);
    clientInfo->wsaBuf.len = DATA_BUFSIZE;
    ZeroMemory(clientInfo->wsaBuf.buf, DATA_BUFSIZE);

    clientInfo->socket = clientSocket;
    CopyMemory(&clientInfo->clientAddr, remoteClientAddrInfo, remoteLen);

    inet_ntop(AF_INET, &remoteClientAddrInfo->sin_addr, clientInfo->addressStr, INET_ADDRSTRLEN);
    clientInfo->port = htons(remoteClientAddrInfo->sin_port);

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

void UnregisterClient(LPSERVER_INFO lpServerInfo, LPCLIENT_INFO clientInfo)
{
    if (!clientInfo)
        return;
    closesocket(clientInfo->socket);
    free(clientInfo->wsaBuf.buf);
    free(clientInfo);

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

// Worker code. Server main logic.
DWORD WINAPI ServerWorkerThread(LPVOID pParameter)
{
    LPCLIENT_INFO clientInfo;
    LPSERVER_INFO serverInfo;
    DWORD bytesTransferred;
    LPOVERLAPPED overlappedResult;
    BOOL finish;
    CHAR winErrorMsgBuf[MAX_BUF_WIN_STR_ERROR];

    serverInfo = (LPSERVER_INFO)pParameter;
    finish = FALSE;

    /*
        Worker waits to IO events on each open socket. When un IO event
        arrives, it dequeues and inspect the event type (set in each
        WSRecv and WSASend calls)

        When IO packet is dequeued, it checks:

        1 - If packet has 0 bytes, it's due to client closed the connection.

        2 - If packet is type READ, send data back to client.
            It's important set the bytes to sent in the overlapped structure.
            That will be useful when dequeue an IO packet of send type and check if all
            data was sent.

        3 - As send is asynchronous, a packet type sent may arrive to queue.
            In this case, if bytes transfered are less than expected, send what
            remains too.

        4 - If packet is SEND type but there is no data pending to send, queue a Read
            to continue fetching data from client.
    */

    while (!finish)
    {
        if (GetQueuedCompletionStatus(serverInfo->completionPort,
                                      &bytesTransferred,
                                      (PULONG_PTR)&clientInfo,
                                      (LPOVERLAPPED *)&overlappedResult,
                                      INFINITE))
        {
            if (bytesTransferred == 0)
            {
                printf(LOG_FORMAT("Client close connection"), clientInfo->addressStr, clientInfo->port);
                UnregisterClient(serverInfo, clientInfo);
                continue;
            }

            LPEOVERLAPPED lpClientOverlapped = (LPEOVERLAPPED)overlappedResult;

            if (lpClientOverlapped->eventType == EVENT_READ ||
                (lpClientOverlapped->eventType == EVENT_SEND && bytesTransferred < lpClientOverlapped->bytesSent))
            {
                clientInfo->wsaBuf.len = bytesTransferred;
                ZeroMemory(&(clientInfo->overlapped), sizeof(EOVERLAPPED));
                clientInfo->overlapped.eventType = EVENT_SEND;
                clientInfo->overlapped.bytesSent = bytesTransferred;
                DWORD wsaError;
                DWORD ovlpOpResult = WSASend(clientInfo->socket, &(clientInfo->wsaBuf), 1, NULL, 0, (LPOVERLAPPED) & (clientInfo->overlapped), NULL);

                if (OverlappedOperationError(ovlpOpResult, &wsaError))
                {
                    printf(LOG_FORMAT("Error sending data: %s. Closing connection"), clientInfo->addressStr, clientInfo->port, StrWinError(wsaError, winErrorMsgBuf));
                    UnregisterClient(serverInfo, clientInfo);
                }
            }
            else
            {
                ZeroMemory(&(clientInfo->overlapped), sizeof(EOVERLAPPED));
                ZeroMemory(clientInfo->wsaBuf.buf, DATA_BUFSIZE);
                clientInfo->wsaBuf.len = DATA_BUFSIZE;
                clientInfo->overlapped.eventType = EVENT_READ;

                DWORD wsaError;
                DWORD flags = 0;
                DWORD opOvlResult = WSARecv(clientInfo->socket, &(clientInfo->wsaBuf), 1, NULL, &flags, (LPOVERLAPPED) & (clientInfo->overlapped), NULL);

                if (OverlappedOperationError(opOvlResult, &wsaError))
                {
                    printf(LOG_FORMAT("Error fetching data: %s. Closing connection"), clientInfo->addressStr, clientInfo->port, StrWinError(wsaError, winErrorMsgBuf));
                    UnregisterClient(serverInfo, clientInfo);
                }
            }
        }
        else
        {
            DWORD iocpLastError = WSAGetLastError();

            // triggered when thread is killed due to the application finish.
            if (iocpLastError == ERROR_ABANDONED_WAIT_0)
            {
                finish = TRUE;
            }
            else
            {
                SetLastError(iocpLastError);
                PWError("IOCP error in worker thread", winErrorMsgBuf);
                if (clientInfo)
                {
                    UnregisterClient(serverInfo, clientInfo);
                }
            }
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

    GetSystemInfo(&systemInfo);

    workersToCreate = min(systemInfo.dwNumberOfProcessors, MAX_WORKERS);

    for (INT i = 0; i < workersToCreate; i++)
    {
        if (CreateThread(NULL, 0, ServerWorkerThread, (LPVOID)lpServerInfo, 0, NULL))
        {
            workersCreated++;
        }
        else
        {
            PWError("Error creating a thread", winErrorMsgBuf);
        }
    }
    return workersCreated;
}

void Cleanup()
{
    if (gServerInfo != NULL)
    {
        CloseHandle(gServerInfo->completionPort);
        CloseServer(gServerInfo);
        WSACleanup();
        gServerInfo = NULL;
    }
}

BOOL WINAPI CtrlHandler(DWORD fdwCtrlType)
{
    switch (fdwCtrlType)
    {
    case CTRL_C_EVENT:
        puts("Closing server...");
        Cleanup();
        return FALSE;
    default:
        return FALSE;
    }
}

int main(int argc, char *argv[])
{

    WSADATA wsaData;
    SOCKADDR_IN internetAddr;
    INT serverPort;
    SOCKET listenSocket;
    INT wsaOpResult;
    HANDLE completionPort;

    INT workersCreated;
    INT exitStatus = EXIT_SUCCESS;
    CHAR winErrorMsgBuf[MAX_BUF_WIN_STR_ERROR];
    LPSERVER_INFO serverInfo;

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

    SetConsoleCtrlHandler(CtrlHandler, TRUE);

    wsaOpResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaOpResult != 0)
    {
        SetLastError(wsaOpResult);
        PWError("Error initializing winsock", winErrorMsgBuf);
        return EXIT_FAILURE;
    }

    listenSocket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);

    if (listenSocket == INVALID_SOCKET)
    {
        PWError("Error creating socket port", winErrorMsgBuf);
        WSACleanup();
        return EXIT_FAILURE;
    }
    
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
        PWError("Error binding port", winErrorMsgBuf);
        return EXIT_FAILURE;
    }

    wsaOpResult = listen(listenSocket, SOMAXCONN);
    if (wsaOpResult == SOCKET_ERROR)
    {
        PWError("Error listening on port", winErrorMsgBuf);
        return EXIT_FAILURE;
    }

    // The famous IOCP
    completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (completionPort == NULL)
    {
        PWError("Error creating IOCP", winErrorMsgBuf);
        closesocket(listenSocket);
        WSACleanup();
        return EXIT_FAILURE;
    }

    gServerInfo = serverInfo = CreateServer(listenSocket);
    serverInfo->completionPort = completionPort;
    serverInfo->listenSocket = listenSocket;

    workersCreated = CreateWorkerThreads(serverInfo);

    // if no worker threads were created, exit.
    if (!workersCreated)
    {
        fprintf(stderr, "Error creating all workers. Exiting.");
        Cleanup();
        return EXIT_FAILURE;
    }

    printf("Server listening on port %d. Workers: %d\n", serverPort, workersCreated);

    // Loop for accepting connections.
    while (TRUE)
    {
        SOCKADDR_IN saRemote;
        SOCKET acceptSocket;
        int remoteLen = sizeof(saRemote);

        acceptSocket = WSAAccept(listenSocket, (SOCKADDR *)&saRemote, &remoteLen, NULL, (DWORD_PTR)NULL);

        if (acceptSocket == INVALID_SOCKET)
        {
            PWError("Error accepting a connection attempt", winErrorMsgBuf);
            continue;
        }

        if (GetNumClients(serverInfo) >= MAX_CLIENTS)
        {
            fprintf(stderr, "Max clients exceeded\n");
            closesocket(acceptSocket);
            continue;
        }

        LPCLIENT_INFO clientInfo = RegisterClient(serverInfo, acceptSocket, &saRemote, remoteLen);

        printf(LOG_FORMAT("Connected"), clientInfo->addressStr, clientInfo->port);

        // assign IOCP to socket to receive I/O events.
        if (CreateIoCompletionPort((HANDLE)acceptSocket, completionPort, (ULONG_PTR)clientInfo, 0) == NULL)
        {
            PWError("Error when assigning socket to IOCP", winErrorMsgBuf);
            UnregisterClient(serverInfo, clientInfo);
            continue;
        }

        // begin reading so Workers can process the completion reads.
        DWORD wsaRecvFlags = 0;
        DWORD ovlpOpResult;
        DWORD wsaErrorCode;

        clientInfo->overlapped.eventType = EVENT_READ;
        ovlpOpResult = WSARecv(clientInfo->socket, &(clientInfo->wsaBuf), 1, NULL,
                               (LPDWORD)&wsaRecvFlags, (LPOVERLAPPED)&clientInfo->overlapped, NULL);
        if (OverlappedOperationError(ovlpOpResult, &wsaErrorCode))
        {
            // if starting reading fails, close connection client directly.
            printf(LOG_FORMAT("Error starting receiving data: %s"),
                   clientInfo->addressStr, clientInfo->port, StrWinError(wsaErrorCode, winErrorMsgBuf));
            UnregisterClient(serverInfo, clientInfo);
        }
    }

    return exitStatus;
}
