
#include <stdio.h>
#include <winsock2.h>

#define PORT 7777
#define DATA_BUFSIZE 1024

#pragma comment(lib, "ws2_32.lib")

typedef struct
{
    DWORD nClients;

} CLIENTS_STATS, *LPCLIENTS_STATS;

typedef struct _PER_HANDLE_DATA
{
    SOCKET socket;
    SOCKADDR_IN clientAddr;
    CLIENTS_STATS *clientsStats;
} PER_HANDLE_DATA, *LPPER_HANDLE_DATA;

typedef struct
{
    OVERLAPPED overlapped;
    char buffer[DATA_BUFSIZE];
    WSABUF wsaBuf;
} PER_IO_DATA, *LPPER_IO_DATA;

DWORD WINAPI ServerWorkerThread(LPVOID);

void PrintError(DWORD, const char *, ...);
void ErrorExit(DWORD, const char *, ...);

int main()
{
    WSADATA wsaData;
    HANDLE completionPort;
    SYSTEM_INFO systemInfo;
    SOCKADDR_IN internetAddr;
    SOCKET listenSocket;
    CLIENTS_STATS *clientsStats;
    DWORD dw;

    // initialize Winsock
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0)
    {
        ErrorExit(GetLastError(), "Error initializing Winsock");
    }

    // creating completion port
    completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (completionPort == NULL)
    {
        dw = GetLastError();
        WSACleanup();
        CloseHandle(completionPort);
        ErrorExit(dw, "Error creating completion port");
    }

    // Create socket with overlapped I/O enabled.
    listenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);

    if (listenSocket == INVALID_SOCKET)
    {
        dw = GetLastError();
        WSACleanup();
        CloseHandle(completionPort);
        ErrorExit(dw, "Error creating server socket.");
    }

    // bind and set listening
    internetAddr.sin_family = AF_INET;
    internetAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    internetAddr.sin_port = htons(PORT);

    // set SO_REUSEADDR option to allow reuse of the address
    BOOL bOptVal = TRUE;
    int bOptLen = sizeof(BOOL);
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (char *)&bOptVal, bOptLen);

    result = bind(listenSocket, (SOCKADDR *)&internetAddr, sizeof(SOCKADDR));
    if (result == SOCKET_ERROR)
    {
        dw = GetLastError();
        closesocket(listenSocket);
        WSACleanup();
        ErrorExit(dw, "Error binding server socket.");
    }

    result = listen(listenSocket, SOMAXCONN);
    if (result == SOCKET_ERROR)
    {
        dw = GetLastError();
        closesocket(listenSocket);
        WSACleanup();
        ErrorExit(dw, "Error listening on server socket.");
    }

    // Determine how many processors are on the system
    GetSystemInfo(&systemInfo);

    // create the worker threads. One for each processor.
    for (int i = 0; i < systemInfo.dwNumberOfProcessors; i++)
    {
        HANDLE threadHandle;
        threadHandle = CreateThread(NULL, 0, ServerWorkerThread, completionPort, 0, NULL);
        if (threadHandle == NULL)
        {
            dw = GetLastError();
            WSACleanup();
            CloseHandle(completionPort);
            ErrorExit(dw, "Error creating worker thread %d", i);
        }
    }

    // stats
    clientsStats = (CLIENTS_STATS *)GlobalAlloc(GPTR, sizeof(CLIENTS_STATS));
    clientsStats->nClients = 0;

    while (TRUE)
    {
        SOCKADDR_IN saRemote;
        SOCKET acceptSocket;
        int remoteLen = sizeof(saRemote);

        acceptSocket = WSAAccept(listenSocket, (SOCKADDR *)&saRemote, &remoteLen, NULL, (DWORD_PTR)NULL);

        if (acceptSocket == INVALID_SOCKET)
        {
            PrintError(GetLastError(), "Error accepting connection.");
        }
        else
        {
            DWORD wsaRecvFlags;
            LPPER_HANDLE_DATA handleData;
            DWORD recvBytes;
            LPPER_IO_DATA perIoData;

            printf("Accepting connection %d from %s:%d\n", (int)++clientsStats->nClients,
                   inet_ntoa(saRemote.sin_addr), ntohs(saRemote.sin_port));

            handleData = (LPPER_HANDLE_DATA)GlobalAlloc(GPTR, sizeof(PER_HANDLE_DATA));
            handleData->socket = acceptSocket;
            handleData->clientsStats = clientsStats;
            CopyMemory(&handleData->clientAddr, &saRemote, remoteLen);

            if (CreateIoCompletionPort((HANDLE)acceptSocket, completionPort, (ULONG_PTR)handleData, 0) == NULL)
            {
                PrintError(GetLastError(), "Error assign completion port to socket.");
                closesocket(acceptSocket);
                GlobalFree(handleData);
            }
            else
            {
                perIoData = (LPPER_IO_DATA)GlobalAlloc(GPTR, sizeof(PER_IO_DATA));
                ZeroMemory(&(perIoData->overlapped), sizeof(OVERLAPPED));
                perIoData->wsaBuf.len = DATA_BUFSIZE;
                perIoData->wsaBuf.buf = perIoData->buffer;
                wsaRecvFlags = 0;

                result = WSARecv(handleData->socket, &(perIoData->wsaBuf), 1, (LPDWORD)&recvBytes, (LPDWORD)&wsaRecvFlags, &(perIoData->overlapped), NULL);

                if (result == SOCKET_ERROR)
                {
                    dw = WSAGetLastError();
                    if (dw != WSA_IO_PENDING)
                    {
                        PrintError(dw, "Error assign completion port to socket.");
                        closesocket(acceptSocket);
                        GlobalFree(handleData);
                        GlobalFree(perIoData);
                    }
                }
            }
        }
    }

    closesocket(listenSocket);
    GlobalFree(clientsStats);
    WSACleanup();
    return 0;
}

