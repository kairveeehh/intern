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
#define MAX_SLOTS 4  // Maximum slots per server

struct Slot {
    int id;
    int busy;  // 0 for free, 1 for busy
};

struct Server {
    char ip[16];
    int port;
    int active;
    struct Slot slots[MAX_SLOTS];
    int total_slots;
};

struct Server servers[MAX_SERVERS];
int server_count = 0;
int current_server = 0;

void add_server(const char* ip, int port, int num_slots) {
    if (server_count < MAX_SERVERS) {
        strncpy(servers[server_count].ip, ip, sizeof(servers[server_count].ip) - 1);
        servers[server_count].ip[sizeof(servers[server_count].ip) - 1] = '\0';
        servers[server_count].port = port;
        servers[server_count].active = 1;
        servers[server_count].total_slots = (num_slots <= MAX_SLOTS) ? num_slots : MAX_SLOTS;
        for (int i = 0; i < servers[server_count].total_slots; i++) {
            servers[server_count].slots[i].id = i;
            servers[server_count].slots[i].busy = 0;
        }
        server_count++;
    }
}

int find_available_slot(int server_index) {
    for (int i = 0; i < servers[server_index].total_slots; i++) {
        if (!servers[server_index].slots[i].busy) {
            return i;
        }
    }
    return -1;  // No available slot
}

int get_next_server() {
    int initial = current_server;
    do {
        current_server = (current_server + 1) % server_count;
        if (servers[current_server].active && find_available_slot(current_server) != -1) {
            return current_server;
        }
    } while (current_server != initial);
    return -1;  // No available server with free slots
}

SOCKET create_socket(const char* ip, int port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    return sock;
}

int main(int argc, char *argv[]) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    if (argc < 5 || (argc % 3) != 2) {
        fprintf(stderr, "Usage: %s <load_balancer_port> <server1_ip> <server1_port> <server1_slots> [<server2_ip> <server2_port> <server2_slots> ...]\n", argv[0]);
        WSACleanup();
        exit(1);
    }

    int lb_port = atoi(argv[1]);

    for (int i = 2; i < argc; i += 3) {
        add_server(argv[i], atoi(argv[i+1]), atoi(argv[i+2]));
    }

    SOCKET server_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(lb_port);

    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        fprintf(stderr, "Bind failed with error: %d\n", WSAGetLastError());
        closesocket(server_sock);
        WSACleanup();
        return 1;
    }

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
        SOCKET client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);

        if (client_sock == INVALID_SOCKET) {
            fprintf(stderr, "Accept failed with error: %d\n", WSAGetLastError());
            continue;
        }

        int server_index = get_next_server();
        if (server_index == -1) {
            const char* error_msg = "No servers available";
const char* http_response_format = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n%s";
char http_response[BUFFER_SIZE];
snprintf(http_response, sizeof(http_response), http_response_format, strlen(error_msg), error_msg);
send(client_sock, http_response, strlen(http_response), 0);

            closesocket(client_sock);
            continue;
        }

        int slot_index = find_available_slot(server_index);
        if (slot_index == -1) {
            const char* error_msg = "No slots available";
            send(client_sock, error_msg, strlen(error_msg), 0);
            closesocket(client_sock);
            continue;
        }

        servers[server_index].slots[slot_index].busy = 1;

        SOCKET backend_sock = create_socket(servers[server_index].ip, servers[server_index].port);
        if (backend_sock == INVALID_SOCKET) {
            servers[server_index].active = 0;
            servers[server_index].slots[slot_index].busy = 0;
            const char* error_msg = "Failed to connect to server";
            send(client_sock, error_msg, strlen(error_msg), 0);
            closesocket(client_sock);
            continue;
        }

        char buffer[BUFFER_SIZE];
        int bytes_received;

        while ((bytes_received = recv(client_sock, buffer, BUFFER_SIZE, 0)) > 0) {
            send(backend_sock, buffer, bytes_received, 0);
            bytes_received = recv(backend_sock, buffer, BUFFER_SIZE, 0);
            if (bytes_received > 0) {
                send(client_sock, buffer, bytes_received, 0);
            } else {
                break;
            }
        }

        servers[server_index].slots[slot_index].busy = 0;
        closesocket(backend_sock);
        closesocket(client_sock);
    }

    closesocket(server_sock);
    WSACleanup();
    return 0;
}