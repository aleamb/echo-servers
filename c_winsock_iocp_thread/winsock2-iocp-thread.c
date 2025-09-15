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

#define DATA_BUFSIZE 512
#define MAX_CLIENTS 256
#define MAX_WORKERS 8
#define MAX_BUF_WIN_STR_ERROR 64
#define CLIENT_NO_ERROR 0

typedef struct
{
    SOCKET socket;
    SOCKADDR_IN clientAddr;
    OVERLAPPED overlapped;
    WSABUF wsaBuf;
    DWORD messages;
} CLIENT_INFO, *LPCLIENT_INFO;

typedef struct
{
    INT nClients;
    SOCKET listenSocket;
    CRITICAL_SECTION criticalSection;
    HANDLE completionPort;
    LPCLIENT_INFO *clients;
    HANDLE workers[MAX_WORKERS];
    INT activeWorkers;

} SERVER_INFO, *LPSERVER_INFO;

typedef struct
{
    HANDLE threadHandle;
    SERVER_INFO *serverInfo;

} WORKER_DATA, *LPWORKER_DATA;

void Usage(const char *programName);
void Log(const SOCKADDR_IN *addrInfo, const char *pMsg, DWORD errorCod);
void PWError(const char *msg);
INT CreateWorkerThreads(LPSERVER_INFO pServerInfo);
DWORD WINAPI ServerWorkerThread(LPVOID completionPort);
void CloseClient(LPCLIENT_INFO clientInfo, LPSERVER_INFO pServerInfo);
LPCLIENT_INFO RegisterClient(LPSERVER_INFO pServerInfo, SOCKET clientSocket, LPSOCKADDR_IN clientSockaddr, int remoteLen);
INT GetNumClients(LPSERVER_INFO pServerInfo);
int CheckOverlappedSocketOperation(DWORD overlappedSocketOperation);
void ExitHandler(void);
BOOL WINAPI CtrlHandler(DWORD fdwCtrlType);
void Cleanup();
void PrintNumClients(LPSERVER_INFO pServerInfo);

// global server info
LPSERVER_INFO serverInfo = NULL;

void Usage(const char *programName)
{
    printf("%s\nUsage: %s <port>\n", PROGRAM_VERSION, programName);
}

INT CheckOverlappedSocketOperation(DWORD wsaOpResult)
{
    if (wsaOpResult == SOCKET_ERROR)
    {
        int lastError = WSAGetLastError();
        if (lastError != WSA_IO_PENDING)
        {
            return lastError;
        }
    }
    return CLIENT_NO_ERROR;
}

LPCLIENT_INFO RegisterClient(LPSERVER_INFO pServerInfo, SOCKET clientSocket, LPSOCKADDR_IN remoteClientAddrInfo, int remoteLen)
{
    if (pServerInfo->nClients >= MAX_CLIENTS)
    {
        return NULL;
    }

    LPCLIENT_INFO clientInfo = (LPCLIENT_INFO)GlobalAlloc(GPTR, sizeof(CLIENT_INFO));
    ZeroMemory(clientInfo, sizeof(CLIENT_INFO));

    clientInfo->wsaBuf.buf = (CHAR *)GlobalAlloc(GPTR, DATA_BUFSIZE);
    ZeroMemory(clientInfo->wsaBuf.buf, sizeof(DATA_BUFSIZE));
    clientInfo->wsaBuf.len = DATA_BUFSIZE;

    clientInfo->socket = clientSocket;
    CopyMemory(&clientInfo->clientAddr, remoteClientAddrInfo, remoteLen);

    EnterCriticalSection(&pServerInfo->criticalSection);
    for (INT c = 0; c < MAX_CLIENTS; c++)
    {
        if (pServerInfo->clients[c] == NULL)
        {
            pServerInfo->clients[c] = clientInfo;
        }
    }
    pServerInfo->nClients++;
    LeaveCriticalSection(&pServerInfo->criticalSection);
    return clientInfo;
}

void CloseClient(LPCLIENT_INFO clientInfo, LPSERVER_INFO pServerInfo)
{
    closesocket(clientInfo->socket);
    GlobalFree(clientInfo->wsaBuf.buf);
    GlobalFree(clientInfo);

    EnterCriticalSection(&pServerInfo->criticalSection);
    pServerInfo->nClients--;
    for (INT c = 0; c < MAX_CLIENTS; c++)
    {
        if (pServerInfo->clients[c] == clientInfo)
        {
            pServerInfo->clients[c] = NULL;
        }
    }
    LeaveCriticalSection(&pServerInfo->criticalSection);
}

INT GetNumClients(LPSERVER_INFO pServerInfo)
{

    INT nClients;
    EnterCriticalSection(&pServerInfo->criticalSection);
    nClients = pServerInfo->nClients;
    LeaveCriticalSection(&pServerInfo->criticalSection);
    return nClients;
}

