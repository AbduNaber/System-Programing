// Compile: gcc chatserver.c -o chatserver -lpthread
#include "../shared/chatDefination.h"
#include <time.h>
#include <stdarg.h>
#include <ctype.h>

int running = 1;

client_info_t clients[MAX_CLIENTS];
room_t rooms[MAX_GROUPS];
int server_fd;
int room_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t rooms_mutex = PTHREAD_MUTEX_INITIALIZER;

FileQueue file_queue;

void *handle_client_read(void *arg);
void broadcast_to_room(char *msg, char *room_name, int sender_socket);
void send_private_message(char *msg, char *target_username, int sender_socket);
int find_client_by_socket(int socket);
int find_client_by_username(char *username);
int find_or_create_room(char *room_name);
void remove_client_from_room(int client_index);
void add_client_to_room(int client_index, char *room_name);
void handle_command(int client_socket, char *message);
void log_event(const char *format, ...);
void *handle_file_transfer(void *arg);
int relay_file();
int validate_file_type(const char *filename);
int validate_room_name(const char *room_name);
void filequeue_init(FileQueue *q);
int filequeue_enqueue(FileQueue *q, FileMeta *meta);
int filequeue_dequeue(FileQueue *q, FileMeta *meta);
int filequeue_start_transfer(FileQueue *q, FileMeta *meta);
void filequeue_finish_transfer(FileQueue *q);
int filequeue_try_start_next(FileQueue *q, FileMeta *meta);

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        log_event("[SHUTDOWN] %s received. Disconnecting clients, saving logs", 
                  signal == SIGINT ? "SIGINT" : "SIGTERM");
        printf("\nServer shutting down...\n");
        
        running = 0;  // Set running to 0 first to stop accept loop
        
        // Close server socket to interrupt accept()
        shutdown(server_fd, SHUT_RDWR);
        close(server_fd);
        
        int disconnected_clients = 0;
        pthread_mutex_lock(&clients_mutex);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].active) {
                send(clients[i].socket, "[SERVER] Server is shutting down. Disconnecting...\n", 50, 0);
                shutdown(clients[i].socket, SHUT_RDWR);
                close(clients[i].socket);
                clients[i].active = 0;
                disconnected_clients++;
                log_event("[SHUTDOWN] Client %d (%s) forcibly disconnected", 
                         i, clients[i].username[0] ? clients[i].username : "unnamed");
            }
        }
        pthread_mutex_unlock(&clients_mutex);
        
        log_event("[SHUTDOWN] Server socket closed, %d clients disconnected", disconnected_clients);
    }
}

int main(int argc, char *argv[]) {
    log_event("[STARTUP] Chat server starting up");
    
    if (argc != 2) {
        log_event("[ERROR] Invalid arguments provided, expected port number");
        fprintf(stderr, "Usage: ./%s <port>\n", argv[0]);
        exit(1);
    }

    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  // Don't use SA_RESTART so accept() can be interrupted

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        log_event("[ERROR] Failed to set SIGINT handler");
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        log_event("[ERROR] Failed to set SIGTERM handler");
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    log_event("[STARTUP] Signal handlers configured");

    int port = atoi(argv[1]);
    log_event("[STARTUP] Server port set to %d", port);

    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len;
    
    // Initialize clients array
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].socket = -1;
        clients[i].active = 0;
        memset(clients[i].username, 0, MAX_USERNAME_LENGTH);
        memset(clients[i].current_room, 0, MAX_GROUP_NAME_LENGTH);
    }
    log_event("[STARTUP] Client array initialized (%d slots)", MAX_CLIENTS);
    
    // Initialize rooms array
    for (int i = 0; i < MAX_GROUPS; i++) {
        memset(rooms[i].name, 0, MAX_GROUP_NAME_LENGTH);
        rooms[i].member_count = 0;
        for (int j = 0; j < MAX_GROUP_MEMBERS; j++) {
            rooms[i].members[j] = -1;
        }
    }
    log_event("[STARTUP] Room array initialized (%d max rooms)", MAX_GROUPS);
    
    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        log_event("[ERROR] Socket creation failed");
        perror("Socket creation failed");
        exit(1);
    }
    log_event("[STARTUP] Server socket created (fd: %d)", server_fd);
    
    // Set socket options to reuse address
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        log_event("[ERROR] Failed to set socket options");
        perror("Setsockopt failed");
        exit(1);
    }
    log_event("[STARTUP] Socket options configured (SO_REUSEADDR)");
    
    // Setup server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    
    // Bind
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        log_event("[ERROR] Bind failed on port %d", port);
        perror("Bind failed");
        exit(1);
    }
    log_event("[STARTUP] Socket bound to port %d", port);
    
    //setup file transfer queue
    filequeue_init(&file_queue);
    log_event("[STARTUP] File transfer queue initialized");

    // Listen
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        log_event("[ERROR] Listen failed");
        perror("Listen failed");
        exit(1);
    }
    printf("Server listening on ip 127.0.0.1 on port %d...\n", port);
    log_event("[STARTUP] Server listening on ip 127.0.0.1 on port %d, ready for connections", port);

    while (running) {
        addr_len = sizeof(client_addr);
        int client_socket = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_socket < 0) {
            if (running && errno != EINTR) {
                log_event("[ERROR] Accept failed");
                perror("Accept failed");
            }
            continue;
        }
        
        if (!running) {
            close(client_socket);
            break;
        }
        
        char client_ip[INET_ADDRSTRLEN];
        strcpy(client_ip, inet_ntoa(client_addr.sin_addr));
        int client_port = ntohs(client_addr.sin_port);
        
        log_event("[CONNECTION] New connection from %s:%d (socket fd: %d)", 
                  client_ip, client_port, client_socket);
        
        // Find available slot for client
        pthread_mutex_lock(&clients_mutex);
        int client_index = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].active) {
                client_index = i;
                clients[i].socket = client_socket;
                clients[i].active = 1;
                memset(clients[i].username, 0, MAX_USERNAME_LENGTH);
                memset(clients[i].current_room, 0, MAX_GROUP_NAME_LENGTH);
                break;
            }
        }
        pthread_mutex_unlock(&clients_mutex);
        
        if (client_index == -1) {
            log_event("[CONNECTION_REJECTED] Max clients reached, rejecting %s:%d", 
                      client_ip, client_port);
            printf("Max clients reached. Rejecting connection.\n");
            send(client_socket, "Server full. Try again later.\n", 31, 0);
            close(client_socket);
            continue;
        }
        
        printf("Client connected from %s:%d (slot %d)\n", client_ip, client_port, client_index);
        log_event("[CONNECTION_ACCEPTED] Client assigned to slot %d from %s:%d", 
                  client_index, client_ip, client_port);
        send(client_socket, "SUCCESS_LOGIN", 14, 0);
        
        pthread_t client_handler_thread;
        int *client_index_ptr = malloc(sizeof(int));
        *client_index_ptr = client_index;
        
        if (pthread_create(&client_handler_thread, NULL, handle_client_read, client_index_ptr) != 0) {
            log_event("[ERROR] Failed to create thread for client %d", client_index);
            perror("Failed to create thread");
            close(client_socket);
            pthread_mutex_lock(&clients_mutex);
            clients[client_index].active = 0;
            pthread_mutex_unlock(&clients_mutex);
            free(client_index_ptr);
            continue;
        }
        
        pthread_detach(client_handler_thread);
    }
    
    close(server_fd);
    log_event("[SHUTDOWN] Server shutdown complete");
    return 0;
}

