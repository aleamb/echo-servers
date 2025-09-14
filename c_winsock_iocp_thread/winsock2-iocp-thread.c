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
#define MAX_DEBUG_MSG_SIZE_TO_PRINT 128
#define CLIENT_NO_ERROR 0
#define MAX_WORKERS 1

typedef struct
{
    SOCKET socket;
    SOCKADDR_IN clientAddr;

    OVERLAPPED overlapped;
    WSABUF wsaBuf;
    char *buffer;

} CLIENT_INFO, *LPCLIENT_INFO;

typedef struct
{
    DWORD nClients;
    CLIENT_INFO *clients;
    SOCKET listenSocket;
    CRITICAL_SECTION criticalSection;
    HANDLE completionPort;

} SERVER_INFO, *LPSERVER_INFO;

typedef struct 
{
    HANDLE threadHandle;
    SERVER_INFO *serverInfo;

} WORKER_DATA, *LPWORKER_DATA;


void Usage(const char*);
void PrintWindowsErrorCode(DWORD errorCode, const char *);
void PrintWindowsError(const char *);
DWORD WINAPI ServerWorkerThread(LPVOID);


void Usage(const char* programName) {
    printf("%s\nUsage: %s <port>\n", PROGRAM_VERSION, programName);
}

void PrintWindowsErrorCode(DWORD errorCode, const char *pMainErrorMsg)
{
    LPSTR lpMsgBuffer = NULL;

    if (FormatMessage(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
            NULL,
            errorCode,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
           (LPSTR)&lpMsgBuffer, 0, NULL) != 0)
    {
        fprintf(stderr, "%s: %s (%ld)\n", pMainErrorMsg, lpMsgBuffer, errorCode);
        LocalFree(lpMsgBuffer);
    }
    else
    {
        fprintf(stderr, "%s. Error code: %ld\n", pMainErrorMsg, errorCode);
    }
}

void PrintWindowsError(const char *pMainErrorMsg)
{
    PrintWindowsErrorCode(GetLastError(), pMainErrorMsg);
}


void CloseClient(LPCLIENT_INFO clientInfo, LPSERVER_INFO serverInfo)
{
    EnterCriticalSection(&serverInfo->criticalSection);
    serverInfo->nClients--;
    LeaveCriticalSection(&serverInfo->criticalSection);
    closesocket(clientInfo->socket);
    GlobalFree(clientInfo->buffer);
    GlobalFree(clientInfo);   
}

LPCLIENT_INFO RegisterClient(LPSERVER_INFO serverInfo, SOCKET clientSocket, LPSOCKADDR_IN remoteClientAddrInfo, int remoteLen)
{

    LPCLIENT_INFO clientInfo;
    EnterCriticalSection(&serverInfo->criticalSection);
    serverInfo->nClients++;
    LeaveCriticalSection(&serverInfo->criticalSection);
    
    clientInfo = (LPCLIENT_INFO)GlobalAlloc(GPTR, sizeof(CLIENT_INFO));
    clientInfo->socket = clientSocket;
    ZeroMemory(&(clientInfo->overlapped), sizeof(OVERLAPPED));
    ZeroMemory(&(clientInfo->buffer), sizeof(DATA_BUFSIZE));
    clientInfo->wsaBuf.len = DATA_BUFSIZE;
    clientInfo->wsaBuf.buf = clientInfo->buffer;

    CopyMemory(&clientInfo->clientAddr, remoteClientAddrInfo, remoteLen);

    return clientInfo;
}

int CheckOverlappedSocketOperation(DWORD wsaOpResult) {
    if (wsaOpResult == SOCKET_ERROR)
    {
        int lastError = WSAGetLastError();
        if (lastError != WSA_IO_PENDING)
        {
            PrintWindowsErrorCode(lastError, "Error in overlapped socket operation");
            return lastError;
        }
    }
    return CLIENT_NO_ERROR;
}


DWORD WINAPI ServerWorkerThread(LPVOID pParameter)
{
    DWORD bytesTransferred;
    LPCLIENT_INFO clientInfo;
    LPWORKER_DATA workerData;
    LPOVERLAPPED overlappedData;

    workerData = (LPWORKER_DATA)pParameter;

    while (GetQueuedCompletionStatus(workerData->serverInfo->completionPort,
                                     &bytesTransferred,
                                     (PULONG_PTR)&clientInfo,
                                     (LPOVERLAPPED *)&overlappedData,
                                     INFINITE))
    {

        if (bytesTransferred == 0)
        { 
            CloseClient(clientInfo, workerData->serverInfo);
        }
        else
        {
            DWORD flags;
            clientInfo->wsaBuf.len = bytesTransferred;
            WSASend(clientInfo->socket, &(clientInfo->wsaBuf), 1, NULL, 0, &(clientInfo->overlapped), NULL);

            ZeroMemory(&(clientInfo->overlapped), sizeof(OVERLAPPED));
            ZeroMemory(clientInfo->buffer, DATA_BUFSIZE);
            flags = 0;

            WSARecv(clientInfo->socket, &(clientInfo->wsaBuf), 1, NULL, &flags, &(clientInfo->overlapped), NULL);
        }
    }

    return 0;
}

