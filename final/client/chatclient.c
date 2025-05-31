// Compile: gcc chatclient.c -o chatclient -lpthread
#include "../shared/chatDefination.h"

// Enhanced color definitions for better user experience
#define ANSI_COLOR_SUCCESS      "\x1b[32m"      // Green for success messages
#define ANSI_COLOR_ERROR        "\x1b[31m"      // Red for error messages
#define ANSI_COLOR_WARNING      "\x1b[33m"      // Yellow for warnings
#define ANSI_COLOR_INFO         "\x1b[36m"      // Cyan for info messages
#define ANSI_COLOR_PRIVATE      "\x1b[35m"      // Magenta for private messages
#define ANSI_COLOR_BROADCAST    "\x1b[34m"      // Blue for broadcast messages
#define ANSI_COLOR_SYSTEM       "\x1b[37m"      // White for system messages
#define ANSI_COLOR_USERNAME     "\x1b[1;36m"    // Bold cyan for usernames
#define ANSI_COLOR_FILENAME     "\x1b[1;33m"    // Bold yellow for filenames
#define ANSI_COLOR_PROMPT       "\x1b[1;32m"    // Bold green for prompts
#define ANSI_COLOR_RESET        "\x1b[0m"       // Reset color

// Global variables
pthread_t server_response_thread;
int is_running = 1;

pthread_mutex_t ready_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t ready_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t file_transfer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t file_transfer_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t socket_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t prompt_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t file_transfer_progress_mutex = PTHREAD_MUTEX_INITIALIZER;
int ready_for_file = 0;
int file_transfer_finished = 0;
int file_transfer_in_progress = 0;
int prompt_shown = 0;

char username[MAX_USERNAME_LENGTH];

// Function prototypes
void *handle_server_responses(void *arg);
void handle_incoming_file(int socket_fd,char *buffer);
void print_help_menu(void);
int validate_username(const char *username);
int initialize_connection(const char *server_ip, int port);
int setup_username(int socket_fd);
void process_user_input(int socket_fd);
void cleanup_resources(int socket_fd);
int wait_for_ready(int timeout_seconds);
int handle_send_file(int socket_fd, const char *recipient, const char *filename);
int validate_room_name(const char *room_name) ;
void print_status_message(const char *message, const char *color);
int wait_for_file_transfer(int timeout_seconds) ;



void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        printf(ANSI_COLOR_WARNING "\n[SYSTEM] Client shutting down gracefully...\n" ANSI_COLOR_RESET);
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

void show_prompt() {
    pthread_mutex_lock(&prompt_mutex);
    if (!prompt_shown) {
        printf(ANSI_COLOR_PROMPT "> " ANSI_COLOR_RESET);
        fflush(stdout);
        prompt_shown = 1;
    }
    pthread_mutex_unlock(&prompt_mutex);
}

void clear_prompt() {
    pthread_mutex_lock(&prompt_mutex);
    if (prompt_shown) {
        printf("\r\033[K");  // Clear current line
        fflush(stdout);
        prompt_shown = 0;
    }
    pthread_mutex_unlock(&prompt_mutex);
}

