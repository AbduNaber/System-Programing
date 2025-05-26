// Compile: gcc chatclient.c -o chatclient -lpthread
#include "chatDefination.h"

// Global variables
pthread_t server_response_thread;
int is_running = 1;

pthread_mutex_t ready_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t ready_cond = PTHREAD_COND_INITIALIZER;
int ready_for_file = 0;

// Function prototypes
void *handle_server_responses(void *arg);
void handle_incoming_file(int socket_fd, char *buffer);
void print_help_menu(void);
int validate_username(const char *username);
int initialize_connection(const char *server_ip, int port);
int setup_username(int socket_fd);
void process_user_input(int socket_fd);
void send_file_to_user(int socket_fd, char *recipient, char *filename);
void cleanup_resources(int socket_fd);

// Handle server responses in a separate thread
void *handle_server_responses(void *arg) {
    int socket_fd = *(int *)arg;
    char response_buffer[BUFFER_SIZE];
    
    while (is_running) {
       
        memset(response_buffer, 0, BUFFER_SIZE);
        int bytes_received = read(socket_fd, response_buffer, BUFFER_SIZE - 1);
       
        
        if (bytes_received > 0) {
            response_buffer[bytes_received] = '\0';
            printf("\na%sa\n> ", response_buffer);
            fflush(stdout);
            
            // Check for incoming file transfer
            if (strncmp(response_buffer, "INCOMING_FILE", 13) == 0) {
                handle_incoming_file(socket_fd, response_buffer);
            }

            if(strncmp(response_buffer, "READY_FOR_FILE", 14) == 0) {
                printf("Server is ready for file transfer.\n");
                pthread_mutex_lock(&ready_mutex);
                ready_for_file = 1;
                pthread_cond_signal(&ready_cond);
                pthread_mutex_unlock(&ready_mutex);
            }
        } 
        else if (bytes_received == 0) {
            printf("\nServer disconnected.\n");
            is_running = 0;
            break;
        } 
        else {
            if (is_running) {
                perror("Read error");
            }
            break;
        }
    }
    return NULL;
}

// Handle incoming file transfers
void handle_incoming_file(int socket_fd, char *buffer) {
    char sender_name[32], original_filename[128], actual_filename[256];
    size_t file_size;
    
    sscanf(buffer, "INCOMING_FILE %31s %127s %zu", sender_name, original_filename, &file_size);
    printf("Receiving file '%s' from %s (%zu bytes)\n", original_filename, sender_name, file_size);
    
    // Create unique filename if file already exists
    strcpy(actual_filename, original_filename);
    if (access(actual_filename, F_OK) != -1) {
        snprintf(actual_filename, sizeof(actual_filename), "%s(1)", original_filename);
    }
    
    int file_fd = open(actual_filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file_fd < 0) {
        perror("Failed to create file");
        return;
    }
    printf("Saving file as: %s\n", actual_filename);
    // get file from server
    char file_buffer[FILE_CHUNK_SIZE];
    size_t total_bytes_received = 0;
    while (total_bytes_received < file_size) {
        
        ssize_t bytes_received = read(socket_fd, file_buffer, FILE_CHUNK_SIZE);
        if (bytes_received < 0) {
            perror("Error receiving file data");
            close(file_fd);
            return;
        } else if (bytes_received == 0) {
            printf("Connection closed before file transfer completed.\n");
            close(file_fd);
            return;
        }
        
        write(file_fd, file_buffer, bytes_received);
        total_bytes_received += bytes_received;
    }
    close(file_fd);
    printf("File received successfully: %s\n", actual_filename);
}

// Display help menu
void print_help_menu(void) {
    printf("\nAvailable commands:\n");
    printf("/username <name>     - Set your username\n");
    printf("/join <room>         - Join a chat room\n");
    printf("/broadcast <msg>     - Broadcast message to current room\n");
    printf("/whisper <user> <msg>- Send private message to user\n");
    printf("/sendfile <file> <user> - Send file to user\n");
    printf("/list                - List users in current room\n");
    printf("/help                - Show this help\n");
    printf("/exit                - Exit the chat\n");
    printf("Or just type a message to send to current room\n\n");
}