DWORD WINAPI ServerWorkerThread(LPVOID pParameter)
{
    DWORD bytesTransferred;
    LPCLIENT_INFO clientInfo;
    LPWORKER_DATA workerData;
    LPOVERLAPPED overlappedFromSocket;
    workerData = (LPWORKER_DATA)pParameter;

    while (GetQueuedCompletionStatus(workerData->serverInfo->completionPort,
                                     &bytesTransferred,
                                     (PULONG_PTR)&clientInfo,
                                     (LPOVERLAPPED *)&overlappedFromSocket,
                                     INFINITE))
    {

        if (bytesTransferred == 0)
        {
            CloseClient(clientInfo, workerData->serverInfo);
            Log(&clientInfo->clientAddr, "Connection closed", NO_ERROR);
        }
        else
        {
            DWORD flags;
            clientInfo->wsaBuf.len = bytesTransferred;

            WSASend(clientInfo->socket, &(clientInfo->wsaBuf), 1, NULL, 0, NULL, NULL);

            ZeroMemory(&(clientInfo->overlapped), sizeof(OVERLAPPED));
            ZeroMemory(clientInfo->wsaBuf.buf, DATA_BUFSIZE);
            clientInfo->wsaBuf.len = DATA_BUFSIZE;
            flags = 0;

            DWORD result = WSARecv(clientInfo->socket, &(clientInfo->wsaBuf), 1, NULL, &flags, &(clientInfo->overlapped), NULL);
            INT dw = CheckOverlappedSocketOperation(result);
            if (dw != 0)
            {
                Log(&clientInfo->clientAddr, "Error reading data. Closing connection.", dw);
                CloseClient(clientInfo, workerData->serverInfo);
            }
        }
    }

    return 0;
}

INT CreateWorkerThreads(LPSERVER_INFO pServerInfo)
{

    SYSTEM_INFO systemInfo;
    INT workersToCreate;
    INT workersCreated = 0;

    // Determine how many processors are on the system
    GetSystemInfo(&systemInfo);

    workersToCreate = min(systemInfo.dwNumberOfProcessors, MAX_WORKERS);

    // Create the worker threads. One for each processor.
    for (INT i = 0; i < workersToCreate; i++)
    {
        LPWORKER_DATA workerData = (LPWORKER_DATA)GlobalAlloc(GPTR, sizeof(WORKER_DATA));
        HANDLE threadHandle;

        threadHandle = CreateThread(NULL, 0, ServerWorkerThread, (LPVOID)workerData, 0, NULL);
        if (threadHandle == NULL)
        {
            PWError("Error creating worker thread");
            GlobalFree(workerData);
            pServerInfo->workers[i] = INVALID_HANDLE_VALUE;
        }
        else
        {
            workerData->threadHandle = threadHandle;
            workerData->serverInfo = serverInfo;
            workersCreated++;
            pServerInfo->workers[i] = threadHandle;
        }
    }
    pServerInfo->activeWorkers = workersCreated;
    return workersCreated;
}

void Log(const SOCKADDR_IN *addrInfo, const char *msg, DWORD errorCode)
{
    CHAR errorMsgBuffer[MAX_BUF_WIN_STR_ERROR] = {0};
    CHAR address_str[INET6_ADDRSTRLEN] = {'-'};
    INT port = 0;

    if (addrInfo)
    {
        inet_ntop(AF_INET, &addrInfo->sin_addr, address_str, INET6_ADDRSTRLEN);
        port = (INT)htons(addrInfo->sin_port);
    }

    if (errorCode)
    {
        if (FormatMessage(
                FORMAT_MESSAGE_FROM_SYSTEM,
                NULL,
                errorCode,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                errorMsgBuffer, MAX_BUF_WIN_STR_ERROR, NULL) == 0)
        {
            snprintf(errorMsgBuffer, MAX_BUF_WIN_STR_ERROR, "Unknown error %ld", errorCode);
        }
    }

    if (addrInfo)
    {
        printf("%s:%d -> %s: %s\n", address_str, port, msg, errorMsgBuffer);
    }
    else if (!addrInfo && errorCode)
    {
        fprintf(stderr, "%s: %s\n", msg, errorMsgBuffer);
    }
    else
    {
        fprintf(stderr, "%s\n", msg);
    }
    fflush(stdout);
}

void PWError(const char *msg)
{
    Log(NULL, msg, GetLastError());
}

void Cleanup()
{
    if (serverInfo != NULL)
    {
        CloseHandle(serverInfo->completionPort);
        DeleteCriticalSection(&serverInfo->criticalSection);
        for (INT c = 0; c < MAX_CLIENTS; c++)
        {
            LPCLIENT_INFO clientInfo = serverInfo->clients[c];
            if (clientInfo)
            {
                CloseClient(clientInfo, serverInfo);
            }
        }
        closesocket(serverInfo->listenSocket);
        GlobalFree(serverInfo->clients);
        GlobalFree(serverInfo);
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
        // Handle the CTRL-C signal.
    case CTRL_C_EVENT:
        ExitHandler();
        return FALSE;
    default:
        return FALSE;
    }
}

