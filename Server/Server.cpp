// Undefine UNICODE and define WIN32_LEAN_AND_MEAN to exclude unnecessary headers
#undef UNICODE
#define WIN32_LEAN_AND_MEAN

// Include necessary headers
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <process.h>  // For _beginthreadex
#include <string>
#include <atomic>
#include <thread>

// Link with the Winsock library
#pragma comment(lib, "Ws2_32.lib")

// Define constants
#define DEFAULT_BUFLEN 512
#define FILE_BUFLEN 1024
#define DEFAULT_PORT "27015"

// Atomic variable to keep track of the number of connected clients
std::atomic_int clientCount(0);

// Function to print errors
void printError(const char* action) {
    printf("%s failed with error: %d\n", action, WSAGetLastError());
}

// Function to send an error message to the client
void sendErrorMessage(SOCKET clientSocket, const std::string& clientName, const std::string& errorMessage) {
    // Send the error message to the client
    int iResult = send(clientSocket, errorMessage.c_str(), static_cast<int>(errorMessage.length()), 0);
    if (iResult == SOCKET_ERROR) {
        // Print an error message if sending fails
        printf("%s: Error sending error message to the client: %d\n", clientName.c_str(), WSAGetLastError());
    }
}

// Function to send a file to the client
int sendFile(SOCKET clientSocket, const char* fileName, const std::string& clientName) {
    // Get the full path of the file
    char fullPath[MAX_PATH];
    if (GetModuleFileNameA(NULL, fullPath, MAX_PATH) == 0) {
        printf("Error getting module filename\n");
        return -1;
    }

    char* lastBackslash = strrchr(fullPath, '\\');
    if (lastBackslash != NULL) {
        *(lastBackslash + 1) = '\0';
    }

    strcat_s(fullPath, MAX_PATH, fileName);

    // Open the file for reading
    FILE* file;
    if (fopen_s(&file, fullPath, "rb") != 0) {
        // If the file is not found, send an error message to the client
        std::string errorMessage = "Error: File not found - " + std::string(fileName);
        sendErrorMessage(clientSocket, clientName, errorMessage);
        return -1;
    }

    char fileBuf[FILE_BUFLEN];
    int bytesRead, iResult;

    // Read and send the file in chunks
    do {
        bytesRead = fread(fileBuf, 1, sizeof(fileBuf), file);
        if (bytesRead > 0) {
            // Send the file data to the client
            iResult = send(clientSocket, fileBuf, bytesRead, 0);
            if (iResult == SOCKET_ERROR) {
                // Print an error message if sending fails
                printf("%s: Error sending file to the client: %d\n", clientName.c_str(), WSAGetLastError());
                fclose(file);
                return -1;
            }
        }
    } while (bytesRead > 0);

    // Close the file
    fclose(file);
    return 0;
}

// Function to handle each client in a separate thread
void clientThread(SOCKET clientSocket, int currentClient) {
    // Generate a unique name for the client using the client count
    std::string clientName = "Client_" + std::to_string(currentClient);
    printf("%s: Connected to the server.\n", clientName.c_str());

    char recvbuf[DEFAULT_BUFLEN];
    int recvbuflen = DEFAULT_BUFLEN;
    int iResult;

    // Main loop to receive file requests from the client
    do {
        // Receive data from the client
        iResult = recv(clientSocket, recvbuf, recvbuflen, 0);
        if (iResult > 0) {
            // Null-terminate the received data
            recvbuf[iResult] = '\0';
            printf("%s: Bytes received: %d\n", clientName.c_str(), iResult);
            printf("%s: Requested file: %s\n", clientName.c_str(), recvbuf);

            printf("%s: Attempting to send file...\n", clientName.c_str());
            // Attempt to send the requested file to the client
            if (sendFile(clientSocket, recvbuf, clientName) == 0) {
                printf("%s: File sent successfully.\n", clientName.c_str());
            }
            else {
                printf("%s: Error sending file.\n", clientName.c_str());
            }
        }
        else {
            // Handle client disconnection
            printf("%s: Client disconnected unexpectedly.\n", clientName.c_str());
            break;
        }
    } while (true);

    // Close the client socket
    closesocket(clientSocket);
    printf("%s: Disconnected.\n", clientName.c_str());
}

// Main function
int main(void) {
    // Initialize Winsock
    WSADATA wsaData;
    int iResult;

    // Socket variables
    SOCKET ListenSocket = INVALID_SOCKET;
    SOCKET ClientSocket = INVALID_SOCKET;

    // Address info variables
    struct addrinfo* result = NULL;
    struct addrinfo hints;

    // Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        printError("WSAStartup");
        return 1;
    }

    // Configure address info hints for the server
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    // Resolve the local address and port to be used by the server
    iResult = getaddrinfo(NULL, DEFAULT_PORT, &hints, &result);
    if (iResult != 0) {
        printError("getaddrinfo");
        WSACleanup();
        return 1;
    }

    // Create a socket for the server
    ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (ListenSocket == INVALID_SOCKET) {
        printError("socket");
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

    // Bind the socket
    iResult = bind(ListenSocket, result->ai_addr, static_cast<int>(result->ai_addrlen));
    if (iResult == SOCKET_ERROR) {
        printError("bind");
        freeaddrinfo(result);
        closesocket(ListenSocket);
        WSACleanup();
        return 1;
    }

    // Free the address info
    freeaddrinfo(result);

    // Set the socket to listen for incoming connections
    iResult = listen(ListenSocket, SOMAXCONN);
    if (iResult == SOCKET_ERROR) {
        printError("listen");
        closesocket(ListenSocket);
        WSACleanup();
        return 1;
    }

    // Print a message indicating the server is ready and waiting
    printf("Server is ready and waiting for a client to connect...\n");

    // Main server loop to accept client connections
    while (true) {
        // Accept a new client connection
        ClientSocket = accept(ListenSocket, NULL, NULL);
        if (ClientSocket == INVALID_SOCKET) {
            // Print an error message if accepting a connection fails
            printError("accept");
            closesocket(ListenSocket);
            WSACleanup();
            return 1;
        }

        // Create a new thread to handle the client and detach it
        std::thread(clientThread, ClientSocket, clientCount.fetch_add(1)).detach();
    }

    // Close the listening socket and clean up Winsock
    closesocket(ListenSocket);
    WSACleanup();

    return 0;
}
