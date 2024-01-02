#undef UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <process.h>
#include <string>
#include <atomic>

#pragma comment(lib, "Ws2_32.lib")

#define DEFAULT_BUFLEN 512
#define FILE_BUFLEN 1024
#define DEFAULT_PORT "27015"

// Counter for numbering clients
std::atomic_int clientCount(0);

// Function to send an error message to the client
void sendErrorMessage(SOCKET clientSocket, const std::string& clientName, const std::string& errorMessage) {
    int iResult = send(clientSocket, errorMessage.c_str(), static_cast<int>(errorMessage.length()), 0);
    if (iResult == SOCKET_ERROR) {
        printf("%s: Error sending error message to the client: %d\n", clientName.c_str(), WSAGetLastError());
    }
}

// Function to send a file to the client
int sendFile(SOCKET clientSocket, const char* fileName, const std::string& clientName) {
    // Construct the full path for the file based on the current executable's location
    char fullPath[MAX_PATH];
    if (GetModuleFileNameA(NULL, fullPath, MAX_PATH) == 0) {
        printf("Error getting module filename\n");
        return -1;
    }

    // Remove the filename from the full path
    char* lastBackslash = strrchr(fullPath, '\\');
    if (lastBackslash != NULL) {
        *(lastBackslash + 1) = '\0';  // Null-terminate after the last backslash
    }

    // Append the filename to the path
    strcat_s(fullPath, MAX_PATH, fileName);

    FILE* file;
    if (fopen_s(&file, fullPath, "rb") != 0) {
        // Send an error message to the client if the file does not exist
        std::string errorMessage = "File not found: " + std::string(fileName);
        sendErrorMessage(clientSocket, clientName, errorMessage);
        return -1;
    }

    char fileBuf[FILE_BUFLEN];
    int bytesRead, iResult;

    do {
        bytesRead = fread(fileBuf, 1, sizeof(fileBuf), file);
        if (bytesRead > 0) {
            iResult = send(clientSocket, fileBuf, bytesRead, 0);
            if (iResult == SOCKET_ERROR) {
                printf("%s: Error sending file to the client: %d\n", clientName.c_str(), WSAGetLastError());
                fclose(file);
                return -1;
            }
        }
    } while (bytesRead > 0);

    fclose(file);
    return 0;
}

// Function to handle each client in a separate thread
void clientThread(SOCKET clientSocket) {
    int currentClient = clientCount.fetch_add(1); // Atomically increment and get the current value

    // Naming the client based on the connection order
    std::string clientName = "Client_" + std::to_string(currentClient);

    // Print a message indicating that a client has connected
    printf("%s: Connected to the server.\n", clientName.c_str());

    char recvbuf[DEFAULT_BUFLEN];
    int recvbuflen = DEFAULT_BUFLEN;
    int iResult;

    // Receive until the peer shuts down the connection
    do {
        // Receive the file name from the client
        iResult = recv(clientSocket, recvbuf, recvbuflen, 0);
        if (iResult > 0) {
            recvbuf[iResult] = '\0';
            printf("%s: Bytes received: %d\n", clientName.c_str(), iResult);
            printf("%s: Requested file: %s\n", clientName.c_str(), recvbuf);

            // Attempt to send the requested file
            printf("%s: Attempting to send file...\n", clientName.c_str());
            if (sendFile(clientSocket, recvbuf, clientName) == 0) {
                printf("%s: File sent successfully.\n", clientName.c_str());
            }
            else {
                printf("%s: Error sending file.\n", clientName.c_str());
                // No need to break here, continue handling the next request
            }
        }
        else {
            // Handle other cases (connection closing, recv error)...

            // If no data received, assume the client disconnected
            printf("%s: Client disconnected unexpectedly.\n", clientName.c_str());
            break;  // Exit the loop on recv error
        }

    } while (true);

    // cleanup for the current client
    closesocket(clientSocket);
    printf("%s: Disconnected.\n", clientName.c_str());
}

int main(void) {
    WSADATA wsaData;
    int iResult;

    SOCKET ListenSocket = INVALID_SOCKET;
    SOCKET ClientSocket = INVALID_SOCKET;

    struct addrinfo* result = NULL;
    struct addrinfo hints;

    // Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        printf("WSAStartup failed with error: %d\n", iResult);
        return 1;
    }

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    // Resolve the server address and port
    iResult = getaddrinfo(NULL, DEFAULT_PORT, &hints, &result);
    if (iResult != 0) {
        printf("getaddrinfo failed with error: %d\n", iResult);
        WSACleanup();
        return 1;
    }

    // Create a SOCKET for the server to listen for client connections.
    ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (ListenSocket == INVALID_SOCKET) {
        printf("socket failed with error: %ld\n", WSAGetLastError());
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

    // Setup the TCP listening socket
    iResult = bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen);
    if (iResult == SOCKET_ERROR) {
        printf("bind failed with error: %d\n", WSAGetLastError());
        freeaddrinfo(result);
        closesocket(ListenSocket);
        WSACleanup();
        return 1;
    }

    freeaddrinfo(result);

    iResult = listen(ListenSocket, SOMAXCONN);
    if (iResult == SOCKET_ERROR) {
        printf("listen failed with error: %d\n", WSAGetLastError());
        closesocket(ListenSocket);
        WSACleanup();
        return 1;
    }

    // Indicate that the server is now waiting for a client to connect
    printf("Server is ready and waiting for a client to connect...\n");

    // Accept multiple client connections
    while (true) {
        // Accept a client socket
        ClientSocket = accept(ListenSocket, NULL, NULL);
        if (ClientSocket == INVALID_SOCKET) {
            printf("accept failed with error: %d\n", WSAGetLastError());
            closesocket(ListenSocket);
            WSACleanup();
            return 1;
        }

        // Start a new thread to handle the client
        uintptr_t threadID;
        _beginthreadex(NULL, 0, (_beginthreadex_proc_type)clientThread, (void*)ClientSocket, 0, (unsigned*)&threadID);
    }

    // cleanup
    closesocket(ListenSocket);
    WSACleanup();

    return 0;
}