// Enhanced function to print colored status messages
void print_status_message(const char *message, const char *color) {
    printf("%s%s%s\n", color, message, ANSI_COLOR_RESET);
    fflush(stdout);
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
           
            // Handle specific server responses with appropriate colors
            if (strncmp(response_buffer, "FILE_SIZE_EXCEEDS_LIMIT", 23) == 0) {
                print_status_message("[ERROR] File size exceeds server limit. Transfer aborted.", ANSI_COLOR_ERROR);
                pthread_mutex_lock(&file_transfer_progress_mutex);
                file_transfer_in_progress = 0;
                pthread_mutex_unlock(&file_transfer_progress_mutex);
                continue;
            }



            // Check for incoming file transfer
            else if (strncmp(response_buffer, "INCOMING_FILE", 13) == 0) {
                handle_incoming_file(socket_fd,response_buffer);
            }

            else if (strncmp(response_buffer, "RECIPIENT_NOT_FOUND", 19) == 0) {

                print_status_message("[ERROR] Recipient not found. Please check the username.", ANSI_COLOR_ERROR);
                pthread_mutex_lock(&ready_mutex);
                ready_for_file = -1;
                pthread_cond_signal(&ready_cond);
                pthread_mutex_unlock(&ready_mutex);
            }
            else if (strncmp(response_buffer, "RECIPIENT_OFFLINE", 17) == 0) {
                print_status_message("[ERROR] Recipient is offline. Cannot send file.", ANSI_COLOR_ERROR);
                 pthread_mutex_lock(&ready_mutex);
                ready_for_file = 0;
                pthread_cond_signal(&ready_cond);
                pthread_mutex_unlock(&ready_mutex);
            }
            else if (strncmp(response_buffer, "FILE_EXISTS", 11) == 0) {
                print_status_message("[WARNING] File already exists on server. Renaming to avoid conflict.", ANSI_COLOR_WARNING);
            }
            
            else if(strncmp(response_buffer, "FILE_TRANSFER_SUCCESS", 21) == 0) {
               

                pthread_mutex_lock(&file_transfer_mutex);
                file_transfer_finished = 1;
                pthread_cond_broadcast(&file_transfer_cond);
                pthread_mutex_unlock(&file_transfer_mutex);

            }
            else if(strncmp(response_buffer, "INVALID_FILE_TYPE", 20) == 0) {
                print_status_message("[ERROR] Invalid file type. File transfer aborted.", ANSI_COLOR_ERROR);
                pthread_mutex_lock(&ready_mutex);
                ready_for_file = 0;
                pthread_cond_signal(&ready_cond);
                pthread_mutex_unlock(&ready_mutex);
            }
            else if(strncmp(response_buffer, "READY_FOR_FILE", 14) == 0) {
                pthread_mutex_lock(&ready_mutex);
                ready_for_file = 1;
                pthread_cond_signal(&ready_cond);
                pthread_mutex_unlock(&ready_mutex);
    
            }
            
            else if(strncmp(response_buffer, "USER_NOT_FOUND", 14) == 0) {
                print_status_message("[ERROR] User not found or not online.", ANSI_COLOR_ERROR);
            }
            else if(strncmp(response_buffer, "ROOM_JOINED", 11) == 0) {
                print_status_message("[SUCCESS] Successfully joined the room.", ANSI_COLOR_SUCCESS);
            }
            else if(strncmp(response_buffer, "USERNAME_SET", 12) == 0) {
                print_status_message("[SUCCESS] Username set successfully.", ANSI_COLOR_SUCCESS);
            }

            else if (strncmp(response_buffer, "FILE_QUEUE_FULL", 15) == 0) {
                print_status_message("[ERROR] File queue is full. Please try again later.", ANSI_COLOR_ERROR);
                pthread_mutex_lock(&ready_mutex);
                ready_for_file = 0;
                pthread_cond_signal(&ready_cond);
                pthread_mutex_unlock(&ready_mutex);
        
            }

            else if (strncmp(response_buffer, "ROOM_LEFT", 9) == 0) {
                print_status_message("[SUCCESS] Successfully left the room.", ANSI_COLOR_SUCCESS);
            }
            else {
                // Use enhanced message parsing for other responses
                printf("\n");
                printf(ANSI_COLOR_SYSTEM "%s" ANSI_COLOR_RESET "\n", response_buffer);
            
            }

            pthread_mutex_lock(&file_transfer_progress_mutex);
            int transfer_active = file_transfer_in_progress;
            pthread_mutex_unlock(&file_transfer_progress_mutex);
            
            if (!transfer_active && is_running) {
                printf(ANSI_COLOR_PROMPT "> " ANSI_COLOR_RESET);
                fflush(stdout);
            }

        } 
        else if (bytes_received == 0) {
            printf("\r\033[K"); // Clear current line
            print_status_message("[SYSTEM] Server disconnected.", ANSI_COLOR_WARNING);
            is_running = 0;
            break;
        } 
        else {
            if (is_running) {
                printf("\r\033[K"); // Clear current line
                print_status_message("[ERROR] Connection read error.", ANSI_COLOR_ERROR);
            }
            break;
        }
    }
    return NULL;
}