void *handle_client_read(void *arg) {
    int client_index = *(int *)arg;
    free(arg);  // Free the allocated memory immediately
    
    pthread_mutex_lock(&clients_mutex);
    int client_socket = clients[client_index].socket;
    int is_active = clients[client_index].active;
    pthread_mutex_unlock(&clients_mutex);
    
    if (!is_active) {
        return NULL;
    }
    
    char buffer[BUFFER_SIZE];
    int bytes_read;
    
    while (1) {
        // Check if client is still active before reading
        pthread_mutex_lock(&clients_mutex);
        if (!clients[client_index].active) {
            pthread_mutex_unlock(&clients_mutex);
            break;
        }
        pthread_mutex_unlock(&clients_mutex);
        
        bytes_read = read(client_socket, buffer, BUFFER_SIZE - 1);
        if (bytes_read <= 0) {
            break;
        }
        
        buffer[bytes_read] = '\0';
        
        // Remove newline if present
        char *newline = strchr(buffer, '\n');
        if (newline) *newline = '\0';
        
        pthread_mutex_lock(&clients_mutex);
        log_event("[MESSAGE_RECEIVED] Client %d (%s): %s [%d bytes]", 
                  client_index, 
                  clients[client_index].username[0] ? clients[client_index].username : "unnamed",
                  buffer, bytes_read);
        pthread_mutex_unlock(&clients_mutex);
        
        printf("Client %d: %s\n", client_index, buffer);
        
        if (buffer[0] == '/') {
            log_event("[COMMAND] Processing command from client %d: %s", client_index, buffer);
            handle_command(client_socket, buffer);
        }
        else if (strncmp(buffer, "FILE_EXISTS", 11) == 0) {
            log_event("[FILE] Conflict: '%s' received twice -> renamed by client", buffer + 12);
        }
        else {
            // if not a command, warn the user for entering a command
            char response[BUFFER_SIZE*2];
            snprintf(response, sizeof(response), "Unknown command: '%s'. Type /help for available commands.", buffer);
            send(client_socket, response, strlen(response), 0);
            log_event("[UNKNOWN_COMMAND] Client %d sent invalid command: %s", client_index, buffer);
        }
    }
    
    // Client disconnected
    pthread_mutex_lock(&clients_mutex);
    log_event("[DISCONNECT] Client %d (%s) disconnected", 
              client_index, 
              clients[client_index].username[0] ? clients[client_index].username : "unnamed");
    printf("Client %d disconnected\n", client_index);
    
    pthread_mutex_lock(&rooms_mutex);
    remove_client_from_room(client_index);
    pthread_mutex_unlock(&rooms_mutex);
    
    clients[client_index].active = 0;
    close(clients[client_index].socket);
    clients[client_index].socket = -1;
    pthread_mutex_unlock(&clients_mutex);
    
    log_event("[CLEANUP] Client %d resources cleaned up", client_index);
    return NULL;
}

