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
#define MAX_WORKERS 1
#define MAX_CLIENTS 128

#define MAX_DEBUG_MSG_SIZE_TO_PRINT 128
#define NO_PERROR 0

#define CLIENT_NO_ERROR 0

typedef struct
{
    SOCKET socket;
    SOCKADDR_IN clientAddr;
    OVERLAPPED overlapped;
    WSABUF wsaBuf;
    CHAR address_str[INET6_ADDRSTRLEN + 6]; // address + port
} CLIENT_INFO, *LPCLIENT_INFO;

typedef struct
{
    INT nClients;
    SOCKET listenSocket;
    CRITICAL_SECTION criticalSection;
    HANDLE completionPort;
    LPCLIENT_INFO *clients;

} SERVER_INFO, *LPSERVER_INFO;

typedef struct
{
    HANDLE threadHandle;
    SERVER_INFO *serverInfo;

} WORKER_DATA, *LPWORKER_DATA;

void Usage(const char *programName);
void PrintWindowsErrorCode(DWORD errorCode, const char *message);
void PrintWindowsError(const char *message);
void Log(const char *pMainMsg, DWORD errorCode, LPCLIENT_INFO clientInfo, LPSERVER_INFO pServerInfo);
void LogError(const char *pMainErrorMsg, DWORD errorCode, LPCLIENT_INFO clientInfo, LPSERVER_INFO pServerInfo);
void LogInfo(const char *pMainMsg, LPCLIENT_INFO clientInfo, LPSERVER_INFO pServerInfo);
INT CreateWorkerThreads();
DWORD WINAPI ServerWorkerThread(LPVOID completionPort);
void CloseClient(LPCLIENT_INFO clientInfo, LPSERVER_INFO pServerInfo);
LPCLIENT_INFO RegisterClient(LPSERVER_INFO pServerInfo, SOCKET clientSocket, LPSOCKADDR_IN clientSockaddr, int remoteLen);
INT GetNumClients(LPSERVER_INFO pServerInfo);
int CheckOverlappedSocketOperation(DWORD overlappedSocketOperation);
void ExitHandler(void);
void Cleanup();
BOOL WINAPI CtrlHandler(DWORD fdwCtrlType);

// global server info
LPSERVER_INFO serverInfo = NULL;

void Usage(const char *programName)
{
    printf("%s\nUsage: %s <port>\n", PROGRAM_VERSION, programName);
}

// log functions
void SPrintfWinErrorMsg(DWORD errorCode, LPSTR msgBuffer, size_t msgBufferSize)
{
    if (FormatMessage(
            FORMAT_MESSAGE_FROM_SYSTEM,
            NULL,
            errorCode,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            msgBuffer, (DWORD)msgBufferSize, NULL) == 0)
    {
        snprintf(msgBuffer, msgBufferSize, "Unknown error %ld", errorCode);
    }
}

void PrintWindowsErrorCode(DWORD errorCode, const char *pMainErrorMsg)
{
    LPSTR lpMsgBuffer = NULL;
    CHAR msgBuffer[64];
    SPrintfWinErrorMsg(errorCode, msgBuffer, sizeof(msgBuffer));
    fprintf(stderr, "%s: %s (%ld)\n", pMainErrorMsg, lpMsgBuffer, errorCode);
}

void PrintWindowsError(const char *pMainErrorMsg)
{
    PrintWindowsErrorCode(GetLastError(), pMainErrorMsg);
}

void Log(const char *pMainMsg, DWORD errorCode, LPCLIENT_INFO clientInfo, LPSERVER_INFO pServerInfo)
{

    char *empty = "";
    char *errorMsg = NULL;
    CHAR msgErrorBuffer[64];

    if (errorCode == NO_PERROR)
    {
        errorMsg = empty;
    }
    else
    {
        SPrintfWinErrorMsg(errorCode, msgErrorBuffer, sizeof(msgErrorBuffer));
        errorMsg = msgErrorBuffer;
    }

    printf("%s -> %s (%s). Current clients: %d\n", clientInfo->address_str, pMainMsg, errorMsg, GetNumClients(pServerInfo));
}

void LogError(const char *pMainErrorMsg, DWORD errorCode, LPCLIENT_INFO clientInfo, LPSERVER_INFO pServerInfo)
{
    Log(pMainErrorMsg, errorCode, clientInfo, pServerInfo);
}