void handle_incoming_file(int socket_fd, char *buffer) {
    // Set file transfer in progress flag for incoming files too
    pthread_mutex_lock(&file_transfer_progress_mutex);
    file_transfer_in_progress = 1;
    pthread_mutex_unlock(&file_transfer_progress_mutex);
    
    char sender_name[32], original_filename[128], actual_filename[256];
    size_t file_size;
    char output_buffer[BUFFER_SIZE];
    sscanf(buffer, "INCOMING_FILE %31s %127s %zu", sender_name, original_filename, &file_size);
    printf(ANSI_COLOR_INFO "\n[FILE TRANSFER] Receiving file " ANSI_COLOR_FILENAME "'%s'" ANSI_COLOR_INFO " from " ANSI_COLOR_USERNAME "%s" ANSI_COLOR_INFO " (%zu bytes)" ANSI_COLOR_RESET "\n", original_filename, sender_name, file_size);
    
    if (access(original_filename, F_OK) == 0) {
        socket_send(socket_fd, "FILE_EXISTS", 12);
        snprintf(actual_filename, sizeof(actual_filename), "%s_%s", sender_name, original_filename);
        snprintf(output_buffer, sizeof(output_buffer), "[WARNING] File '%s' already exists. Renaming to '%s'", original_filename, actual_filename);
        print_status_message(output_buffer, ANSI_COLOR_WARNING);    
    } else {
        snprintf(actual_filename, sizeof(actual_filename), "%s", original_filename);
    }
    
     
    int fd = open(actual_filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        print_status_message("[ERROR] Failed to open file for writing.", ANSI_COLOR_ERROR);
        // Clear file transfer flag on error
        pthread_mutex_lock(&file_transfer_progress_mutex);
        file_transfer_in_progress = 0;
        pthread_mutex_unlock(&file_transfer_progress_mutex);
        return;
    }

   
    
    print_status_message("[SUCCESS] File received successfully.", ANSI_COLOR_SUCCESS);
    
    // Clear file transfer in progress flag
    pthread_mutex_lock(&file_transfer_progress_mutex);
    file_transfer_in_progress = 0;
    pthread_mutex_unlock(&file_transfer_progress_mutex);

}