int validate_file_type(const char *filename) {
    const char *valid_extensions[] = {".txt", ".pdf", ".jpg", ".png"};
    int num_extensions = 4;
    const char *ext = strrchr(filename, '.');
    if (!ext) return 0; 
    for (int i = 0; i < num_extensions; i++) {
        if (strcasecmp(ext, valid_extensions[i]) == 0) {
            return 1; 
        }
    }
    return 0; 
}

int validate_room_name(const char *room_name) {
    if (!room_name || strlen(room_name) == 0) {
        return 0; // Empty room name
    }
    
    size_t len = strlen(room_name);
    if (len > 32) {
        return 0; // Too long
    }
    
    // Check each character is alphanumeric
    for (size_t i = 0; i < len; i++) {
        if (!isalnum(room_name[i])) {
            return 0; // Non-alphanumeric character found
        }
    }
    
    return 1; // Valid room name
}

void handle_command(int client_socket, char *message) {
    int client_index = find_client_by_socket(client_socket);
    if (client_index == -1) {
        log_event("[ERROR] Could not find client by socket %d", client_socket);
        return;
    }
    
    char message_copy[BUFFER_SIZE];
    strncpy(message_copy, message, BUFFER_SIZE - 1);
    message_copy[BUFFER_SIZE - 1] = '\0';
    
    char *cmd = strtok(message_copy, " ");
    char response[BUFFER_SIZE];
    
    log_event("[COMMAND_PARSE] Client %d executing command: %s", client_index, cmd);
    
    if (strcmp(cmd, "/username") == 0) {
        char *username = strtok(NULL, " ");
        if (username && strlen(username) > 0) {
            pthread_mutex_lock(&clients_mutex);
            // Check if username already exists
            if (find_client_by_username(username) != -1) {
                snprintf(response, sizeof(response), "ALREADY_TAKEN");
                log_event("[USERNAME_TAKEN] Client %d tried to use taken username: %s", 
                         client_index, username);
            } else {
                char old_username[MAX_USERNAME_LENGTH];
                strcpy(old_username, clients[client_index].username);
                strncpy(clients[client_index].username, username, MAX_USERNAME_LENGTH - 1);
                clients[client_index].username[MAX_USERNAME_LENGTH - 1] = '\0';
                snprintf(response, sizeof(response), "SET_USERNAME");
                log_event("[USERNAME_SET] Client %d changed username from '%s' to '%s'", 
                         client_index, 
                         old_username[0] ? old_username : "unnamed", 
                         username);
            }
            pthread_mutex_unlock(&clients_mutex);
        } else {
            strcpy(response, "[SERVER] Usage: /username <name>");
            log_event("[COMMAND_ERROR] Client %d sent invalid username command", client_index);
        }
        send(client_socket, response, strlen(response), 0);
        
    } else if (strcmp(cmd, "/join") == 0) {
        char *room_name = strtok(NULL, " ");
        if (room_name && strlen(room_name) > 0) {
            // Validate room name
            if (!validate_room_name(room_name)) {
                strcpy(response, "[SERVER] Invalid room name. Must be alphanumeric, max 32 chars, no spaces/special chars");
                log_event("[COMMAND_ERROR] Client %d tried to join invalid room name: '%s'", 
                         client_index, room_name);
                send(client_socket, response, strlen(response), 0);
                return;
            }
            
            pthread_mutex_lock(&clients_mutex);
            pthread_mutex_lock(&rooms_mutex);
            
            char old_room[MAX_GROUP_NAME_LENGTH];
            strcpy(old_room, clients[client_index].current_room);
            
            // Remove from current room
            remove_client_from_room(client_index);
            if (old_room[0]) {
                log_event("[ROOM_LEAVE] Client %d (%s) left room '%s'", 
                         client_index, clients[client_index].username, old_room);
            }
            
            // Add to new room
            add_client_to_room(client_index, room_name);
            strncpy(clients[client_index].current_room, room_name, MAX_GROUP_NAME_LENGTH - 1);
            clients[client_index].current_room[MAX_GROUP_NAME_LENGTH - 1] = '\0';
            snprintf(response, sizeof(response), "[SERVER] Joined room '%s'", room_name);
            log_event("[ROOM_JOIN] Client %d (%s) joined room '%s'", 
                     client_index, clients[client_index].username, room_name);
            
            pthread_mutex_unlock(&rooms_mutex);
            pthread_mutex_unlock(&clients_mutex);
        } else {
            strcpy(response, "[SERVER] Usage: /join <room_name>");
            log_event("[COMMAND_ERROR] Client %d sent invalid join command", client_index);
        }
        send(client_socket, response, strlen(response), 0);
        
    } else if (strcmp(cmd, "/broadcast") == 0) {
        // Safely extract message after "/broadcast "
        if (strlen(message) > 11) {  // "/broadcast " is 11 characters
            char *msg = message + 11;
            
            pthread_mutex_lock(&clients_mutex);
            char current_room[MAX_GROUP_NAME_LENGTH];
            char username[MAX_USERNAME_LENGTH];
            strcpy(current_room, clients[client_index].current_room);
            strcpy(username, clients[client_index].username);
            pthread_mutex_unlock(&clients_mutex);
            
            if (strlen(current_room) > 0) {
                char formatted_msg[BUFFER_SIZE + 100];
                snprintf(formatted_msg, sizeof(formatted_msg), "[BROADCAST] %s: %s", username, msg);
                
                log_event("[BROADCAST_START] Client %d (%s) broadcasting to room '%s': %s", 
                         client_index, username, current_room, msg);
                
                broadcast_to_room(formatted_msg, current_room, client_socket);
                strcpy(response, "[SERVER] Message broadcasted");
                
                log_event("[BROADCAST_COMPLETE] Message from %s broadcasted to room '%s'", 
                         username, current_room);
            } else {
                strcpy(response, "[SERVER] You must join a room first");
                log_event("[BROADCAST_ERROR] Client %d tried to broadcast without joining room", 
                         client_index);
            }
        } else {
            strcpy(response, "[SERVER] Usage: /broadcast <message>");
            log_event("[COMMAND_ERROR] Client %d sent empty broadcast command", client_index);
        }
        send(client_socket, response, strlen(response), 0);
        
    } else if (strcmp(cmd, "/leave") == 0) {
        pthread_mutex_lock(&clients_mutex);
        pthread_mutex_lock(&rooms_mutex);
        if (strlen(clients[client_index].current_room) > 0) {
            char old_room[MAX_GROUP_NAME_LENGTH];
            strcpy(old_room, clients[client_index].current_room);
            remove_client_from_room(client_index);
            memset(clients[client_index].current_room, 0, MAX_GROUP_NAME_LENGTH);
            snprintf(response, sizeof(response), "ROOM_LEFT");
            log_event("[ROOM_LEAVE] Client %d (%s) left room '%s'", 
                    client_index, clients[client_index].username, old_room);
        } else {
            strcpy(response, "[SERVER] You are not in a room");
            log_event("[COMMAND_ERROR] Client %d (%s) tried to leave without being in a room", 
                    client_index, clients[client_index].username);
        }
        pthread_mutex_unlock(&rooms_mutex);
        pthread_mutex_unlock(&clients_mutex);
        send(client_socket, response, strlen(response), 0);
    } 
    
    else if (strcmp(cmd, "/whisper") == 0) {
        char *target_user = strtok(NULL, " ");
        char *msg = strtok(NULL, ""); // Get rest of the message
        if (target_user && msg) {
            pthread_mutex_lock(&clients_mutex);
            char sender_username[MAX_USERNAME_LENGTH];
            strcpy(sender_username, clients[client_index].username);
            pthread_mutex_unlock(&clients_mutex);
            
            char formatted_msg[BUFFER_SIZE + 100];
            snprintf(formatted_msg, sizeof(formatted_msg), "[WHISPER from %s]: %s", 
                    sender_username, msg);
            
            log_event("[WHISPER_START] Client %d (%s) whispering to '%s': %s", 
                     client_index, sender_username, target_user, msg);
            
            send_private_message(formatted_msg, target_user, client_socket);
            snprintf(response, sizeof(response), "[SERVER] Whisper sent to %s", target_user);
            
            log_event("[WHISPER_COMPLETE] Whisper from %s to %s processed", 
                     sender_username, target_user);
        } else {
            strcpy(response, "[SERVER] Usage: /whisper <username> <message>");
            log_event("[COMMAND_ERROR] Client %d sent invalid whisper command", client_index);
        }
        send(client_socket, response, strlen(response), 0);

    } else if (strcmp(cmd, "/sendfile") == 0) {
        char recipient[32] = {0}, filename[128] = {0}, size_buffer[64] = {0};
        char *args = message + 10;  // Skip "/sendfile "
        
        if (strlen(message) > 10) {
            sscanf(args, "%127s %31s %63s", filename, recipient, size_buffer);
        }

        log_event("[FILE_TRANSFER_START] Client %d (%s) initiating file transfer to '%s', file: %s", 
                 client_index, clients[client_index].username, recipient, filename);

        if (strlen(recipient) == 0 || strlen(filename) == 0) {
            send(client_socket, "[SERVER] Usage: /sendfile <recipient> <filename> <size>\n", 56, 0);
            log_event("[FILE_TRANSFER_ERROR] Client %d sent invalid file transfer command", client_index);
            return;
        }

        if (!validate_file_type(filename)) {
            send(client_socket, "INVALID_FILE_TYPE", 18, 0);
            log_event("[FILE_TRANSFER_ERROR] Invalid file type '%s' from %s", filename, clients[client_index].username);
            return;
        }

        // Find recipient first
        int recp_idx = find_client_by_username(recipient);
        if (recp_idx < 0) {
            send(client_socket, "RECIPIENT_NOT_FOUND", 20, 0);
            log_event("[FILE_TRANSFER_ERROR] Recipient '%s' not found for file from %s", 
                     recipient, clients[client_index].username);
            return;
        }

        // Check if recipient is online
        pthread_mutex_lock(&clients_mutex);
        if (!clients[recp_idx].active) {
            pthread_mutex_unlock(&clients_mutex);
            send(client_socket, "RECIPIENT_OFFLINE\n", 19, 0);
            log_event("[FILE_TRANSFER_ERROR] Recipient '%s' is offline", recipient);
            return;
        }
        int recipient_socket = clients[recp_idx].socket;
        pthread_mutex_unlock(&clients_mutex);

        // Get filesize
        size_t filesize = atol(size_buffer);
        
        log_event("[FILE_TRANSFER] File metadata received - size: %zu bytes", filesize);

        // Check file size limit
        if (filesize > MAX_FILE_SIZE) {
            send(client_socket, "FILE_SIZE_EXCEEDS_LIMIT", 24, 0);
            log_event("[FILE_TRANSFER_ERROR] File size %zu exceeds limit for %s", filesize, clients[client_index].username);
            return;
        }

        // Create file metadata for queue
        FileMeta file_meta;
        pthread_mutex_lock(&clients_mutex);
        strncpy(file_meta.sender, clients[client_index].username, sizeof(file_meta.sender) - 1);
        file_meta.sender[sizeof(file_meta.sender) - 1] = '\0';
        pthread_mutex_unlock(&clients_mutex);
        
        strncpy(file_meta.recipient, recipient, sizeof(file_meta.recipient) - 1);
        file_meta.recipient[sizeof(file_meta.recipient) - 1] = '\0';
        strncpy(file_meta.filename, filename, sizeof(file_meta.filename) - 1);
        file_meta.filename[sizeof(file_meta.filename) - 1] = '\0';
        file_meta.filesize = filesize;
        file_meta.sender_socket = client_socket;
        file_meta.recipient_socket = recipient_socket;
        
        // Try to start transfer immediately or queue it
        if (filequeue_start_transfer(&file_queue, &file_meta)) {
            // Transfer can start immediately
            log_event("[FILE_TRANSFER] Starting immediate transfer: %s -> %s", 
                     file_meta.sender, recipient);
            printf("[FILE_TRANSFER] Starting immediate transfer: %s -> %s\n", 
                   file_meta.sender, recipient);

            send(client_socket, "READY_FOR_FILE", 15, 0);
            
            // Inform recipient 
            char filemeta[FILE_META_MSG_LEN];
            snprintf(filemeta, sizeof(filemeta), "INCOMING_FILE %s %s %zu\n", 
                    file_meta.sender, filename, filesize);
            send(recipient_socket, filemeta, strlen(filemeta), 0);

            // Start transfer in separate thread
            pthread_t transfer_thread;
            FileMeta *meta_ptr = malloc(sizeof(FileMeta));
            if (meta_ptr == NULL) {
                log_event("[FILE_TRANSFER_ERROR] Failed to allocate memory for transfer");
                send(client_socket, "[SERVER] File transfer failed.\n", 32, 0);
                filequeue_finish_transfer(&file_queue);
                return;
            }
            *meta_ptr = file_meta;
            
            if (pthread_create(&transfer_thread, NULL, handle_file_transfer, meta_ptr) != 0) {
                log_event("[FILE_TRANSFER_ERROR] Failed to create transfer thread");
                send(client_socket, "[SERVER] File transfer failed.\n", 32, 0);
                filequeue_finish_transfer(&file_queue);
                free(meta_ptr);
            } else {
                pthread_detach(transfer_thread);
            }
        } else {
            // Transfer was queued - handled inside filequeue_start_transfer
            log_event("[FILE_TRANSFER] Transfer queued for %s -> %s", file_meta.sender, recipient);
        }
        
    } else if (strcmp(cmd, "/list") == 0) {
        pthread_mutex_lock(&clients_mutex);
        char current_room[MAX_GROUP_NAME_LENGTH];
        strcpy(current_room, clients[client_index].current_room);
        pthread_mutex_unlock(&clients_mutex);
        
        log_event("[LIST_COMMAND] Client %d (%s) requesting user list for room '%s'", 
                 client_index, clients[client_index].username, current_room);
        
        // List users in current room
        if (strlen(current_room) > 0) {
            pthread_mutex_lock(&clients_mutex);
            pthread_mutex_lock(&rooms_mutex);
            
            int room_index = find_or_create_room(current_room);
            strcpy(response, "[SERVER] Users in room: ");
            
            int user_count = 0;
            for (int i = 0; i < rooms[room_index].member_count; i++) {
                int member_index = rooms[room_index].members[i];
                if (member_index >= 0 && clients[member_index].active) {
                    strcat(response, clients[member_index].username);
                    strcat(response, " ");
                    user_count++;
                }
            }
            
            log_event("[LIST_RESULT] Room '%s' has %d active users", current_room, user_count);
            
            pthread_mutex_unlock(&rooms_mutex);
            pthread_mutex_unlock(&clients_mutex);
        } else {
            strcpy(response, "[SERVER] You must join a room first");
            log_event("[LIST_ERROR] Client %d tried to list users without joining room", 
                     client_index);
        }
        send(client_socket, response, strlen(response), 0);
        
    } else if (strcmp(cmd, "/exit") == 0) {
        strcpy(response, "[SERVER] Goodbye!");
        send(client_socket, response, strlen(response), 0);
        pthread_mutex_lock(&clients_mutex);
        clients[client_index].active = 0;
        pthread_mutex_unlock(&clients_mutex);
        log_event("[EXIT] Client %d (%s) disconnected voluntarily", 
                 client_index, clients[client_index].username);
        
    } else if (strcmp(cmd, "/help") == 0) {
        strcpy(response, "[SERVER] Available commands:\n"
                        "/username <name> - Set your username\n"
                        "/join <room> - Join a chat room\n"
                        "/leave - Leave current room\n"
                        "/broadcast <msg> - Send message to room\n"
                        "/whisper <user> <msg> - Private message\n"
                        "/sendfile <user> <file> <size> - Send file\n"
                        "/list - List users in current room\n"
                        "/exit - Disconnect from server");
        send(client_socket, response, strlen(response), 0);
        log_event("[HELP] Client %d requested help", client_index);
        
    } else {
        strcpy(response, "[SERVER] Unknown command. Type /help for available commands.");
        send(client_socket, response, strlen(response), 0);
        log_event("[UNKNOWN_COMMAND] Client %d sent unrecognized command: %s", client_index, cmd);
    }
}