void fn1()
{
   printf( "HOLA.\n" );
}

int main(int argc, char* argv[])
{
   
    WSADATA wsaData;
    SOCKADDR_IN internetAddr;
    INT port;
    SOCKET listenSocket;
    INT wsaOpResult;

    SYSTEM_INFO systemInfo;

    CRITICAL_SECTION criticalSection;
    HANDLE completionPort;

    SERVER_INFO *serverInfo;

    INT workersToCreate;
    INT workersCreated;
    INT exitStatus = EXIT_SUCCESS;

    atexit(fn1);

    puts(PROGRAM_VERSION);

    if (argc < 2)
    {
        Usage(argv[0]);
        return EXIT_FAILURE;
    }

    port = atoi(argv[1]);

    if (port <= 0) {
        puts("Invalid port number");
        return EXIT_FAILURE;
    }

    serverInfo = (SERVER_INFO *)GlobalAlloc(GPTR, sizeof(SERVER_INFO));

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
        WSACleanup();
        CloseHandle(completionPort);
        return EXIT_FAILURE;
    }

    // Create socket with overlapped I/O enabled.
    listenSocket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);

    if (listenSocket == INVALID_SOCKET)
    {
        PrintWindowsError("Error creating server socket");
        WSACleanup();
        CloseHandle(completionPort);
        return EXIT_FAILURE;
    }

     // Determine how many processors are on the system
    GetSystemInfo(&systemInfo);

    // Create the worker threads. One for each processor.
    workersToCreate = min(systemInfo.dwNumberOfProcessors, MAX_WORKERS);

    printf("Trying to create %d worker threads...\n", workersToCreate);

    workersCreated = 0;
    for (DWORD i = 0; i < workersToCreate; i++)
    {
        LPWORKER_DATA workerData = (LPWORKER_DATA)GlobalAlloc(GPTR, sizeof(WORKER_DATA));
        HANDLE threadHandle;

        threadHandle = CreateThread(NULL, 0, ServerWorkerThread, (LPVOID)workerData, 0, NULL);
        if (threadHandle == NULL)
        {
            PrintWindowsError("Error creating worker thread");
            GlobalFree(workerData);

        } else {
            workerData->threadHandle = threadHandle;
            workerData->serverInfo = serverInfo;
            workersCreated++;
        }
    }

    // if no worker threads were created, exit.
    if (!workersCreated) {
        fprintf(stderr, "No working threads created. Exiting.\n");
        WSACleanup();
        CloseHandle(completionPort);
        DeleteCriticalSection(&criticalSection);
        ExitProcess(1);
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
        closesocket(listenSocket);
        WSACleanup();
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
        }
        else
        {

            printf("Accepted connection from %s:%d\n", inet_ntoa(saRemote.sin_addr), ntohs(saRemote.sin_port));

            LPCLIENT_INFO clientInfo = RegisterClient(serverInfo, acceptSocket, &saRemote, remoteLen);
   
            if (CreateIoCompletionPort((HANDLE)acceptSocket, completionPort, (ULONG_PTR)clientInfo, 0) == NULL)
            {
                PrintWindowsError("Error at assigning completion port to socket");
                CloseClient(clientInfo, serverInfo);
            }
            else
            {
                DWORD overlappedOpResult;
                DWORD wsaRecvFlags;
                DWORD recvBytes;

                wsaRecvFlags = 0;
                wsaOpResult = WSARecv(clientInfo->socket, &(clientInfo->wsaBuf), 1, (LPDWORD)&recvBytes, (LPDWORD)&wsaRecvFlags, &(clientInfo->overlapped), NULL);
                
                overlappedOpResult = CheckOverlappedSocketOperation(wsaOpResult);

                if (overlappedOpResult != 0)
                {
                    CloseClient(clientInfo, serverInfo);
                    printf("Closing connection from %s:%d. Clients: %d\n", inet_ntoa(clientInfo->clientAddr.sin_addr), ntohs(clientInfo->clientAddr.sin_port), (int)serverInfo->nClients);
                }
                
            }
        }
    }

    closesocket(serverInfo->listenSocket);

    GlobalFree(serverInfo);
    WSACleanup();
    CloseHandle(completionPort);
    DeleteCriticalSection(&criticalSection);

    return exitStatus;
}