void print_help_menu(void) {
    printf(ANSI_COLOR_SYSTEM "\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║                    AVAILABLE COMMANDS                    ║\n");
    printf("╠══════════════════════════════════════════════════════════╣\n" ANSI_COLOR_RESET);
    printf(ANSI_COLOR_INFO "║ " ANSI_COLOR_USERNAME "/username <name>" ANSI_COLOR_INFO "     - Set your username                    ║\n" ANSI_COLOR_RESET);
    printf(ANSI_COLOR_INFO "║ " ANSI_COLOR_USERNAME "/detach <room>" ANSI_COLOR_INFO "       - Detach from a chat room               ║\n" ANSI_COLOR_RESET);
    printf(ANSI_COLOR_INFO "║ " ANSI_COLOR_USERNAME "/broadcast <msg>" ANSI_COLOR_INFO "     - Broadcast message to current room       ║\n" ANSI_COLOR_RESET);
    printf(ANSI_COLOR_INFO "║ " ANSI_COLOR_USERNAME "/whisper <user> <msg>" ANSI_COLOR_INFO "- Send private message to user            ║\n" ANSI_COLOR_RESET);
    printf(ANSI_COLOR_INFO "║ " ANSI_COLOR_USERNAME "/sendfile <file> <user>" ANSI_COLOR_INFO " - Send file to user                   ║\n" ANSI_COLOR_RESET);
    printf(ANSI_COLOR_INFO "║ " ANSI_COLOR_USERNAME "/leave" ANSI_COLOR_INFO "                - Leave the current chat room              ║\n" ANSI_COLOR_RESET);
    printf(ANSI_COLOR_INFO "║ " ANSI_COLOR_USERNAME "/list" ANSI_COLOR_INFO "                - List users in current room          ║\n" ANSI_COLOR_RESET);
    printf(ANSI_COLOR_INFO "║ " ANSI_COLOR_USERNAME "/help" ANSI_COLOR_INFO "                - Show this help menu                 ║\n" ANSI_COLOR_RESET);
    printf(ANSI_COLOR_INFO "║ " ANSI_COLOR_USERNAME "/exit" ANSI_COLOR_INFO "                - Exit the chat application           ║\n" ANSI_COLOR_RESET);
    printf(ANSI_COLOR_SYSTEM "╠══════════════════════════════════════════════════════════╣\n");
    printf("║ " ANSI_COLOR_SUCCESS "TIP:" ANSI_COLOR_SYSTEM " Type a message without '/' to send to current room ║\n");
    printf("╚══════════════════════════════════════════════════════════╝" ANSI_COLOR_RESET "\n\n");
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
    
    print_status_message("[INFO] Attempting to connect to server...", ANSI_COLOR_INFO);
    
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        print_status_message("[ERROR] Socket creation failed", ANSI_COLOR_ERROR);
        return -1;
    }
    
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    
    if (inet_pton(AF_INET, server_ip, &server_address.sin_addr) <= 0) {
        printf(ANSI_COLOR_ERROR "[ERROR] Invalid server address: %s" ANSI_COLOR_RESET "\n", server_ip);
        close(socket_fd);
        return -1;
    }
    
    if (connect(socket_fd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        print_status_message("[ERROR] Connection to server failed", ANSI_COLOR_ERROR);
        close(socket_fd);
        return -1;
    }
    
    // Check for server full message
    char buffer[BUFFER_SIZE];
    int bytes_received = read(socket_fd, buffer, BUFFER_SIZE - 1);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        if (strstr(buffer, "Server full")) {
            print_status_message("[ERROR] Server is full. Try again later.", ANSI_COLOR_ERROR);
            close(socket_fd);
            return -1;
        }
        else if (strstr(buffer, "SUCCESS_LOGIN")) {
            print_status_message("[SUCCESS] Connected to server successfully.", ANSI_COLOR_SUCCESS);
        } else {
            print_status_message("[ERROR] Unexpected server response: ", ANSI_COLOR_ERROR);
            printf(ANSI_COLOR_SYSTEM "%s" ANSI_COLOR_RESET "\n", buffer);
            close(socket_fd);
            return -1;
        }
    }
    return socket_fd;
}