void broadcast_to_room(char *msg, char *room_name, int sender_socket) {
    pthread_mutex_lock(&clients_mutex);
    pthread_mutex_lock(&rooms_mutex);
    
    int room_index = -1;
    for (int i = 0; i < room_count; i++) {
        if (strcmp(rooms[i].name, room_name) == 0) {
            room_index = i;
            break;
        }
    }
    
    if (room_index != -1) {
        int messages_sent = 0;
        for (int i = 0; i < rooms[room_index].member_count; i++) {
            int member_index = rooms[room_index].members[i];
            if (member_index >= 0 && clients[member_index].active && 
                clients[member_index].socket != sender_socket) {
                send(clients[member_index].socket, msg, strlen(msg), 0);
                messages_sent++;
                log_event("[BROADCAST_DELIVERY] Message delivered to client %d (%s) in room '%s'", 
                         member_index, clients[member_index].username, room_name);
            }
        }
        log_event("[BROADCAST_SUMMARY] Broadcast in room '%s' delivered to %d clients", 
                 room_name, messages_sent);
    } else {
        log_event("[BROADCAST_ERROR] Room '%s' not found for broadcast", room_name);
    }
    
    pthread_mutex_unlock(&rooms_mutex);
    pthread_mutex_unlock(&clients_mutex);
}

