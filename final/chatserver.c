// Compile: gcc chatserver.c -o chatserver -lpthread
#include "chatDefination.h"
#include <time.h>
#include <stdarg.h>

#define LOG_FILE "server.log"

typedef struct {
    int socket;
    char username[MAX_USERNAME_LENGTH];
    char current_room[MAX_GROUP_NAME_LENGTH];
    int active;
} client_info_t;

typedef struct {
    char name[MAX_GROUP_NAME_LENGTH];
    int members[MAX_GROUP_MEMBERS];
    int member_count;
} room_t;

client_info_t clients[MAX_CLIENTS];
room_t rooms[MAX_GROUPS];
int room_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t rooms_mutex = PTHREAD_MUTEX_INITIALIZER;

void *handle_client_read(void *arg);
void *handle_client_write(void *arg);
void broadcast_to_room(char *msg, char *room_name, int sender_socket);
void send_private_message(char *msg, char *target_username, int sender_socket);
int find_client_by_socket(int socket);
int find_client_by_username(char *username);
int find_or_create_room(char *room_name);
void remove_client_from_room(int client_index);
void add_client_to_room(int client_index, char *room_name);
void handle_command(int client_socket, char *message);
void log_event(const char *format, ...);

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: ./%s <port>\n", argv[0]);
        exit(1);
    }
    
    int port = atoi(argv[1]);
    int server_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len;
    
    // Initialize clients array
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].socket = -1;
        clients[i].active = 0;
        memset(clients[i].username, 0, MAX_USERNAME_LENGTH);
        memset(clients[i].current_room, 0, MAX_GROUP_NAME_LENGTH);
    }
    
    // Initialize rooms array
    for (int i = 0; i < MAX_GROUPS; i++) {
        memset(rooms[i].name, 0, MAX_GROUP_NAME_LENGTH);
        rooms[i].member_count = 0;
        for (int j = 0; j < MAX_GROUP_MEMBERS; j++) {
            rooms[i].members[j] = -1;
        }
    }
    
    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        exit(1);
    }
    
    // Set socket options to reuse address
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt failed");
        exit(1);
    }
    
    // Setup server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY; // Accept connections from any address
    
    // Bind
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(1);
    }
    
    // Listen
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("Listen failed");
        exit(1);
    }
    printf("Server listening on port %d...\n", port);
    log_event("Server started on port %d", port);
    
    while (1) {
        addr_len = sizeof(client_addr);
        int client_socket = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }
        
        // Find available slot for client
        pthread_mutex_lock(&clients_mutex);
        int client_index = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].active) {
                client_index = i;
                clients[i].socket = client_socket;
                clients[i].active = 1;
                strcpy(clients[i].current_room, "");
                break;
            }
        }
        pthread_mutex_unlock(&clients_mutex);
        
        if (client_index == -1) {
            printf("Max clients reached. Rejecting connection.\n");
            send(client_socket, "Server full. Try again later.\n", 31, 0);
            close(client_socket);
            continue;
        }
        
        printf("Client connected from %s:%d (slot %d)\n", 
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), client_index);
        log_event("[LOGIN] user slot %d connected from %s", client_index, inet_ntoa(client_addr.sin_addr));
        
        // Create read and write threads for this client
        pthread_t read_thread, write_thread;
        int *client_index_ptr = malloc(sizeof(int));
        *client_index_ptr = client_index;
        
        if (pthread_create(&read_thread, NULL, handle_client_read, client_index_ptr) != 0) {
            perror("Failed to create read thread");
            close(client_socket);
            clients[client_index].active = 0;
            free(client_index_ptr);
            continue;
        }
        
        pthread_detach(read_thread);
    }
    
    close(server_fd);
    return 0;
}

void *handle_client_read(void *arg) {
    int client_index = *(int *)arg;
    int client_socket = clients[client_index].socket;
    char buffer[BUFFER_SIZE];
    int bytes_read;
    
    printf("Read thread started for client %d\n", client_index);
    
    while (clients[client_index].active && 
           (bytes_read = read(client_socket, buffer, BUFFER_SIZE - 1)) > 0) {
        buffer[bytes_read] = '\0';
        
        // Remove newline if present
        char *newline = strchr(buffer, '\n');
        if (newline) *newline = '\0';
        
        printf("Client %d (%s): %s\n", client_index, clients[client_index].username, buffer);
        
        if (buffer[0] == '/') {
            handle_command(client_socket, buffer);
        } else {
            // Regular message - broadcast to current room
            if (strlen(clients[client_index].current_room) > 0) {
                char formatted_msg[BUFFER_SIZE + 100];
                snprintf(formatted_msg, sizeof(formatted_msg), "[%s]: %s", 
                        clients[client_index].username, buffer);
                broadcast_to_room(formatted_msg, clients[client_index].current_room, client_socket);
            } else {
                send(client_socket, "You must join a room first. Use /join <room_name>", 49, 0);
            }
        }
    }
    
    // Client disconnected
    printf("Client %d disconnected\n", client_index);
    log_event("Client %d disconnected", client_index);
    pthread_mutex_lock(&clients_mutex);
    remove_client_from_room(client_index);
    clients[client_index].active = 0;
    close(clients[client_index].socket);
    clients[client_index].socket = -1;
    pthread_mutex_unlock(&clients_mutex);
    
    free(arg);
    return NULL;
}