// Setup username with server
int setup_username(int socket_fd) {
    char input_buffer[BUFFER_SIZE];
    printf(ANSI_COLOR_PROMPT "enter username: " ANSI_COLOR_RESET);
    while (1) {

        fflush(stdout);

        // Use select to monitor both stdin and socket for input/disconnection
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(socket_fd, &read_fds);
        
        int max_fd = (socket_fd > STDIN_FILENO) ? socket_fd : STDIN_FILENO;
        struct timeval timeout = {1, 0}; // 1 second timeout for periodic checks
        
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (activity < 0) {
            print_status_message("[ERROR] Select failed", ANSI_COLOR_ERROR);
            exit(1);
        } else if (activity == 0) {
            
            continue;
        }
        
        if (FD_ISSET(socket_fd, &read_fds)) {
    char test_buffer[BUFFER_SIZE];
    
    // Actually receive the data (without MSG_PEEK first)
    int bytes = recv(socket_fd, test_buffer, BUFFER_SIZE - 1, MSG_DONTWAIT);
    
    if (bytes <= 0) {
        if (bytes == 0 || (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            print_status_message("[ERROR] Server disconnected while waiting for username", ANSI_COLOR_ERROR);
            close(socket_fd);
            exit(1); 
        }
    } 
}
        
        // Check if stdin has input
        if (!FD_ISSET(STDIN_FILENO, &read_fds)) {
            continue; // No stdin input, keep waiting
        }
         
        // Read username from stdin
        memset(input_buffer, 0, BUFFER_SIZE);
        if (fgets(input_buffer, sizeof(input_buffer), stdin) == NULL) {
            print_status_message("[ERROR] Failed to read username", ANSI_COLOR_ERROR);
            continue;
        }
        
        // Remove newline character if present
        size_t len = strlen(input_buffer);
        if (len > 0 && input_buffer[len - 1] == '\n') {
            input_buffer[len - 1] = '\0';
        }
        
        // Check for empty input
        if (input_buffer[0] == '\0') {
            print_status_message("[ERROR] Username cannot be empty", ANSI_COLOR_ERROR);
            continue;
        }
        
        // Validate username format
        if (!validate_username(input_buffer)) {
            print_status_message("[ERROR] Invalid username format. Use alphanumeric characters and underscores only (3+ chars)", ANSI_COLOR_ERROR);
            continue;
        }
        
        // Copy validated username to global username variable
        strncpy(username, input_buffer, BUFFER_SIZE - 1);
        username[BUFFER_SIZE - 1] = '\0'; // Ensure null termination
        
        // Prepare command to send to server
        char command_buffer[BUFFER_SIZE];
        snprintf(command_buffer, BUFFER_SIZE, "/username %s", username);
        
        // Send username to server
        if (send(socket_fd, command_buffer, strlen(command_buffer), 0) < 0) {
            print_status_message("[ERROR] Failed to send username to server", ANSI_COLOR_ERROR);
            exit(1);
        }
        
        // Wait for server response with timeout
        fd_set response_fds;
        FD_ZERO(&response_fds);
        FD_SET(socket_fd, &response_fds);
        
        struct timeval response_timeout = {5, 0}; // 5 second timeout for server response
        int response_activity = select(socket_fd + 1, &response_fds, NULL, NULL, &response_timeout);
        
        if (response_activity < 0) {
            print_status_message("[ERROR] Failed waiting for server response", ANSI_COLOR_ERROR);
            exit(1);
        } else if (response_activity == 0) {
            print_status_message("[ERROR] Server response timeout", ANSI_COLOR_ERROR);
            exit(1);
        }
        
        // Read server response
        char response_buffer[BUFFER_SIZE];
        int bytes_received = recv(socket_fd, response_buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received < 0) {
            print_status_message("[ERROR] Failed to receive server response", ANSI_COLOR_ERROR);
            exit(1);
        } else if (bytes_received == 0) {
            print_status_message("[ERROR] Server disconnected", ANSI_COLOR_ERROR);
            exit(1);
        }
        
        response_buffer[bytes_received] = '\0'; // Null-terminate the response
        
        // Check server response
        if (strncmp(response_buffer, "ALREADY_TAKEN", 13) == 0) {
            print_status_message("[ERROR] Username already taken. Please choose another.", ANSI_COLOR_ERROR);
            printf(ANSI_COLOR_PROMPT "enter username: " ANSI_COLOR_RESET);
            continue; // Try again
        } else if (strncmp(response_buffer, "SET_USERNAME", 12) == 0) {
            printf(ANSI_COLOR_SUCCESS "\n[SUCCESS] Welcome " ANSI_COLOR_USERNAME "%s" ANSI_COLOR_SUCCESS "! Type " ANSI_COLOR_INFO "/help" ANSI_COLOR_SUCCESS " for available commands.\n" ANSI_COLOR_RESET, username);
            return 1;
        } else {
            print_status_message("[ERROR] Unexpected server response", ANSI_COLOR_ERROR);
            printf(ANSI_COLOR_SYSTEM "%s" ANSI_COLOR_RESET "\n", response_buffer);
            printf(ANSI_COLOR_PROMPT "enter username: " ANSI_COLOR_RESET);
            continue;
        }
    }
}

// Handle file sending in main thread (blocking)
int handle_send_file(int socket_fd, const char *recipient, const char *filename) {
    printf(ANSI_COLOR_INFO "[FILE TRANSFER] Preparing to send " ANSI_COLOR_FILENAME "'%s'" ANSI_COLOR_INFO " to user " ANSI_COLOR_USERNAME "'%s'" ANSI_COLOR_INFO "..." ANSI_COLOR_RESET "\n", filename, recipient);
    
    if(strcmp(recipient, username) == 0) {
        print_status_message("[ERROR] You cannot send a file to yourself.", ANSI_COLOR_ERROR);
        return 0;
    }

    // First, check if file exists and is readable
    int file_fd = open(filename, O_RDONLY);
    if (file_fd < 0) {
        printf(ANSI_COLOR_ERROR "[ERROR] Cannot open file " ANSI_COLOR_FILENAME "'%s'" ANSI_COLOR_ERROR " - %s" ANSI_COLOR_RESET "\n", filename, strerror(errno));
        return 0;
    }

    // Get file size
    off_t file_size = lseek(file_fd, 0, SEEK_END);
    if (file_size < 0) {
        printf(ANSI_COLOR_ERROR "[ERROR] Cannot get file size for " ANSI_COLOR_FILENAME "'%s'" ANSI_COLOR_RESET "\n", filename);
        close(file_fd);
        return 0;
    }
    lseek(file_fd, 0, SEEK_SET); // Reset to beginning
    close(file_fd); // Close for now, will reopen when ready to send
    
    // Send the initial sendfile command to server
    char command_buffer[BUFFER_SIZE];
    snprintf(command_buffer, sizeof(command_buffer), "/sendfile %s %s %zu", filename, recipient, (size_t)file_size);
    
    printf("[DEBUG] Sending command: '%s'\n", command_buffer);  // Debug output

    if (send(socket_fd, command_buffer, strlen(command_buffer), 0) < 0) {
        printf(ANSI_COLOR_ERROR "[ERROR] Failed to send file transfer command for " ANSI_COLOR_FILENAME "'%s'" ANSI_COLOR_RESET "\n", filename);
        return 0;
    }

    print_status_message("[INFO] File transfer request sent. Waiting for server confirmation...", ANSI_COLOR_INFO);
    print_status_message("[WARNING] Please wait - file transfer will block other commands until complete.", ANSI_COLOR_WARNING);
    


    // Wait for READY_FOR_FILE signal with timeout
    int timeout_seconds = 60; // Wait up to 60 seconds
    int ret_code = wait_for_ready(timeout_seconds);
    if (ret_code == 0) {
        
        printf(ANSI_COLOR_ERROR "[ERROR] Timeout: Server did not respond for file transfer of " ANSI_COLOR_FILENAME "'%s'" ANSI_COLOR_RESET "\n", filename);
        print_status_message("[WARNING] File transfer cancelled. You may try again later.", ANSI_COLOR_WARNING);
        return 0;
    }
    else if (ret_code < 0) {
        
        return 0;
    }
    printf("---%d---\n", ret_code);
    
    printf(ANSI_COLOR_SUCCESS "[SUCCESS] Server is ready! Starting file transfer for " ANSI_COLOR_FILENAME "'%s'" ANSI_COLOR_SUCCESS "..." ANSI_COLOR_RESET "\n", filename);
   
    
    int timout = wait_for_file_transfer(20); // Wait for file transfer to complete
    if (!timout) {
        printf(ANSI_COLOR_ERROR "[ERROR] File transfer timed out for " ANSI_COLOR_FILENAME "'%s'" ANSI_COLOR_RESET "\n", filename);
        
        return 0;
    }
    
    printf(ANSI_COLOR_SUCCESS "\n[SUCCESS] File " ANSI_COLOR_FILENAME "'%s'" ANSI_COLOR_SUCCESS " sent successfully!" ANSI_COLOR_RESET "\n", filename);
    print_status_message("[INFO] You can now continue using chat commands.", ANSI_COLOR_INFO);
    
    return 1;
}

// Process user input and handle commands
void process_user_input(int socket_fd) {
    char input_buffer[BUFFER_SIZE];
    int transfer_active = 0;
    
    // Show initial prompt
    printf(ANSI_COLOR_PROMPT "> " ANSI_COLOR_RESET);
    fflush(stdout);
    
    while (is_running) {
        // Check if file transfer is in progress
        pthread_mutex_lock(&file_transfer_progress_mutex);
        transfer_active = file_transfer_in_progress;
        pthread_mutex_unlock(&file_transfer_progress_mutex);
        
        if (transfer_active) {
            // Don't show the repeated message, just wait
            usleep(500000); // Sleep for 500ms
            continue;
        }
        
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        struct timeval timeout = {0, 100000}; // 100ms

        int activity = select(STDIN_FILENO + 1, &read_fds, NULL, NULL, &timeout);
        if (activity < 0) {
            break;
        } else if (activity == 0) {
            continue;
        }
        
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            memset(input_buffer, 0, BUFFER_SIZE);
            // Don't print prompt here anymore - it's already shown
            if (fgets(input_buffer, sizeof(input_buffer), stdin) == NULL) {
                print_status_message("[ERROR] Failed to read user input", ANSI_COLOR_ERROR);
                printf(ANSI_COLOR_PROMPT "> " ANSI_COLOR_RESET);
                fflush(stdout);
                continue;
            }
            
            // Remove newline character
            if (input_buffer[strlen(input_buffer) - 1] == '\n') {
                input_buffer[strlen(input_buffer) - 1] = '\0';
            }
            
            // Check for empty input
            if (input_buffer[0] == '\0') {
                printf(ANSI_COLOR_PROMPT "> " ANSI_COLOR_RESET);
                fflush(stdout);
                continue;
            }
        }

        // Handle local commands (help, exit remain the same)
        if (strcmp(input_buffer, "/help") == 0) {
            print_help_menu();
            continue;
        }

        if (strcmp(input_buffer, "/exit") == 0) {
            print_status_message("[INFO] Sending disconnect request to server...", ANSI_COLOR_INFO);
            if (send(socket_fd, input_buffer, strlen(input_buffer), 0) < 0) {
                print_status_message("[WARNING] Failed to send disconnect message to server", ANSI_COLOR_WARNING);
            }
            is_running = 0;
            break;
        }
        
        // Modified sendfile command handling with blocking
        if (strncmp(input_buffer, "/sendfile ", 10) == 0) {
            char recipient[32], filename[128];
            if (sscanf(input_buffer + 10, " %127s %31s ", filename, recipient) == 2) {
                // Set file transfer in progress flag
                pthread_mutex_lock(&file_transfer_progress_mutex);
                file_transfer_in_progress = 1;
                pthread_mutex_unlock(&file_transfer_progress_mutex);
                
                // Call blocking file transfer function
                int result = handle_send_file(socket_fd, recipient, filename);
                
                // Clear file transfer in progress flag
                pthread_mutex_lock(&file_transfer_progress_mutex);
                file_transfer_in_progress = 0;
                pthread_mutex_unlock(&file_transfer_progress_mutex);
                
                if (result) {
                    printf(ANSI_COLOR_PROMPT "\n> " ANSI_COLOR_RESET);
                    fflush(stdout);
                }
            } else {
                print_status_message("[ERROR] Invalid syntax. Usage: /sendfile <filename> <recipient> ", ANSI_COLOR_ERROR);
            }
            continue;
        }
        
        // Provide feedback for common commands
        if (strncmp(input_buffer, "/join ", 6) == 0) {

           if (strlen(input_buffer) < 7 || !validate_room_name(input_buffer + 6)) {
                print_status_message("[ERROR] Invalid room name. Use alphanumeric characters and underscores only (1-32 chars)", ANSI_COLOR_ERROR);
                continue;
            }
        

            print_status_message("[INFO] Attempting to join room...", ANSI_COLOR_INFO);
        } else if (strncmp(input_buffer, "/whisper ", 9) == 0) {
            print_status_message("[INFO] Sending private message...", ANSI_COLOR_INFO);
        } else if (strncmp(input_buffer, "/broadcast ", 11) == 0) {
            print_status_message("[INFO] Broadcasting message to room...", ANSI_COLOR_INFO);
        } else if (strcmp(input_buffer, "/list") == 0) {
            print_status_message("[INFO] Requesting user list...", ANSI_COLOR_INFO);
        } else if (strcmp(input_buffer, "/leave") == 0) {
            print_status_message("[INFO] Leaving current room...", ANSI_COLOR_INFO);
        }

        // Send command/message to server
        strcat(input_buffer, "\n"); // Add newline for server parsing
        if (send(socket_fd, input_buffer, strlen(input_buffer), 0) < 0) {
            print_status_message("[ERROR] Failed to send message to server", ANSI_COLOR_ERROR);
            break;
        }
    }
}