void send_private_message(char *msg, char *target_username, int sender_socket) {
    pthread_mutex_lock(&clients_mutex);
    
    int target_index = find_client_by_username(target_username);
    if (target_index != -1 && clients[target_index].active) {
        send(clients[target_index].socket, msg, strlen(msg), 0);
        log_event("[WHISPER_DELIVERY] Private message delivered to %s (client %d)", 
                 target_username, target_index);
    } else {
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, sizeof(error_msg), "[SERVER] User '%s' not found or offline", target_username);
        send(sender_socket, error_msg, strlen(error_msg), 0);
        log_event("[WHISPER_ERROR] Target user '%s' not found or offline", target_username);
    }
    
    pthread_mutex_unlock(&clients_mutex);
}

int find_client_by_socket(int socket) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].socket == socket) {
            return i;
        }
    }
    log_event("[SEARCH_ERROR] Client not found by socket %d", socket);
    return -1;
}

int find_client_by_username(char *username) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && strcmp(clients[i].username, username) == 0) {
            log_event("[SEARCH_SUCCESS] Found client %d by username '%s'", i, username);
            return i;
        }
    }
    log_event("[SEARCH_MISS] Client not found by username '%s'", username);
    return -1;
}

int find_or_create_room(char *room_name) {
    // Find existing room
    for (int i = 0; i < room_count; i++) {
        if (strcmp(rooms[i].name, room_name) == 0) {
            log_event("[ROOM_FOUND] Room '%s' found at index %d", room_name, i);
            return i;
        }
    }
    
    // Create new room if space available
    if (room_count < MAX_GROUPS) {
        strncpy(rooms[room_count].name, room_name, MAX_GROUP_NAME_LENGTH - 1);
        rooms[room_count].name[MAX_GROUP_NAME_LENGTH - 1] = '\0';
        rooms[room_count].member_count = 0;
        for (int i = 0; i < MAX_GROUP_MEMBERS; i++) {
            rooms[room_count].members[i] = -1;
        }
        log_event("[ROOM_CREATED] New room '%s' created at index %d", room_name, room_count);
        return room_count++;
    }
    
    log_event("[ROOM_ERROR] Cannot create room '%s' - maximum rooms reached (%d)", 
             room_name, MAX_GROUPS);
    return -1; // No space for new room
}