DWORD WINAPI ServerWorkerThread(LPVOID pCompletionPortID)
{
    HANDLE completionPort = (HANDLE)pCompletionPortID;
    DWORD bytesTransferred;
    LPPER_IO_DATA perIoData;
    LPPER_HANDLE_DATA handleData;
    DWORD flags;

    while (GetQueuedCompletionStatus(completionPort,
                                     &bytesTransferred,
                                     (PULONG_PTR)&handleData,
                                     (LPOVERLAPPED *)&perIoData,
                                     INFINITE))
    {

        if (bytesTransferred == 0)
        {
            printf("Closing connection %d from %s:%d\n", (int)handleData->clientsStats->nClients,
                   inet_ntoa(handleData->clientAddr.sin_addr), ntohs(handleData->clientAddr.sin_port));
            closesocket(handleData->socket);
            handleData->clientsStats->nClients--;
            GlobalFree(handleData);
            GlobalFree(perIoData);
        }
        else
        {

            perIoData->wsaBuf.buf[bytesTransferred] = '\0';

            WSASend(handleData->socket, &(perIoData->wsaBuf), 1, NULL, 0, NULL, NULL);

            ZeroMemory(&(perIoData->overlapped), sizeof(OVERLAPPED));
            ZeroMemory(perIoData->buffer, DATA_BUFSIZE);
            perIoData->wsaBuf.len = DATA_BUFSIZE;
            perIoData->wsaBuf.buf = perIoData->buffer;
            flags = 0;

            WSARecv(handleData->socket, &(perIoData->wsaBuf), 1, NULL, &flags, &(perIoData->overlapped), NULL);
        }
    }

    return 0;
}

//

void vPrintError(DWORD dw, const char *pMainErrorMsg, va_list args)
{
    LPVOID lpMsgBuf;

    vfprintf(stderr, pMainErrorMsg, args);

    if (FormatMessage(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
            NULL,
            dw,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPTSTR)&lpMsgBuf, 0, NULL) != 0)
    {
        fprintf(stderr, "%s (Error code: %d) -> %s", pMainErrorMsg, (int)dw, (LPCTSTR)lpMsgBuf);
        LocalFree(lpMsgBuf);
    }
    else
    {
        fprintf(stderr, "%s Error code: %d\n", pMainErrorMsg, (int)dw);
    }
}

void PrintError(DWORD dw, const char *pMainErrorMsg, ...)
{
    va_list args;
    va_start(args, pMainErrorMsg);
    vPrintError(dw, pMainErrorMsg, args);
    va_end(args);
}

void ErrorExit(DWORD dw, const char *pMainErrorMsg, ...)
{
    va_list args;
    va_start(args, pMainErrorMsg);
    vPrintError(dw, pMainErrorMsg, args);
    va_end(args);
    ExitProcess(dw);
}