int validate_room_name(const char *room_name) {
    size_t len = strlen(room_name);
    if (len < 1 || len > 32) {
        return 0; // Invalid length
    }
    for (size_t i = 0; i < len; i++) {
        if (!isalnum(room_name[i]) && room_name[i] != '_') {
            return 0; // Invalid character
        }
    }
    return 1; // Valid room name
}

// Wait for server to signal readiness for file transfer
int wait_for_ready(int timeout_seconds) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_seconds;
    
    int rc = 0;
    int success = 0;
    
    pthread_mutex_lock(&ready_mutex);
    

    
    while (!ready_for_file && rc != ETIMEDOUT) {
        rc = pthread_cond_timedwait(&ready_cond, &ready_mutex, &ts);
        
        // Check if the client is shutting down
        if (!is_running) {
            pthread_mutex_unlock(&ready_mutex);
            return 0; // Client is shutting down
        }
    }
   
   
    // Check the result after the loop
    if (ready_for_file == 1) {
      
        ready_for_file = 0; // Reset for next use
        success = 1; // Success: ready signal received
    } 
    else if(rc == ETIMEDOUT) {
        print_status_message("[ERROR] Timeout waiting for server readiness for file transfer", ANSI_COLOR_ERROR);
        success = 0; // Timeout occurred
    }
    else {
        success = -1; // Timeout or other error
    }
    
    pthread_mutex_unlock(&ready_mutex);
    return success;
}