void handle_command(int client_socket, char *message) {
    int client_index = find_client_by_socket(client_socket);
    if (client_index == -1) return;
    
    char *cmd = strtok(message, " ");
    char response[BUFFER_SIZE];
    
    if (strcmp(cmd, "/username") == 0) {
        char *username = strtok(NULL, " ");
        if (username) {
            pthread_mutex_lock(&clients_mutex);
            // Check if username already exists
            if (find_client_by_username(username) != -1) {
                snprintf(response, sizeof(response), "Username '%s' is already taken.", username);
            } else {
                strncpy(clients[client_index].username, username, MAX_USERNAME_LENGTH - 1);
                snprintf(response, sizeof(response), "Username set to '%s'", username);
                log_event("[USER: %s] - set username", username);
            }
            pthread_mutex_unlock(&clients_mutex);
        } else {
            strcpy(response, "Usage: /username <name>");
        }
        send(client_socket, response, strlen(response), 0);
        
    } else if (strcmp(cmd, "/join") == 0) {
        char *room_name = strtok(NULL, " ");
        if (room_name) {
            pthread_mutex_lock(&clients_mutex);
            pthread_mutex_lock(&rooms_mutex);
            // Remove from current room
            remove_client_from_room(client_index);
            // Add to new room
            add_client_to_room(client_index, room_name);
            strcpy(clients[client_index].current_room, room_name);
            snprintf(response, sizeof(response), "Joined room '%s'", room_name);
            log_event("[USER: %s] - joined room '%s'", clients[client_index].username, room_name);
            pthread_mutex_unlock(&rooms_mutex);
            pthread_mutex_unlock(&clients_mutex);
        } else {
            strcpy(response, "Usage: /join <room_name>");
        }
        send(client_socket, response, strlen(response), 0);
        
    } else if (strcmp(cmd, "/broadcast") == 0) {
        char *msg = message + 11; // Skip "/broadcast "
        if (strlen(clients[client_index].current_room) > 0) {
            char formatted_msg[BUFFER_SIZE + 100];
            snprintf(formatted_msg, sizeof(formatted_msg), "[BROADCAST] %s: %s", 
                    clients[client_index].username, msg);
            broadcast_to_room(formatted_msg, clients[client_index].current_room, client_socket);
            strcpy(response, "Message broadcasted");
            log_event("[BROADCAST] user '%s': %s", clients[client_index].username, msg);
        } else {
            strcpy(response, "You must join a room first");
        }
        send(client_socket, response, strlen(response), 0);
        
    } else if (strcmp(cmd, "/whisper") == 0) {
        char *target_user = strtok(NULL, " ");
        char *msg = strtok(NULL, ""); // Get rest of the message
        if (target_user && msg) {
            char formatted_msg[BUFFER_SIZE + 100];
            snprintf(formatted_msg, sizeof(formatted_msg), "[WHISPER from %s]: %s", 
                    clients[client_index].username, msg);
            send_private_message(formatted_msg, target_user, client_socket);
            snprintf(response, sizeof(response), "Whisper sent to %s", target_user);
            log_event("[WHISPER] from '%s' to '%s': %s", clients[client_index].username, target_user, msg);
        } else {
            strcpy(response, "Usage: /whisper <username> <message>");
        }
        send(client_socket, response, strlen(response), 0);
        
    } else if (strcmp(cmd, "/sendfile") == 0) {
        char *filename = strtok(NULL, " ");
        char *target_user = strtok(NULL, " ");
        if (filename && target_user) {
            // Simulate file transfer (in real implementation, you'd handle actual file transfer)
            char formatted_msg[BUFFER_SIZE + 100];
            snprintf(formatted_msg, sizeof(formatted_msg), "[FILE from %s]: %s", 
                    clients[client_index].username, filename);
            send_private_message(formatted_msg, target_user, client_socket);
            snprintf(response, sizeof(response), "File '%s' sent to %s", filename, target_user);
            log_event("[SEND FILE] '%s' sent from %s to %s (success)", filename, clients[client_index].username, target_user);
        } else {
            strcpy(response, "Usage: /sendfile <filename> <username>");
        }
        send(client_socket, response, strlen(response), 0);
        
    } else if (strcmp(cmd, "/list") == 0) {
        // List users in current room
        if (strlen(clients[client_index].current_room) > 0) {
            pthread_mutex_lock(&clients_mutex);
            pthread_mutex_lock(&rooms_mutex);
            
            int room_index = find_or_create_room(clients[client_index].current_room);
            strcpy(response, "Users in room: ");
            
            for (int i = 0; i < rooms[room_index].member_count; i++) {
                int member_index = rooms[room_index].members[i];
                if (member_index >= 0 && clients[member_index].active) {
                    strcat(response, clients[member_index].username);
                    strcat(response, " ");
                }
            }
            
            pthread_mutex_unlock(&rooms_mutex);
            pthread_mutex_unlock(&clients_mutex);
        } else {
            strcpy(response, "You must join a room first");
        }
        send(client_socket, response, strlen(response), 0);
        
    } else if (strcmp(cmd, "/exit") == 0) {
        strcpy(response, "Goodbye!");
        send(client_socket, response, strlen(response), 0);
        clients[client_index].active = 0;
        log_event("Client %d disconnected voluntarily", client_index);
        
    } else {
        strcpy(response, "Unknown command. Available commands: /username, /join, /broadcast, /whisper, /sendfile, /list, /exit");
        send(client_socket, response, strlen(response), 0);
    }
}

