#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

#define MAX_SERVERS 10
#define BUFFER_SIZE 1024

// Structure to hold information about each server
struct Server {
    char ip[16];   // IP address of the server
    int port;      // Port number of the server
    int active;    // Status of the server (1 for active, 0 for inactive)
};

// Array to store server information
struct Server servers[MAX_SERVERS];
int server_count = 0;  // Number of servers currently stored
int current_server = 0;  // Index of the current server to use

// Function to add a server to the list
void add_server(const char* ip, int port) {
    if (server_count < MAX_SERVERS) {
        // Copy IP address to the server structure
        strncpy(servers[server_count].ip, ip, sizeof(servers[server_count].ip) - 1);
        servers[server_count].ip[sizeof(servers[server_count].ip) - 1] = '\0';
        // Set port number
        servers[server_count].port = port;
        // Mark server as active
        servers[server_count].active = 1;
        server_count++;
    }
}

// Function to get the next available server in a round-robin fashion
int get_next_server() {
    int initial = current_server;
    do {
        // Move to the next server
        current_server = (current_server + 1) % server_count;
        if (servers[current_server].active) {
            return current_server;  // Return index of the active server
        }
    } while (current_server != initial);  // Continue until we have looped back to the start
    return -1;  // No available server
}

// Function to create a socket and connect to the specified server
SOCKET create_socket(const char* ip, int port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);  // Convert IP address from string to binary form

    // Attempt to connect to the server
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        closesocket(sock);  // Close socket on failure
        return INVALID_SOCKET;
    }
    return sock;
}

int main(int argc, char *argv[]) {
    WSADATA wsaData;
    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    // Check for correct usage
    if (argc < 4 || (argc % 2) != 0) {
        fprintf(stderr, "Usage: %s <load_balancer_port> <server1_ip> <server1_port> [<server2_ip> <server2_port> ...]\n", argv[0]);
        WSACleanup();
        exit(1);
    }

    int lb_port = atoi(argv[1]);  // Load balancer port number

    // Add servers to the list
    for (int i = 2; i < argc; i += 2) {
        add_server(argv[i], atoi(argv[i+1]));
    }

    // Create a socket for the load balancer
    SOCKET server_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;  // Accept connections from any IP address
    server_addr.sin_port = htons(lb_port);  // Set the load balancer port

    // Bind the socket to the specified port
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        fprintf(stderr, "Bind failed with error: %d\n", WSAGetLastError());
        closesocket(server_sock);
        WSACleanup();
        return 1;
    }

    // Listen for incoming connections
    if (listen(server_sock, 5) == SOCKET_ERROR) {
        fprintf(stderr, "Listen failed with error: %d\n", WSAGetLastError());
        closesocket(server_sock);
        WSACleanup();
        return 1;
    }

    printf("Load balancer listening on port %d\n", lb_port);

    while (1) {
        struct sockaddr_in client_addr;
        int client_len = sizeof(client_addr);
        // Accept a new client connection
        SOCKET client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);

        if (client_sock == INVALID_SOCKET) {
            fprintf(stderr, "Accept failed with error: %d\n", WSAGetLastError());
            continue;
        }

        // Get the index of the next available server
        int server_index = get_next_server();
        if (server_index == -1) {
            const char* error_msg = "No servers available";
            send(client_sock, error_msg, strlen(error_msg), 0);
            closesocket(client_sock);
            continue;
        }

        // Create a socket to connect to the selected backend server
        SOCKET backend_sock = create_socket(servers[server_index].ip, servers[server_index].port);
        if (backend_sock == INVALID_SOCKET) {
            servers[server_index].active = 0;  // Mark server as inactive if connection fails
            const char* error_msg = "Failed to connect to server";
            send(client_sock, error_msg, strlen(error_msg), 0);
            closesocket(client_sock);
            continue;
        }

        char buffer[BUFFER_SIZE];
        int bytes_received;

        // Forward data between the client and backend server
        while ((bytes_received = recv(client_sock, buffer, BUFFER_SIZE, 0)) > 0) {
            send(backend_sock, buffer, bytes_received, 0);
            bytes_received = recv(backend_sock, buffer, BUFFER_SIZE, 0);
            if (bytes_received > 0) {
                send(client_sock, buffer, bytes_received, 0);
            } else {
                break;
            }
        }

        // Close sockets after data transfer is complete
        closesocket(backend_sock);
        closesocket(client_sock);
    }

    // Clean up and close the main server socket
    closesocket(server_sock);
    WSACleanup();
    return 0;
}
