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

#define PROGRAM_NAME "winsock2-iocp-thread"
#define DATA_BUFSIZE 1024
#define MAX_DEBUG_MSG_SIZE_TO_PRINT 128


typedef struct
{
    WORD nClients;

} CLIENTS_STATS, *LPCLIENTS_STATS;

typedef struct _PER_HANDLE_DATA
{
    SOCKET socket;
    USHORT port;
    SOCKADDR_IN clientAddr;
    CLIENTS_STATS *clientsStats;
    CHAR address_str[INET6_ADDRSTRLEN];
    
} PER_HANDLE_DATA, *LPPER_HANDLE_DATA;

typedef struct
{
    OVERLAPPED overlapped;
    WSABUF wsaBuf;
    char buffer[DATA_BUFSIZE];
} PER_IO_DATA, *LPPER_IO_DATA;


CRITICAL_SECTION CriticalSection;

void Usage();
void PrintWindowsErrorCode(DWORD errorCode, const char *);
void PrintWindowsError(const char *);
DWORD WINAPI ServerWorkerThread(LPVOID);


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
        Usage();
        ExitProcess(1);
    }

    port = atoi(argv[1]);

    if (port <= 0) {
        puts("Invalid port number");
        ExitProcess(1);
    }

    // This critical section is used to protect access to the clientsStats structure and avoid to merge log messages.
    InitializeCriticalSection(&CriticalSection);

    // initialize Winsock
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0)
    {
        PrintWindowsErrorCode(result, "Error initializing Winsock");
        ExitProcess(1);
    }

    // creating completion port
    completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (completionPort == NULL)
    {
        PrintWindowsError("Error creating completion port");
        WSACleanup();
        CloseHandle(completionPort);
        ExitProcess(1);
    }

    // Create socket with overlapped I/O enabled.
    listenSocket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);

    if (listenSocket == INVALID_SOCKET)
    {
        PrintWindowsError("Error creating server socket");
        WSACleanup();
        CloseHandle(completionPort);
        ExitProcess(1);
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
        PrintWindowsError("Error binding server socket");
        closesocket(listenSocket);
        WSACleanup();
        ExitProcess(1);
    }

    result = listen(listenSocket, SOMAXCONN);
    if (result == SOCKET_ERROR)
    {
        PrintWindowsError("Error listening on server socket");
        closesocket(listenSocket);
        WSACleanup();    
        ExitProcess(1);
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
            PrintWindowsError("Error creating worker thread");
            WSACleanup();
            CloseHandle(completionPort);
            ExitProcess(1);
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
            PrintWindowsError("Error accepting connection");
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

            printf("Accepting connection from %s:%d. Clients: %d\n", handleData->address_str, ntohs(saRemote.sin_port), (int)(++clientsStats->nClients));

            if (CreateIoCompletionPort((HANDLE)acceptSocket, completionPort, (ULONG_PTR)handleData, 0) == NULL)
            {
                PrintWindowsError("Error assign completion port to socket");
                closesocket(acceptSocket);
                GlobalFree(handleData);
            }
            else
            {
                perIoData = (LPPER_IO_DATA)GlobalAlloc(GPTR, sizeof(PER_IO_DATA));
                ZeroMemory(&(perIoData->overlapped), sizeof(OVERLAPPED));
                ZeroMemory(&(perIoData->buffer), sizeof(DATA_BUFSIZE));
                perIoData->wsaBuf.len = DATA_BUFSIZE;
                perIoData->wsaBuf.buf = perIoData->buffer;
                wsaRecvFlags = 0;
                
                result = WSARecv(handleData->socket, &(perIoData->wsaBuf), 1, (LPDWORD)&recvBytes, (LPDWORD)&wsaRecvFlags, &(perIoData->overlapped), NULL);

                if (result == SOCKET_ERROR)
                {
                    dw = WSAGetLastError();
                    if (dw != WSA_IO_PENDING)
                    {
                        fprintf(stderr, "Error preparing receiving of data from %s. Error: %d. Closing socket.\n", handleData->address_str, dw);
                        closesocket(handleData->socket);
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
            DWORD bytesToPrint;
            char tmp_byte;
            bytesToPrint = bytesTransferred > MAX_DEBUG_MSG_SIZE_TO_PRINT ?  MAX_DEBUG_MSG_SIZE_TO_PRINT : bytesTransferred;
            tmp_byte = perIoData->wsaBuf.buf[bytesToPrint];
            perIoData->wsaBuf.buf[bytesToPrint] = '\0';
            printf("Received %d bytes from %s. Data received (truncated): %s\n", bytesTransferred, handleData->address_str, perIoData->wsaBuf.buf);
            fflush(stdout);
            perIoData->wsaBuf.buf[bytesToPrint] = tmp_byte;
            #endif

            perIoData->wsaBuf.len = bytesTransferred;
            WSASend(handleData->socket, &(perIoData->wsaBuf), 1, NULL, 0, NULL, NULL);

            ZeroMemory(&(perIoData->overlapped), sizeof(OVERLAPPED));
            ZeroMemory(perIoData->buffer, DATA_BUFSIZE);
            flags = 0;

            WSARecv(handleData->socket, &(perIoData->wsaBuf), 1, NULL, &flags, &(perIoData->overlapped), NULL);
        }
    }

    return 0;
}

//

void Usage() {
    printf("Usage: %s <port>\n", PROGRAM_NAME);
}

void NotifyClientClosed(LPPER_HANDLE_DATA handleData) 
{
    EnterCriticalSection(&CriticalSection);

    handleData->clientsStats->nClients--;
    printf("Closing connection from %s. Clients: %d\n", handleData->address_str, handleData->clientsStats->nClients);

    LeaveCriticalSection(&CriticalSection);   
}

void PrintWindowsError(const char *pMainErrorMsg)
{
    PrintWindowsErrorCode(GetLastError(), pMainErrorMsg);
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
