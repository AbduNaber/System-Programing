// Compile: gcc chatserver.c -o chatserver
#include "chatDefination.h"



void *handle_client(void *arg) ;

int client_sockets[MAX_CLIENTS];
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;


int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: ./%s <port>\n", argv[0]);
        exit(1);
    }
    

    int port = atoi(argv[1]);
    int server_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len;
    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    // Setup server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Bind
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(1);
    }

    // Listen
    if (listen(server_fd, 1) < 0) {
        perror("Listen failed");
        exit(1);
    }
    printf("Server listening on address: %s, port %d...\n", inet_ntoa(server_addr.sin_addr), port);


    int client_count = 0;

    while (1) {
        addr_len = sizeof(client_addr);
        client_sockets[client_count] = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_sockets[client_count] < 0) {
            perror("Accept failed");
            exit(1);
        }
        printf("Client connected.\n");

        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, (void *)&client_sockets[client_count]);
        client_count++;
    }

    close(server_fd);
    return 0;
}


void *handle_client(void *arg) {
    int client_fd = *(int *)arg;
    char buffer[BUFFER_SIZE];
    int bytes_read;

    while ((bytes_read = read(client_fd, buffer, BUFFER_SIZE)) > 0) {
        buffer[bytes_read] = '\0';
        printf("Client: %s\n", buffer);
        send(client_fd, buffer, bytes_read, 0);
    }

    close(client_fd);
    return NULL;
}


void broadcast(char *msg, int sender_socket) {
    pthread_mutex_lock(&client_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sockets[i] != sender_socket) {
            send(client_sockets[i], msg, strlen(msg), 0);
        }
    }
    pthread_mutex_unlock(&client_mutex);
}