int main(int argc, char *argv[])
{

    WSADATA wsaData;
    SOCKADDR_IN internetAddr;
    INT port;
    SOCKET listenSocket;
    INT wsaOpResult;

    CRITICAL_SECTION criticalSection;
    HANDLE completionPort;

    INT workersCreated;
    INT exitStatus = EXIT_SUCCESS;

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

    atexit(ExitHandler);
    SetConsoleCtrlHandler(CtrlHandler, TRUE);

    serverInfo = (LPSERVER_INFO)GlobalAlloc(GPTR, sizeof(SERVER_INFO));
    ZeroMemory(serverInfo, sizeof(SERVER_INFO));
    serverInfo->clients = (LPCLIENT_INFO *)GlobalAlloc(GPTR, MAX_CLIENTS * sizeof(LPCLIENT_INFO));

    // This critical section is used to protect access to the clientsStats structure and avoid to merge log messages.
    InitializeCriticalSection(&criticalSection);

    // Initialize Winsock
    wsaOpResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaOpResult != 0)
    {
        SetLastError(wsaOpResult);
        PWError("Error initializing Winsock");
        return EXIT_FAILURE;
    }

    // Creating completion port
    completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (completionPort == NULL)
    {
        PWError("Error creating completion port");
        return EXIT_FAILURE;
    }

    // Create socket with overlapped I/O enabled.
    listenSocket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);

    if (listenSocket == INVALID_SOCKET)
    {
        PWError("Error creating server socket");
        return EXIT_FAILURE;
    }

    serverInfo->completionPort = completionPort;
    serverInfo->criticalSection = criticalSection;
    serverInfo->nClients = 0;

    workersCreated = CreateWorkerThreads(serverInfo);

    // if no worker threads were created, exit.
    if (!workersCreated)
    {
        fprintf(stderr, "No working threads created. Exiting.\n");
        Cleanup();
        return EXIT_FAILURE;
    }

    printf("Created %d worker threads.\n", workersCreated);

    serverInfo->listenSocket = listenSocket;

    // bind and set listening
    internetAddr.sin_family = AF_INET;
    internetAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    internetAddr.sin_port = htons((USHORT)port);

    // Set SO_REUSEADDR option to allow reuse of the address
    BOOL bOptVal = TRUE;
    int bOptLen = sizeof(BOOL);
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (char *)&bOptVal, bOptLen);

    wsaOpResult = bind(listenSocket, (SOCKADDR *)&internetAddr, sizeof(SOCKADDR));
    if (wsaOpResult == SOCKET_ERROR)
    {
        PWError("Error binding server socket");
        Cleanup();
        ExitProcess(1);
    }

    wsaOpResult = listen(listenSocket, SOMAXCONN);
    if (wsaOpResult == SOCKET_ERROR)
    {
        PWError("Error listening on server socket");
        closesocket(listenSocket);
        WSACleanup();
        ExitProcess(1);
    }

    printf("Server listening on port %d\n", port);

    // Server code. Accepting connections and initializing receiving data.
    while (TRUE)
    {
        SOCKADDR_IN saRemote;
        SOCKET acceptSocket;
        int remoteLen = sizeof(saRemote);

        acceptSocket = WSAAccept(listenSocket, (SOCKADDR *)&saRemote, &remoteLen, NULL, (DWORD_PTR)NULL);

        if (acceptSocket == INVALID_SOCKET)
        {
            PWError("Error creating socket connection");
            continue;
        }
        if (GetNumClients(serverInfo) >= MAX_CLIENTS)
        {
            Log(&saRemote, "Max clients exceeded. Refusing connection", NO_ERROR);
            closesocket(acceptSocket);
            continue;
        }
        LPCLIENT_INFO clientInfo = RegisterClient(serverInfo, acceptSocket, &saRemote, remoteLen);

        if (CreateIoCompletionPort((HANDLE)acceptSocket, completionPort, (ULONG_PTR)clientInfo, 0) == NULL)
        {
            Log(&saRemote, "Error at assigning completion port. Closing connection", GetLastError());
            CloseClient(clientInfo, serverInfo);
            continue;
        }
        INT overlappedOpResult;
        DWORD wsaRecvFlags;
        DWORD recvBytes;

        Log(&clientInfo->clientAddr, "Connection accepted", NO_ERROR);

        wsaRecvFlags = 0;
        wsaOpResult = WSARecv(clientInfo->socket, &(clientInfo->wsaBuf), 1, (LPDWORD)&recvBytes, (LPDWORD)&wsaRecvFlags, &(clientInfo->overlapped), NULL);
        overlappedOpResult = CheckOverlappedSocketOperation(wsaOpResult);
        if (overlappedOpResult != 0)
        {
            Log(&clientInfo->clientAddr, "Error starting reading data. Closing connection", overlappedOpResult);
            CloseClient(clientInfo, serverInfo);
        }
        printf("Current clients: %d\n", GetNumClients(serverInfo));
    }

    return exitStatus;
}