// Validate username format
int validate_username(const char *username) {
    size_t username_length = strlen(username);
    
    if (username_length < 3 || username_length > MAX_USERNAME_LENGTH - 1) {
        return 0; // Invalid length
    }
    
    for (size_t i = 0; i < username_length; i++) {
        if (!isalnum(username[i]) && username[i] != '_') {
            return 0; // Invalid character
        }
    }
    
    return 1; // Valid username
}

// Initialize connection to server
int initialize_connection(const char *server_ip, int port) {
    int socket_fd;
    struct sockaddr_in server_address;
    
    // Create socket
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        perror("Socket creation failed");
        return -1;
    }
    
    // Configure server address
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    
    if (inet_pton(AF_INET, server_ip, &server_address.sin_addr) <= 0) {
        fprintf(stderr, "Invalid address: %s\n", server_ip);
        close(socket_fd);
        return -1;
    }
    
    // Connect to server
    if (connect(socket_fd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        perror("Connection failed");
        close(socket_fd);
        return -1;
    }
    
    printf("Connected to server at %s:%d\n", server_ip, port);
    return socket_fd;
}

// Setup username with server
int setup_username(int socket_fd) {
    char username[MAX_USERNAME_LENGTH];
    char message_buffer[BUFFER_SIZE];
    
    printf("Enter your username: ");
    fflush(stdout);
    
    if (fgets(username, sizeof(username), stdin) == NULL) {
        printf("Error reading username\n");
        return 0;
    }
    
    username[strcspn(username, "\n")] = 0; // Remove newline
    
    if (strlen(username) == 0) {
        printf("Username cannot be empty\n");
        return 0;
    }
    
    if (!validate_username(username)) {
        printf("Invalid username. Please use alphanumeric characters and underscores only.\n");
        return 0;
    }
    
    snprintf(message_buffer, BUFFER_SIZE, "/username %s", username);
    if (send(socket_fd, message_buffer, strlen(message_buffer), 0) < 0) {
        perror("Failed to send username");
        return 0;
    }
    
    printf("\nWelcome %s! Type /help for available commands.\n", username);
    return 1;
}

// Process user input and handle commands
void process_user_input(int socket_fd) {
    char input_buffer[BUFFER_SIZE];
    char server_response[BUFFER_SIZE];
    fd_set read_file_descriptors;
    
    while (is_running) {

       

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        struct timeval timeout = {0, 100000}; // 100ms

        int activity = select(STDIN_FILENO + 1, &read_fds, NULL, NULL, &timeout);
        if (activity < 0) {
            perror("Select error");
            break;
        } else if (activity == 0) {
            // No input available, continue to next iteration
            continue;
        }
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            memset(input_buffer, 0, BUFFER_SIZE);
            printf( "> " );
            fflush(stdout);
            if (fgets(input_buffer, sizeof(input_buffer), stdin) == NULL) {
                printf("Error reading input\n");
                continue;
            }
            // Remove newline character
            if (input_buffer[strlen(input_buffer) - 1] == '\n') {
                input_buffer[strlen(input_buffer) - 1] = '\0';
            }
            // Check for empty input
            if (input_buffer[0] == '\0') {
                continue; // Skip empty input
            }
        }





            // Handle local commands
            if (strcmp(input_buffer, "/help") == 0) {
                print_help_menu();
                continue;
            }

            if (strcmp(input_buffer, "/exit") == 0) {
                send(socket_fd, input_buffer, strlen(input_buffer), 0);
                is_running = 0;
                break;
            }
            
            if (strncmp(input_buffer, "/sendfile ", 10) == 0) {
                char recipient[32], filename[128];
                if (sscanf(input_buffer + 10, "%31s %127s", recipient, filename) == 2) {
                    send_file_to_user(socket_fd, recipient, filename);
                } else {
                    printf("Usage: /sendfile <recipient> <filename>\n");
                }
                continue;
            }
            
            // Send command/message to server
            strcat(input_buffer, "\n"); // Add newline for server parsing
            if (send(socket_fd, input_buffer, strlen(input_buffer), 0) < 0) {
                perror("Send failed");
                break;
            }
        }
    }