void LogInfo(const char *pMainMsg, LPCLIENT_INFO clientInfo, LPSERVER_INFO pServerInfo)
{
    Log(pMainMsg, NO_PERROR, clientInfo, pServerInfo);
}

// socket functions helpers
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

// server functions

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
    inet_ntop(AF_INET, &clientInfo->clientAddr.sin_addr, clientInfo->address_str, INET6_ADDRSTRLEN);
    snprintf(clientInfo->address_str + strlen(clientInfo->address_str), 6, ":%d", ntohs(remoteClientAddrInfo->sin_port));
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
            LogInfo("Connection closed.", clientInfo, serverInfo);
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
                LogError("Error reading data. Closing connection.", dw, clientInfo, serverInfo);
                CloseClient(clientInfo, workerData->serverInfo);
            }
        }
    }

    return 0;
}

void Cleanup()
{
    if (serverInfo != NULL)
    {
        for (INT c = 0; c < MAX_CLIENTS; c++)
        {
            LPCLIENT_INFO clientInfo = serverInfo->clients[c];
            if (clientInfo)
            {
                CloseClient(clientInfo, serverInfo);
            }
        }
        DeleteCriticalSection(&serverInfo->criticalSection);
        CloseHandle(serverInfo->completionPort);
        closesocket(serverInfo->listenSocket);
        GlobalFree(serverInfo->clients);
        GlobalFree(serverInfo);
        WSACleanup();
    }
}

INT CreateWorkerThreads()
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
            PrintWindowsError("Error creating worker thread");
            GlobalFree(workerData);
        }
        else
        {
            workerData->threadHandle = threadHandle;
            workerData->serverInfo = serverInfo;
            workersCreated++;
        }
    }
    return workersCreated;
}

void ExitHandler(void)
{
    puts("Closing server...");
    //Cleanup();
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
        puts("Invalid port number");
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
        PrintWindowsErrorCode(wsaOpResult, "Error initializing Winsock");
        return EXIT_FAILURE;
    }

    // Creating completion port
    completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (completionPort == NULL)
    {
        PrintWindowsError("Error creating completion port");
        return EXIT_FAILURE;
    }

    // Create socket with overlapped I/O enabled.
    listenSocket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);

    if (listenSocket == INVALID_SOCKET)
    {
        PrintWindowsError("Error creating server socket");
        return EXIT_FAILURE;
    }

    serverInfo->completionPort = completionPort;
    serverInfo->criticalSection = criticalSection;
    serverInfo->nClients = 0;

    workersCreated = CreateWorkerThreads();

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
        PrintWindowsError("Error binding server socket");
        Cleanup();
        ExitProcess(1);
    }

    wsaOpResult = listen(listenSocket, SOMAXCONN);
    if (wsaOpResult == SOCKET_ERROR)
    {
        PrintWindowsError("Error listening on server socket");
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
            PrintWindowsError("Error accepting connection");
            continue;
        }
        if (GetNumClients(serverInfo) >= MAX_CLIENTS)
        {
            printf("Max clients exceeded. Refusing connection.\n");
            closesocket(acceptSocket);
            continue;
        }
        LPCLIENT_INFO clientInfo = RegisterClient(serverInfo, acceptSocket, &saRemote, remoteLen);

        if (CreateIoCompletionPort((HANDLE)acceptSocket, completionPort, (ULONG_PTR)clientInfo, 0) == NULL)
        {
            LogError("Error at assigning completion port. Closing connection.", GetLastError(), clientInfo, serverInfo);
            CloseClient(clientInfo, serverInfo);
            continue;
        }
        INT overlappedOpResult;
        DWORD wsaRecvFlags;
        DWORD recvBytes;

        LogInfo("Accepting connection", clientInfo, serverInfo);

        wsaRecvFlags = 0;
        wsaOpResult = WSARecv(clientInfo->socket, &(clientInfo->wsaBuf), 1, (LPDWORD)&recvBytes, (LPDWORD)&wsaRecvFlags, &(clientInfo->overlapped), NULL);
        overlappedOpResult = CheckOverlappedSocketOperation(wsaOpResult);
        if (overlappedOpResult != 0)
        {
            LogError("Error starting reading data. Closing connection.", overlappedOpResult, clientInfo, serverInfo);
            CloseClient(clientInfo, serverInfo);
        }
    }

    return exitStatus;
}
