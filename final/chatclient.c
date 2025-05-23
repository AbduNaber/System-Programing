// Compile: gcc chatclient.c -o chatclient -lpthread
#include "chatDefination.h"

pthread_t server_response_thread;
int running = 1;

void *server_response_handler(void *arg) {
    int sock = *(int *)arg;
    char buffer[BUFFER_SIZE];
    
    while (running) {
        memset(buffer, 0, BUFFER_SIZE);
        int n = read(sock, buffer, BUFFER_SIZE - 1);
        if (n > 0) {
            buffer[n] = '\0';
            printf("\n%s\n> ", buffer);
            fflush(stdout);
        } else if (n == 0) {
            printf("\nServer disconnected.\n");
            running = 0;
            break;
        } else {
            if (running) {
                perror("Read error");
            }
            break;
        }
    }
    return NULL;
}

void print_help() {
    printf("\nAvailable commands:\n");
    printf("/username <name>     - Set your username\n");
    printf("/join <room>         - Join a chat room\n");
    printf("/broadcast <msg>     - Broadcast message to current room\n");
    printf("/whisper <user> <msg>- Send private message to user\n");
    printf("/sendfile <file> <user> - Send file to user (simulated)\n");
    printf("/list                - List users in current room\n");
    printf("/help                - Show this help\n");
    printf("/exit                - Exit the chat\n");
    printf("Or just type a message to send to current room\n\n");
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: ./%s <server_ip> <port>\n", argv[0]);
        exit(1);
    }

    int port = atoi(argv[2]);
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    // Server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address: %s\n", argv[1]);
        exit(1);
    }

    // Connect
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        exit(1);
    }
    printf("Connected to server at %s:%d\n", argv[1], port);

    // Create a thread to handle server responses
    if (pthread_create(&server_response_thread, NULL, server_response_handler, (void *)&sock) != 0) {
        perror("Failed to create response thread");
        close(sock);
        exit(1);
    }

    // Get username
    char username[MAX_USERNAME_LENGTH];
    printf("Enter your username: ");
    fflush(stdout);
    if (fgets(username, sizeof(username), stdin) == NULL) {
        printf("Error reading username\n");
        running = 0;
        close(sock);
        exit(1);
    }
    username[strcspn(username, "\n")] = 0; // Remove newline
    
    if (strlen(username) == 0) {
        printf("Username cannot be empty\n");
        running = 0;
        close(sock);
        exit(1);
    }
    
    snprintf(buffer, BUFFER_SIZE, "/username %s", username);
    send(sock, buffer, strlen(buffer), 0);
    
    printf("\nWelcome %s! Type /help for available commands.\n", username);

    // Main command loop
    while (running) {
        printf("> ");
        fflush(stdout);
        
        if (fgets(buffer, BUFFER_SIZE, stdin) == NULL) {
            break;
        }
        
        buffer[strcspn(buffer, "\n")] = 0; // Remove newline
        
        if (strlen(buffer) == 0) {
            continue;
        }
        
        if (strcmp(buffer, "/help") == 0) {
            print_help();
            continue;
        }
        
        if (strcmp(buffer, "/exit") == 0) {
            send(sock, buffer, strlen(buffer), 0);
            running = 0;
            break;
        }
        
        // Send command/message to server
        if (send(sock, buffer, strlen(buffer), 0) < 0) {
            perror("Send failed");
            break;
        }
    }

    // Cleanup
    running = 0;
    pthread_cancel(server_response_thread);
    pthread_join(server_response_thread, NULL);
    close(sock);
    printf("Disconnected from server. Goodbye!\n");
    
    return 0;
}