// wait for server to signal readiness for file transfer
int wait_for_ready(int timeout_seconds) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_seconds;

    int rc = 0;

    pthread_mutex_lock(&ready_mutex);
    while (!ready_for_file && rc != ETIMEDOUT) {
        rc = pthread_cond_timedwait(&ready_cond, &ready_mutex, &ts);
    }
    if (ready_for_file) {
        ready_for_file = 0; // Reset for next use
        pthread_mutex_unlock(&ready_mutex);
        return 1; // Success: ready signal received
    } else {
        pthread_mutex_unlock(&ready_mutex);
        return 0; // Timeout or error
    }
}

// Send file to another user
void send_file_to_user(int socket_fd, char *recipient, char *filename) {
    FILE *file_ptr = fopen(filename, "rb");
    if (!file_ptr) {
        printf("Error: Cannot open file '%s'\n", filename);
        return;
    }
    
    // Get file size
    fseek(file_ptr, 0, SEEK_END);
    size_t file_size = ftell(file_ptr);
    fseek(file_ptr, 0, SEEK_SET);
    
    // Send file transfer command
    char command_buffer[256];
    
    
    snprintf(command_buffer, sizeof(command_buffer), "/sendfile %s %s\n", recipient, filename);
    
    if (send(socket_fd, command_buffer, strlen(command_buffer), 0) < 0) {
        printf("Error: Failed to send file transfer command\n");
        
        fclose(file_ptr);
        return;
    }

    // Wait for server to signal readiness for file transfer
    if(! wait_for_ready(5)) {
        printf("Error: Server did not signal read for file transfer\n");
        fclose(file_ptr);
        return;
    }
    // Send file size
    char size_buffer[64];
    snprintf(size_buffer, sizeof(size_buffer), "%zu", file_size);
    if (send(socket_fd, size_buffer, sizeof(size_buffer), 0) < 0) {
        printf("Error: Failed to send file size\n");
        fclose(file_ptr);
        return;
    }
    
    // Send file in chunks
    char file_buffer[FILE_CHUNK_SIZE];
    size_t total_bytes_sent = 0;
    
    while (!feof(file_ptr) && total_bytes_sent < file_size) {
        size_t bytes_read = fread(file_buffer, 1, FILE_CHUNK_SIZE, file_ptr);
        if (bytes_read > 0) {
            if (send(socket_fd, file_buffer, bytes_read, 0) < 0) {
                printf("Error: Failed to send file data\n");
                break;
            }
            total_bytes_sent += bytes_read;
        }
    }
    
    fclose(file_ptr);
    printf("File sent successfully: %s (%zu bytes)\n", filename, file_size);
}

// Clean up resources
void cleanup_resources(int socket_fd) {
    is_running = 0;
    pthread_cancel(server_response_thread);
    pthread_join(server_response_thread, NULL);
    close(socket_fd);
    printf("Disconnected from server. Goodbye!\n");
}

// Main function
int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
        exit(1);
    }
    
    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    
    // Initialize connection
    int socket_fd = initialize_connection(server_ip, port);
    if (socket_fd < 0) {
        exit(1);
    }
    
    // Create server response handler thread
    if (pthread_create(&server_response_thread, NULL, handle_server_responses, (void *)&socket_fd) != 0) {
        perror("Failed to create response thread");
        close(socket_fd);
        exit(1);
    }
    
    // Setup username
    if (!setup_username(socket_fd)) {
        cleanup_resources(socket_fd);
        exit(1);
    }
    
    // Process user input
    process_user_input(socket_fd);
    
    // Cleanup and exit
    cleanup_resources(socket_fd);
    return 0;
}