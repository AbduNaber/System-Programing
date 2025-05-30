// Compile: gcc chatclient.c -o chatclient -lpthread
#include "chatDefination.h"

// Global variables
pthread_t server_response_thread;
int is_running = 1;

pthread_mutex_t ready_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t ready_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t socket_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t file_transfer_mutex = PTHREAD_MUTEX_INITIALIZER;
int ready_for_file = 0;
int file_transfer_in_progress = 0;

typedef struct {
    int socket_fd;
    char recipient[MAX_USERNAME_LENGTH];
    char filename[128];
} file_input_t;

// Function prototypes
void *handle_server_responses(void *arg);
void handle_incoming_file(char *buffer);
void print_help_menu(void);
int validate_username(const char *username);
int initialize_connection(const char *server_ip, int port);
int setup_username(int socket_fd);
void process_user_input(int socket_fd);
void cleanup_resources(int socket_fd);
int wait_for_ready(int timeout_seconds);


void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        printf(ANSI_COLOR_YELLOW "\nClient shutting down...\n" ANSI_COLOR_RESET);
        is_running = 0;

    }
}


int socket_send(int socket_fd, const char *buffer, size_t length) {
    int result;
    pthread_mutex_lock(&socket_mutex);
    result = send(socket_fd, buffer, length, 0);
    pthread_mutex_unlock(&socket_mutex);
    return result;
}


int can_send_command(void) {
    int can_send;
    pthread_mutex_lock(&file_transfer_mutex);
    can_send = !file_transfer_in_progress;
    pthread_mutex_unlock(&file_transfer_mutex);
    return can_send;
}

