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

#define DATA_BUFSIZE 1024
#define MAX_DEBUG_MSG_SIZE_TO_PRINT 512

typedef struct
{
    WORD nClients;

} CLIENTS_STATS, *LPCLIENTS_STATS;

typedef struct _PER_HANDLE_DATA
{
    SOCKET socket;
    SOCKADDR_IN clientAddr;
    CLIENTS_STATS *clientsStats;
    CHAR address_str[INET6_ADDRSTRLEN];
} PER_HANDLE_DATA, *LPPER_HANDLE_DATA;

typedef struct
{
    OVERLAPPED overlapped;
    char buffer[DATA_BUFSIZE];
    WSABUF wsaBuf;
} PER_IO_DATA, *LPPER_IO_DATA;


CRITICAL_SECTION CriticalSection;

DWORD WINAPI ServerWorkerThread(LPVOID);

void PrintWinError(DWORD, const char *, ...);
void WinErrorExit(DWORD, const char *, ...);
void NotifyClientClosed(LPPER_HANDLE_DATA);


int main(int argc, char* argv[])
{
    WSADATA wsaData;
    HANDLE completionPort;
    SYSTEM_INFO systemInfo;
    SOCKADDR_IN internetAddr;
    SOCKET listenSocket;
    CLIENTS_STATS *clientsStats;
    INT port;
    DWORD dw; // store error code

    if (argc < 2)
    {
        printf("Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    port = atoi(argv[1]);

    if (port <= 0) {
        puts("Invalid port number.\n");
        exit(2);
    }

    // This critical section is used to protect access to the clientsStats structure and avoid logs merging.
    if (!InitializeCriticalSectionAndSpinCount(&CriticalSection, 0x00000400)) {

        WinErrorExit(GetLastError(), "Error initializing critical section.\n");
    }

    // initialize Winsock
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0)
    {
        WinErrorExit(GetLastError(), "Error initializing Winsock.\n");
    }

    // creating completion port
    completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (completionPort == NULL)
    {
        dw = GetLastError();
        WSACleanup();
        CloseHandle(completionPort);
        WinErrorExit(dw, "Error creating completion port.\n");
    }

    // Create socket with overlapped I/O enabled.
    listenSocket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);

    if (listenSocket == INVALID_SOCKET)
    {
        dw = GetLastError();
        WSACleanup();
        CloseHandle(completionPort);
        WinErrorExit(dw, "Error creating server socket.\n");
    }

    // bind and set listening
    internetAddr.sin_family = AF_INET;
    internetAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    internetAddr.sin_port = htons((USHORT)port);

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
        WinErrorExit(dw, "Error binding server socket.\n");
    }

    result = listen(listenSocket, SOMAXCONN);
    if (result == SOCKET_ERROR)
    {
        dw = GetLastError();
        closesocket(listenSocket);
        WSACleanup();
        WinErrorExit(dw, "Error listening on server socket.\n");
    }

    printf("Server listening on port %d\n", port);

    // Determine how many processors are on the system
    GetSystemInfo(&systemInfo);

    // create the worker threads. One for each processor.
    for (DWORD i = 0; i < systemInfo.dwNumberOfProcessors; i++)
    {
        HANDLE threadHandle;
        threadHandle = CreateThread(NULL, 0, ServerWorkerThread, completionPort, 0, NULL);
        if (threadHandle == NULL)
        {
            dw = GetLastError();
            WSACleanup();
            CloseHandle(completionPort);
            WinErrorExit(dw, "Error creating worker thread %d\n", i);
        }
    }
    
    printf("Created %ld worker threads.\n", systemInfo.dwNumberOfProcessors);

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
            PrintWinError(GetLastError(), "Error accepting connection.\n");
        }
        else
        {
            DWORD wsaRecvFlags;
            LPPER_HANDLE_DATA handleData;
            DWORD recvBytes;
            LPPER_IO_DATA perIoData;

            handleData = (LPPER_HANDLE_DATA)GlobalAlloc(GPTR, sizeof(PER_HANDLE_DATA));
            handleData->socket = acceptSocket;
            handleData->clientsStats = clientsStats;
            CopyMemory(&handleData->clientAddr, &saRemote, remoteLen);
            inet_ntop(AF_INET, &saRemote.sin_addr, handleData->address_str, INET6_ADDRSTRLEN);

            if (CreateIoCompletionPort((HANDLE)acceptSocket, completionPort, (ULONG_PTR)handleData, 0) == NULL)
            {
                PrintWinError(GetLastError(), "Error assign completion port to socket.\n");
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

                printf("Accepting connection from %s:%d. Clients: %d\n", handleData->address_str, ntohs(saRemote.sin_port), (int)(++clientsStats->nClients));

                result = WSARecv(handleData->socket, &(perIoData->wsaBuf), 1, (LPDWORD)&recvBytes, (LPDWORD)&wsaRecvFlags, &(perIoData->overlapped), NULL);

                if (result == SOCKET_ERROR)
                {
                    dw = WSAGetLastError();
                    if (dw != WSA_IO_PENDING)
                    {
                        PrintWinError(dw, "Error assign completion port to socket.\n");
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
    DeleteCriticalSection(&CriticalSection);
    return 0;
}

DWORD WINAPI ServerWorkerThread(LPVOID pCompletionPortID)
{
    HANDLE completionPort = (HANDLE)pCompletionPortID;
    DWORD bytesTransferred;
    LPPER_IO_DATA perIoData;
    LPPER_HANDLE_DATA handleData;
    DWORD flags;

    #ifdef _DEBUG
    DWORD bytesToPrint;
    #endif

    while (GetQueuedCompletionStatus(completionPort,
                                     &bytesTransferred,
                                     (PULONG_PTR)&handleData,
                                     (LPOVERLAPPED *)&perIoData,
                                     INFINITE))
    {

        if (bytesTransferred == 0)
        {
            NotifyClientClosed(handleData);
            closesocket(handleData->socket);
            GlobalFree(handleData);
            GlobalFree(perIoData);
        }
        else
        {
            #ifdef _DEBUG
            bytesToPrint = bytesTransferred > MAX_DEBUG_MSG_SIZE_TO_PRINT ?  MAX_DEBUG_MSG_SIZE_TO_PRINT : bytesTransferred;
            perIoData->wsaBuf.buf[bytesToPrint] = '\0';
            printf("Received %d bytes from %s. Data received (truncated): %s", bytesTransferred, handleData->address_str, perIoData->wsaBuf.buf);
            puts(perIoData->wsaBuf.buf);
            fflush(stdout);
            #endif

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

void NotifyClientClosed(LPPER_HANDLE_DATA handleData) 
{
    EnterCriticalSection(&CriticalSection); 
    handleData->clientsStats->nClients--;
    printf("Closing connection from %s. Clients: %d\n", handleData->address_str, handleData->clientsStats->nClients);
    LeaveCriticalSection(&CriticalSection);   
}

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
        fprintf(stderr, "%s Error code: %d -> %s\n", pMainErrorMsg, (int)dw, (LPCTSTR)lpMsgBuf);
        LocalFree(lpMsgBuf);
    }
    else
    {
        fprintf(stderr, "%s Error code: %d\n", pMainErrorMsg, (int)dw);
    }
}

void PrintWinError(DWORD dw, const char *pMainErrorMsg, ...)
{
    va_list args;
    va_start(args, pMainErrorMsg);
    vPrintError(dw, pMainErrorMsg, args);
    va_end(args);
}

void WinErrorExit(DWORD dw, const char *pMainErrorMsg, ...)
{
    va_list args;
    va_start(args, pMainErrorMsg);
    vPrintError(dw, pMainErrorMsg, args);
    va_end(args);
    ExitProcess(dw);
}


