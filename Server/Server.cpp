#undef UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>

#pragma comment(lib, "Ws2_32.lib")

#define DEFAULT_BUFLEN 512
#define FILE_BUFLEN 1024
#define DEFAULT_PORT "27015"

// Function to send a file to the client
int sendFile(SOCKET clientSocket, const char* fileName) {
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
        printf("Error opening file: %s\n", fullPath);
        return -1;
    }

    char fileBuf[FILE_BUFLEN];
    int bytesRead, iResult;

    do {
        bytesRead = fread(fileBuf, 1, sizeof(fileBuf), file);
        if (bytesRead > 0) {
            iResult = send(clientSocket, fileBuf, bytesRead, 0);
            if (iResult == SOCKET_ERROR) {
                printf("Error sending file to the client: %d\n", WSAGetLastError());
                fclose(file);
                return -1;
            }
        }
    } while (bytesRead > 0);

    fclose(file);
    return 0;
}

int main(void) {
    WSADATA wsaData;
    int iResult;

    SOCKET ListenSocket = INVALID_SOCKET;
    SOCKET ClientSocket = INVALID_SOCKET;

    struct addrinfo* result = NULL;
    struct addrinfo hints;

    int iSendResult;
    char recvbuf[DEFAULT_BUFLEN];
    int recvbuflen = DEFAULT_BUFLEN;

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

        // Print a message indicating that a client has connected
        printf("Client has connected to the server.\n");

        // Receive until the peer shuts down the connection
        do {
            // Receive the file name from the client
            iResult = recv(ClientSocket, recvbuf, recvbuflen, 0);
            if (iResult > 0) {
                recvbuf[iResult] = '\0';
                printf("Bytes received: %d\n", iResult);
                printf("Requested file: %s\n", recvbuf);

                // Attempt to send the requested file
                printf("Attempting to send file...\n");
                if (sendFile(ClientSocket, recvbuf) == 0) {
                    printf("File sent successfully.\n");
                }
                else {
                    printf("Error sending file.\n");
                    break;  // Exit the loop on send error
                }
            }
            else if (iResult == 0) {
                printf("Connection closing...\n");
                break;  // Exit the loop on connection closing
            }
            else {
                int error = WSAGetLastError();
                printf("recv failed with error: %d\n", error);

                if (error == WSAECONNRESET) {
                    printf("Client disconnected unexpectedly.\n");
                }
                else {
                    printf("Error details: %d\n", error);
                }
                break;  // Exit the loop on recv error
            }

        } while (true);

        // cleanup for the current client
        closesocket(ClientSocket);
        printf("Client disconnected.\n");

        // Ask if the server should continue to listen for more clients
        printf("Do you want to continue listening for more clients? (yes/no): ");
        char userChoice[10];
        if (fgets(userChoice, sizeof(userChoice), stdin) == NULL) {
            printf("Error reading user input\n");
            break;  // Exit the loop if input fails
        }

        // Remove the newline character from the user choice
        size_t len = strlen(userChoice);
        if (len > 0 && userChoice[len - 1] == '\n') {
            userChoice[len - 1] = '\0';
        }

        if (strcmp(userChoice, "no") == 0) {
            break;  // Exit the loop if the user chooses 'no'
        }
        else if (strcmp(userChoice, "yes") == 0) {
            printf("Server is ready and waiting for a client to connect...\n"); // Continue the loop if user chooses 'yes'
        }
    }

    // cleanup
    closesocket(ListenSocket);
    WSACleanup();

    return 0;
}

