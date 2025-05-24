// Compile: gcc chatclient.c -o chatclient -lpthread
#include "chatDefination.h"

void send_file(int sock, char *recipient, char *filename) ;
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
            
        } 

        else if (n == 0) {
            printf("\nServer disconnected.\n");
            running = 0;
            break;
        } else {
            if (running) {
                perror("Read error");
            }
            break;
        }


        if (strncmp(buffer, "INCOMING_FILE", 13) == 0) {
            char sender[32], filename[128];
            size_t filesize;
            sscanf(buffer, "INCOMING_FILE %31s %127s %zu", sender, filename, &filesize);

            printf("Receiving file '%s' from %s (%zu bytes)\n", filename, sender, filesize);

            // check if file already exists
            if (access(filename, F_OK) != -1) {
               strcat(filename, "(1)");
            }

            FILE *fp = fopen(filename, "wb");
            size_t bytes_left = filesize;
            char chunk[FILE_CHUNK_SIZE];
            while (bytes_left > 0) {
                size_t to_read = (bytes_left < FILE_CHUNK_SIZE) ? bytes_left : FILE_CHUNK_SIZE;
                ssize_t n = recv(sock, chunk, to_read, 0);
                if (n <= 0) break;
                fwrite(chunk, 1, n, fp);
                bytes_left -= n;
            }
            fclose(fp);
            printf("File received: %s\n", filename);
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

int is_valid_username(const char *username) {
    if (strlen(username) < 3 || strlen(username) > MAX_USERNAME_LENGTH - 1) {
        return 0; // Invalid length
    }
    for (int i = 0; username[i] != '\0'; i++) {
        if (!isalnum(username[i]) && username[i] != '_') {
            return 0; // Invalid character
        }
    }
    return 1; // Valid username
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
    char server_buffer[BUFFER_SIZE];

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

    if (!is_valid_username(username)) {
        printf("Invalid username. Please use alphanumeric characters and underscores only.\n");
        running = 0;
        close(sock);
        exit(1);
    }

    snprintf(buffer, BUFFER_SIZE, "/username %s", username);
    send(sock, buffer, strlen(buffer), 0);
    
    printf("\nWelcome %s! Type /help for available commands.\n", username);

    // Main command loop
       while (running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(sock, &readfds);
        int maxfd = (sock > STDIN_FILENO) ? sock : STDIN_FILENO;

        printf("> ");
        fflush(stdout);

        int activity = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0) {
            perror("select");
            break;
        }

        // Check for server message (including server close)
        if (FD_ISSET(sock, &readfds)) {
            int n = recv(sock, server_buffer, sizeof(server_buffer) - 1, 0);
            if (n <= 0) {
                printf("\n[Disconnected from server]\n");
                break;
            }
            server_buffer[n] = '\0';
            printf("\n[Server]: %s\n> ", server_buffer);
            fflush(stdout);
        }

        // Check for user input
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
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
        if (strncmp(buffer, "/sendfile ", 10) == 0) {
            char recipient[32], filename[128];
            sscanf(buffer + 10, "%31s %127s", recipient, filename);
            send_file(sock, recipient, filename);
            continue;
        }
        
        // Send command/message to server
        strcat(buffer, "\n"); // Add newline for server command parsing
        if (send(sock, buffer, strlen(buffer), 0) < 0) {
            perror("Send failed");
            break;
        }
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


void send_file(int sock, char *recipient, char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        printf("Cannot open file!\n");
        return;
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    size_t filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // Send command
    char command[256];
   
    snprintf(command, sizeof(command), "/sendfile %s %s\n", recipient, filename);
    send(sock, command, strlen(command), 0);

    // Wait for server's "Ready to receive file size."
    char ack[64];
    recv(sock, ack, sizeof(ack), 0);
    if (strncmp(ack, "READY_FOR_FILE", 14) != 0) {
        printf("Server did not acknowledge file transfer.\n");
        fclose(fp);
        return;
    }


    // Send filesize
    char sizebuf[64];
    snprintf(sizebuf, sizeof(sizebuf), "%zu", filesize);
    send(sock, sizebuf, sizeof(sizebuf), 0);

    // Send file in chunks
    char buffer[FILE_CHUNK_SIZE];
    size_t total = 0;
    while (!feof(fp) && total < filesize) {
        size_t n = fread(buffer, 1, FILE_CHUNK_SIZE, fp);
        if (n > 0) {
            send(sock, buffer, n, 0);
            total += n;
        }
    }
    fclose(fp);

    printf("File sent: %s (%zu bytes)\n", filename, filesize);
}