void remove_client_from_room(int client_index) {
    if (strlen(clients[client_index].current_room) == 0) return;
    
    char room_name[MAX_GROUP_NAME_LENGTH];
    strcpy(room_name, clients[client_index].current_room);
    
    int room_index = -1;
    for (int i = 0; i < room_count; i++) {
        if (strcmp(rooms[i].name, clients[client_index].current_room) == 0) {
            room_index = i;
            break;
        }
    }
    
    if (room_index != -1) {
        for (int i = 0; i < rooms[room_index].member_count; i++) {
            if (rooms[room_index].members[i] == client_index) {
                // Shift remaining members
                for (int j = i; j < rooms[room_index].member_count - 1; j++) {
                    rooms[room_index].members[j] = rooms[room_index].members[j + 1];
                }
                rooms[room_index].member_count--;
                rooms[room_index].members[rooms[room_index].member_count] = -1;
                log_event("[ROOM_REMOVE] Client %d (%s) removed from room '%s', %d members remaining", 
                         client_index, clients[client_index].username, room_name, 
                         rooms[room_index].member_count);
                break;
            }
        }
    } else {
        log_event("[ROOM_ERROR] Could not find room '%s' to remove client %d", 
                 room_name, client_index);
    }
    
    memset(clients[client_index].current_room, 0, MAX_GROUP_NAME_LENGTH);
}