void broadcast_to_room(char *msg, char *room_name, int sender_socket) {
    pthread_mutex_lock(&rooms_mutex);
    
    int room_index = -1;
    for (int i = 0; i < room_count; i++) {
        if (strcmp(rooms[i].name, room_name) == 0) {
            room_index = i;
            break;
        }
    }
    
    if (room_index != -1) {
        for (int i = 0; i < rooms[room_index].member_count; i++) {
            int member_index = rooms[room_index].members[i];
            if (member_index >= 0 && clients[member_index].active && 
                clients[member_index].socket != sender_socket) {
                send(clients[member_index].socket, msg, strlen(msg), 0);
            }
        }
    }
    
    pthread_mutex_unlock(&rooms_mutex);
}

void send_private_message(char *msg, char *target_username, int sender_socket) {
    pthread_mutex_lock(&clients_mutex);
    
    int target_index = find_client_by_username(target_username);
    if (target_index != -1 && clients[target_index].active) {
        send(clients[target_index].socket, msg, strlen(msg), 0);
    } else {
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, sizeof(error_msg), "User '%s' not found or offline", target_username);
        send(sender_socket, error_msg, strlen(error_msg), 0);
    }
    
    pthread_mutex_unlock(&clients_mutex);
}

int find_client_by_socket(int socket) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && clients[i].socket == socket) {
            return i;
        }
    }
    return -1;
}

int find_client_by_username(char *username) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active && strcmp(clients[i].username, username) == 0) {
            return i;
        }
    }
    return -1;
}

int find_or_create_room(char *room_name) {
    // Find existing room
    for (int i = 0; i < room_count; i++) {
        if (strcmp(rooms[i].name, room_name) == 0) {
            return i;
        }
    }
    
    // Create new room if space available
    if (room_count < MAX_GROUPS) {
        strcpy(rooms[room_count].name, room_name);
        rooms[room_count].member_count = 0;
        for (int i = 0; i < MAX_GROUP_MEMBERS; i++) {
            rooms[room_count].members[i] = -1;
        }
        return room_count++;
    }
    
    return -1; // No space for new room
}

void remove_client_from_room(int client_index) {
    if (strlen(clients[client_index].current_room) == 0) return;
    
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
                break;
            }
        }
    }
    
    strcpy(clients[client_index].current_room, "");
}

void add_client_to_room(int client_index, char *room_name) {
    int room_index = find_or_create_room(room_name);
    if (room_index != -1 && rooms[room_index].member_count < MAX_GROUP_MEMBERS) {
        rooms[room_index].members[rooms[room_index].member_count] = client_index;
        rooms[room_index].member_count++;
    }
}

void log_event(const char *format, ...) {
    FILE *log_fp = fopen(LOG_FILE, "a");
    if (!log_fp) return;
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    fprintf(log_fp, "%s - ", time_str);
    va_list args;
    va_start(args, format);
    vfprintf(log_fp, format, args);
    va_end(args);
    fprintf(log_fp, "\n");
    fclose(log_fp);
}