int wait_for_file_transfer(int timeout_seconds) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_seconds;

    int rc = 0;
    int success = 0;

    pthread_mutex_lock(&file_transfer_mutex);

    // Wait until file_transfer_finished is set or timeout occurs
    while (!file_transfer_finished && rc != ETIMEDOUT) {
        rc = pthread_cond_timedwait(&file_transfer_cond, &file_transfer_mutex, &ts);

        // Check if the client is shutting down
        if (!is_running) {
            pthread_mutex_unlock(&file_transfer_mutex);
            return 0; // Client is shutting down
        }
    }

    // Check the result after the loop
    if (file_transfer_finished) {
        file_transfer_finished = 0; // Reset for next use
        success = 1; // Success: file transfer finished
    } else {
        success = 0; // Timeout or other error
    }

    pthread_mutex_unlock(&file_transfer_mutex);
    return success;
}

// Clean up resources
void cleanup_resources(int socket_fd) {
    is_running = 0;
    

    
    // Cleanup mutexes and condition variables
    pthread_mutex_destroy(&ready_mutex);
    pthread_cond_destroy(&ready_cond);
    pthread_mutex_destroy(&file_transfer_mutex);
    pthread_cond_destroy(&file_transfer_cond);
    pthread_mutex_destroy(&socket_mutex);
    pthread_mutex_destroy(&prompt_mutex);
    pthread_mutex_destroy(&file_transfer_progress_mutex);
    
    close(socket_fd);
    print_status_message("[SYSTEM] Disconnected from server. Goodbye!", ANSI_COLOR_SUCCESS);
}
// Main function
int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, ANSI_COLOR_ERROR "[ERROR] Usage: %s <server_ip> <port>" ANSI_COLOR_RESET "\n", argv[0]);
        exit(1);
    }
    
    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    
    // Print startup banner
    printf(ANSI_COLOR_SYSTEM "╔══════════════════════════════════════════════════════════╗\n");
    printf("║                   AbduChat v1.0                          ║\n");
    printf("║                                                          ║\n");
    printf("╚══════════════════════════════════════════════════════════╝" ANSI_COLOR_RESET "\n\n");
    
    // Initialize signal handlers
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        print_status_message("[ERROR] Failed to set up signal handler", ANSI_COLOR_ERROR);
        exit(EXIT_FAILURE);
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        print_status_message("[ERROR] Failed to set up signal handler", ANSI_COLOR_ERROR);
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
        print_status_message("[ERROR] Failed to create response handler thread", ANSI_COLOR_ERROR);
        close(socket_fd);
        exit(1);
    }
    
    // Detach the thread so it cleans up automatically when it finishes
    if (pthread_detach(server_response_thread) != 0) {
        print_status_message("[WARNING] Failed to detach response handler thread", ANSI_COLOR_WARNING);
    } 
    
    
    
    // Process user input
    process_user_input(socket_fd);
    
    // Cleanup and exit
    cleanup_resources(socket_fd);
    return 0;
}