void add_client_to_room(int client_index, char *room_name) {
    int room_index = find_or_create_room(room_name);
    if (room_index != -1 && rooms[room_index].member_count < MAX_GROUP_MEMBERS) {
        rooms[room_index].members[rooms[room_index].member_count] = client_index;
        rooms[room_index].member_count++;
        log_event("[ROOM_ADD] Client %d (%s) added to room '%s', %d members total", 
                 client_index, clients[client_index].username, room_name, 
                 rooms[room_index].member_count);
    } else {
        if (room_index == -1) {
            log_event("[ROOM_ERROR] Could not create/find room '%s' for client %d", 
                     room_name, client_index);
        } else {
            log_event("[ROOM_ERROR] Room '%s' is full, cannot add client %d (%d/%d)", 
                     room_name, client_index, rooms[room_index].member_count, MAX_GROUP_MEMBERS);
        }
    }
}

int relay_file() {
    log_event("[FILE_RELAY] Simulating file transfer (no actual transfer)");
    // Simulate file transfer delay
    sleep(2);
    return 0;  // Return success
}

void *handle_file_transfer(void *arg) {
    FileMeta *meta = (FileMeta *)arg;
    meta->start_time = time(NULL);
    time_t wait_duration = meta->start_time - meta->enqueue_time;
    
    log_event("[FILE_TRANSFER] Processing transfer: %s -> %s (%s, %zu bytes) after %ld seconds in queue", 
             meta->sender, meta->recipient, meta->filename, meta->filesize, wait_duration);
    
    // Simulate file transfer
    int result = relay_file();
    
    if (result == 0) {
        send(meta->sender_socket, "FILE_TRANSFER_SUCCESS", 22, 0);
        send(meta->recipient_socket, "FILE_TRANSFER_SUCCESS", 22, 0);
        
        log_event("[SEND FILE] '%s' sent from %s to %s (simulated success)", 
                meta->filename, meta->sender, meta->recipient);
    } else {
        send(meta->sender_socket, "FILE_TRANSFER_FAILED", 20, 0);
        send(meta->recipient_socket, "FILE_TRANSFER_FAILED", 20, 0);
        log_event("[SEND FILE] '%s' from %s to %s (simulated failure)", 
                meta->filename, meta->sender, meta->recipient);
    }
    
    // Mark transfer as finished
    filequeue_finish_transfer(&file_queue);
    
    // Try to start next queued transfer
    FileMeta next_meta;
    if (filequeue_try_start_next(&file_queue, &next_meta)) {
        log_event("[FILE_TRANSFER] Starting next queued transfer: %s -> %s", 
                 next_meta.sender, next_meta.recipient);
        
        // Notify sender they can start
        send(next_meta.sender_socket, "READY_FOR_FILE", 15, 0);
        
        // Inform recipient of queued transfer
        char filemeta[FILE_META_MSG_LEN];
        snprintf(filemeta, sizeof(filemeta), "INCOMING_FILE %s %s %zu\n", 
                next_meta.sender, next_meta.filename, next_meta.filesize);
        send(next_meta.recipient_socket, filemeta, strlen(filemeta), 0);
        
        // Start next transfer in new thread
        pthread_t next_transfer_thread;
        FileMeta *next_meta_ptr = malloc(sizeof(FileMeta));
        if (next_meta_ptr == NULL) {
            log_event("[FILE_TRANSFER_ERROR] Failed to allocate memory for queued transfer");
            filequeue_finish_transfer(&file_queue);
        } else {
            *next_meta_ptr = next_meta;
            
            if (pthread_create(&next_transfer_thread, NULL, handle_file_transfer, next_meta_ptr) != 0) {
                log_event("[FILE_TRANSFER_ERROR] Failed to create thread for queued transfer");
                filequeue_finish_transfer(&file_queue);
                free(next_meta_ptr);
            } else {
                pthread_detach(next_transfer_thread);
            }
        }
    }
    
    free(meta);
    return NULL;
}