// Handle server responses in a separate thread
void *handle_server_responses(void *arg) {
    int socket_fd = *(int *)arg;
    char response_buffer[BUFFER_SIZE];
    
    while (is_running) {
       
        memset(response_buffer, 0, BUFFER_SIZE);
        int bytes_received = read(socket_fd, response_buffer, BUFFER_SIZE - 1);
       
        
        if (bytes_received > 0) {
            response_buffer[bytes_received] = '\0';
            printf("\n%s\n> ", response_buffer);
            fflush(stdout);
            
            if(strncmp(response_buffer, "FILE_SIZE_EXCEEDS_LIMIT", 23) == 0) {
                printf(ANSI_COLOR_RED "[ERROR] File size exceeds limit. Transfer aborted." ANSI_COLOR_RESET "\n");
                continue;
            }   

            // Check for incoming file transfer
            if (strncmp(response_buffer, "INCOMING_FILE", 13) == 0) {
                handle_incoming_file(response_buffer);
            }

            if(strncmp(response_buffer, "READY_FOR_FILE", 14) == 0) {
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

void handle_incoming_file( char *buffer) {
    char sender_name[32], original_filename[128], actual_filename[256];
    size_t file_size;
    
    sscanf(buffer, "INCOMING_FILE %31s %127s %zu", sender_name, original_filename, &file_size);
    printf("\nReceiving file '%s' from %s (%zu bytes)\n", original_filename, sender_name, file_size);
    
    printf("\nFile received successfully: %s\n", actual_filename);
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
    // Send username to server
    if (send(socket_fd, message_buffer, strlen(message_buffer), 0) < 0) {
        perror("Failed to send username");
        return 0;
    }
    
    // read server response
    char response_buffer[BUFFER_SIZE];
    int bytes_received = read(socket_fd, response_buffer, BUFFER_SIZE - 1);
    if (bytes_received < 0) {
        perror("Failed to read server response");
        return 0;
    }
    response_buffer[bytes_received] = '\0'; // Null-terminate the response

    if (strncmp(response_buffer, "ALREADY_TAKEN", 13) == 0) {
        printf(ANSI_COLOR_RED "[ERROR] Username already taken. Choose another." ANSI_COLOR_RESET "\n");
        return 0;
    }

    
    printf("\nWelcome %s! Type /help for available commands.\n", username);
    return 1;
}

void * handle_send_file(void *arg) {
    file_input_t *file_input = (file_input_t *)arg;
    int socket_fd = file_input->socket_fd;
    char *recipient = file_input->recipient;
    char *filename = file_input->filename;
    
    printf("Preparing to send file '%s' to user '%s'...\n", filename, recipient);
    
    // First, check if file exists and is readable
    int file_fd = open(filename, O_RDONLY);
    if (file_fd < 0) {
        printf("Error: Cannot open file '%s' - %s\n", filename, strerror(errno));
        free(file_input);
        return NULL;
    }
    
    // Get file size
    off_t file_size = lseek(file_fd, 0, SEEK_END);
    if (file_size < 0) {
        printf("Error: Cannot get file size for '%s'\n", filename);
        close(file_fd);
        free(file_input);
        return NULL;
    }
    lseek(file_fd, 0, SEEK_SET); // Reset to beginning
    close(file_fd); // Close for now, will reopen when ready to send
    
    // Send the initial sendfile command to server
    char command_buffer[BUFFER_SIZE];
    snprintf(command_buffer, sizeof(command_buffer), "/sendfile %s %s\n", recipient, filename);
    
    // Lock the socket for sending the initial command
    pthread_mutex_lock(&file_transfer_mutex);
    if (send(socket_fd, command_buffer, strlen(command_buffer), 0) < 0) {
        printf("Error: Failed to send file transfer command for '%s'\n", filename);
        pthread_mutex_unlock(&file_transfer_mutex);
        free(file_input);
        return NULL;
    }
    pthread_mutex_unlock(&file_transfer_mutex);
    
    printf("File transfer request sent. Waiting for server to be ready...\n");
    printf("You can continue using the chat while waiting.\n");
    
    // Wait for READY_FOR_FILE signal with timeout
    // This will block this thread but not the main thread
    int timeout_seconds = 60; // Wait up to 60 seconds
    if (!wait_for_ready(timeout_seconds)) {
        printf("Timeout: Server did not respond for file transfer of '%s'\n", filename);
        printf("File transfer cancelled. You may try again later.\n");
        free(file_input);
        return NULL;
    }
    
    printf("Server is ready! Starting file transfer for '%s'...\n", filename);
    
    // Lock the file transfer mutex to ensure only one file transfer at a time
    // and prevent other commands from interfering
    pthread_mutex_lock(&file_transfer_mutex);
    file_transfer_in_progress = 1;
    
    printf("Socket locked for file transfer. Chat commands will be blocked until transfer completes.\n");
    
    // Reopen the file for reading
    file_fd = open(filename, O_RDONLY);
    if (file_fd < 0) {
        printf("Error: Cannot reopen file '%s' for transfer - %s\n", filename, strerror(errno));
        file_transfer_in_progress = 0;
        pthread_mutex_unlock(&file_transfer_mutex);
        free(file_input);
        return NULL;
    }
    
    // Send file size to server
    char size_buffer[64];
    snprintf(size_buffer, sizeof(size_buffer), "%zu", (size_t)file_size);
    if (send(socket_fd, size_buffer, sizeof(size_buffer), 0) < 0) {
        printf("Error: Failed to send file size for '%s'\n", filename);
        close(file_fd);
        file_transfer_in_progress = 0;
        pthread_mutex_unlock(&file_transfer_mutex);
        free(file_input);
        return NULL;
    }
    

    
    close(file_fd);
    
    pthread_mutex_unlock(&file_transfer_mutex);
    
    
    printf("\nFile '%s' sent successfully!\n", filename);
    
    
    free(file_input);
    return NULL;
}

// Process user input and handle commands
void process_user_input(int socket_fd) {
    char input_buffer[BUFFER_SIZE];
    
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
            printf("> ");
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

        // Handle local commands (these don't need socket access)
        if (strcmp(input_buffer, "/help") == 0) {
            print_help_menu();
            continue;
        }

        // Handle exit command (always allowed, even during file transfer)
        if (strcmp(input_buffer, "/exit") == 0) {
            pthread_mutex_lock(&file_transfer_mutex);
            if (send(socket_fd, input_buffer, strlen(input_buffer), 0) < 0) {
                perror("Send failed");
            }
            pthread_mutex_unlock(&file_transfer_mutex);
            is_running = 0;
            break;
        }
        
        // Handle sendfile command
        if (strncmp(input_buffer, "/sendfile ", 10) == 0) {
            // Check if another file transfer is already in progress
            pthread_mutex_lock(&file_transfer_mutex);
            if (file_transfer_in_progress) {
                pthread_mutex_unlock(&file_transfer_mutex);
                printf("Another file transfer is already in progress. Please wait for it to complete.\n");
                continue;
            }
            pthread_mutex_unlock(&file_transfer_mutex);
            
            char recipient[32], filename[128];
            if (sscanf(input_buffer + 10, "%31s %127s", recipient, filename) == 2) {
                // Create a thread to handle file sending
                file_input_t *file_input = malloc(sizeof(file_input_t));
                if (file_input == NULL) {
                    perror("Memory allocation failed");
                    continue;
                }
                
                file_input->socket_fd = socket_fd;
                strncpy(file_input->recipient, recipient, sizeof(file_input->recipient) - 1);
                file_input->recipient[sizeof(file_input->recipient) - 1] = '\0';
                strncpy(file_input->filename, filename, sizeof(file_input->filename) - 1);
                file_input->filename[sizeof(file_input->filename) - 1] = '\0';
                
                pthread_t send_file_thread;
                if (pthread_create(&send_file_thread, NULL, handle_send_file, file_input) != 0) {
                    perror("Failed to create send file thread");
                    free(file_input);
                    continue;
                }
                pthread_detach(send_file_thread); // Detach thread to avoid memory leak
            } else {
                printf("Usage: /sendfile <recipient> <filename>\n");
            }
            continue;
        }
        
        // Check if file transfer is in progress before sending other commands
        pthread_mutex_lock(&file_transfer_mutex);
        if (file_transfer_in_progress) {
            pthread_mutex_unlock(&file_transfer_mutex);
            printf("File transfer in progress. Please wait until transfer completes before sending messages.\n");
            printf("(Available commands during transfer: /help, /exit)\n");
            continue;
        }
        pthread_mutex_unlock(&file_transfer_mutex);
        
        // Send command/message to server (protected by mutex)
        pthread_mutex_lock(&file_transfer_mutex);
        strcat(input_buffer, "\n"); // Add newline for server parsing
        if (send(socket_fd, input_buffer, strlen(input_buffer), 0) < 0) {
            perror("Send failed");
            pthread_mutex_unlock(&file_transfer_mutex);
            break;
        }
        pthread_mutex_unlock(&file_transfer_mutex);
    }
}

// wait for server to signal readiness for file transfer
int wait_for_ready(int timeout_seconds) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_seconds;
    
    int rc = 0;
    int success = 0;
    
    pthread_mutex_lock(&ready_mutex);
    
    // Wait until ready_for_file is set or timeout occurs
    while (!ready_for_file && rc != ETIMEDOUT) {
        rc = pthread_cond_timedwait(&ready_cond, &ready_mutex, &ts);
        
        // Check if the client is shutting down
        if (!is_running) {
            pthread_mutex_unlock(&ready_mutex);
            return 0; // Client is shutting down
        }
    }
    
    // Check the result after the loop
    if (ready_for_file) {
        ready_for_file = 0; // Reset for next use
        success = 1; // Success: ready signal received
    } else {
        success = 0; // Timeout or other error
    }
    
    pthread_mutex_unlock(&ready_mutex);
    return success;
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
    
    //init signal 
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;


    if (sigaction(SIGINT, &sa, NULL) == -1) {
    perror("sigaction");
    exit(EXIT_FAILURE);
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
    perror("sigaction");
    exit(EXIT_FAILURE);
    }

    // Initialize connection
    int socket_fd = initialize_connection(server_ip, port);
    if (socket_fd < 0) {
        exit(1);
    }

    
    
    
    // Setup username
    if (!setup_username(socket_fd)) {
        close(socket_fd);
        exit(1);
    }

    // Create server response handler thread
    if (pthread_create(&server_response_thread, NULL, handle_server_responses, (void *)&socket_fd) != 0) {
        perror("Failed to create response thread");
        close(socket_fd);
        exit(1);
    }
    
    // Process user input
    process_user_input(socket_fd);
    
    // Cleanup and exit
    cleanup_resources(socket_fd);
    return 0;
}