void log_event(const char *format, ...) {
    int log_fd = open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log_fd == -1) return;
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    
    // Write timestamp
    write(log_fd, time_str, strlen(time_str));
    write(log_fd, " - ", 3);
    
    // Format the variable arguments into a buffer
    va_list args;
    va_start(args, format);
    char buffer[1024];  // Adjust size as needed
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    // Write formatted message and newline
    write(log_fd, buffer, len);
    write(log_fd, "\n", 1);
    
    close(log_fd);
}

void filequeue_init(FileQueue *q) {
    q->front = 0;
    q->rear = 0;
    q->count = 0;
    q->active_transfers = 0;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
    log_event("[FILE_QUEUE] File queue initialized");
}

// Enqueue a file transfer request
int filequeue_enqueue(FileQueue *q, FileMeta *meta) {
    log_event("[FILE_QUEUE] Enqueueing file transfer: %s -> %s (size: %zu)", 
              meta->sender, meta->recipient, meta->filesize);
    
    // Lock was missing here - FIXED
    pthread_mutex_lock(&q->mutex);
    
    if (q->count == MAX_FILE_QUEUE) {
        log_event("[FILE_QUEUE] Queue full, notifying sender");
        send(meta->sender_socket, "FILE_QUEUE_FULL", 16, 0);
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }
    
    meta->enqueue_time = time(NULL);
    q->files[q->rear] = *meta;
    q->rear = (q->rear + 1) % MAX_FILE_QUEUE;
    q->count++;
    
    char wait_msg[BUFFER_SIZE];
    snprintf(wait_msg, sizeof(wait_msg), 
             "[SERVER] File transfer queued. Queue position: %d\n", 
             q->count);
    send(meta->sender_socket, wait_msg, strlen(wait_msg), 0);
    
    log_event("[FILE_QUEUE] File enqueued successfully, queue size: %d", q->count);
    
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
    return 0;
}

int filequeue_start_transfer(FileQueue *q, FileMeta *meta) {
    log_event("[FILE_QUEUE] Attempting to start transfer for %s -> %s", 
             meta->sender, meta->recipient);
    
    pthread_mutex_lock(&q->mutex);
    if (q->active_transfers < MAX_SIMULTANEOUS_TRANSFERS) {
        q->active_transfers++;
        log_event("[FILE_QUEUE] Transfer started immediately, active transfers: %d", 
                 q->active_transfers);
        pthread_mutex_unlock(&q->mutex);
        return 1; // Start transfer immediately
    } else {
        log_event("[FILE_QUEUE] Max concurrent transfers reached (%d), enqueueing", 
                 MAX_SIMULTANEOUS_TRANSFERS);
        pthread_mutex_unlock(&q->mutex);
        // Enqueue for later
        filequeue_enqueue(q, meta);
        return 0; // Queued
    }
}

void filequeue_finish_transfer(FileQueue *q) {
    pthread_mutex_lock(&q->mutex);
    if (q->active_transfers > 0) {
        q->active_transfers--;
    }
    log_event("[FILE_QUEUE] Transfer finished, active transfers: %d", q->active_transfers);
    
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
}

int filequeue_try_start_next(FileQueue *q, FileMeta *meta) {
    log_event("[FILE_QUEUE] Trying to start next queued transfer");
    
    pthread_mutex_lock(&q->mutex);
    if (q->count > 0 && q->active_transfers < MAX_SIMULTANEOUS_TRANSFERS) {
        *meta = q->files[q->front];
        
        // Verify sender and recipient are still active
        int sender_idx = find_client_by_username(meta->sender);
        int recipient_idx = find_client_by_username(meta->recipient);
        
        pthread_mutex_lock(&clients_mutex);
        int sender_active = (sender_idx != -1 && clients[sender_idx].active);
        int recipient_active = (recipient_idx != -1 && clients[recipient_idx].active);
        pthread_mutex_unlock(&clients_mutex);
        
        if (!sender_active || !recipient_active) {
            log_event("[FILE_QUEUE_ERROR] Sender or recipient offline for queued transfer");
            q->front = (q->front + 1) % MAX_FILE_QUEUE;
            q->count--;
            pthread_cond_signal(&q->not_full);
            pthread_mutex_unlock(&q->mutex);
            return 0;
        }
        
        q->front = (q->front + 1) % MAX_FILE_QUEUE;
        q->count--;
        q->active_transfers++;
        
        log_event("[FILE_QUEUE] Next transfer started: %s -> %s, queue size: %d, active: %d", 
                 meta->sender, meta->recipient, q->count, q->active_transfers);
        pthread_mutex_unlock(&q->mutex);
        return 1;
    }
    
    log_event("[FILE_QUEUE] No transfers to start (queue: %d, active: %d)", 
             q->count, q->active_transfers);
    pthread_mutex_unlock(&q->mutex);
    return 0;
}