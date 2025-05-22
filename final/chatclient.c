// Compile: gcc chatclient.c -o chatclient
#include "chatDefination.h"

void handleJoin(int sock, char *room, char *current_room, char *buffer) {
    snprintf(buffer, BUFFER_SIZE, "/join %s", room);
    send(sock, buffer, strlen(buffer), 0);
    snprintf(current_room, 50, "%s", room);
    printf("[Server]: You joined the room '%s'\n", room);
}

void handleBroadcast(int sock, char *msg, char *current_room, char *buffer) {
    snprintf(buffer, BUFFER_SIZE, "/broadcast %s", msg);
    send(sock, buffer, strlen(buffer), 0);
    printf("[Server]: Message sent to room '%s'\n", current_room);
}

void handleWhisper(int sock, char *user, char *msg, char *buffer) {
    snprintf(buffer, BUFFER_SIZE, "/whisper %s %s", user, msg);
    send(sock, buffer, strlen(buffer), 0);
    printf("[Server]: Whisper sent to %s\n", user);
}

void handleSendfile(int sock, char *file, char *user, char *buffer) {
    snprintf(buffer, BUFFER_SIZE, "/sendfile %s %s", file, user);
    send(sock, buffer, strlen(buffer), 0);
    printf("[Server]: File added to the upload queue.\n");
}

void handleExit(int sock, char *buffer) {
    snprintf(buffer, BUFFER_SIZE, "/exit");
    send(sock, buffer, strlen(buffer), 0);
    printf("[Server]: Disconnected. Goodbye!\n");
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
    server_addr.sin_addr.s_addr = inet_addr(argv[1]);

    if (server_addr.sin_addr.s_addr == INADDR_NONE) {
        fprintf(stderr, "Invalid address: %s\n", argv[1]);
        exit(1);
    }

    // Connect
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        exit(1);
    }
    printf("Connected to server.\n");

    char username[50];
    printf("Enter your username: ");
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = 0;
    snprintf(buffer, BUFFER_SIZE, "/username %s", username);
    send(sock, buffer, strlen(buffer), 0);
    memset(buffer, 0, BUFFER_SIZE);
    read(sock, buffer, BUFFER_SIZE);
    printf("%s\n", buffer);

    char current_room[50] = "";
    while (1) {
        printf("> ");
        fgets(buffer, BUFFER_SIZE, stdin);
        buffer[strcspn(buffer, "\n")] = 0;

        if (strncmp(buffer, "/join ", 6) == 0) {
            char *room = buffer + 6;
            handleJoin(sock, room, current_room, buffer);
            continue;
        } else if (strncmp(buffer, "/broadcast ", 11) == 0) {
            char *msg = buffer + 11;
            handleBroadcast(sock, msg, current_room, buffer);
            continue;
        } else if (strncmp(buffer, "/whisper ", 9) == 0) {
            char *rest = buffer + 9;
            char *space = strchr(rest, ' ');
            if (space) {
                *space = 0;
                char *user = rest;
                char *msg = space + 1;
                handleWhisper(sock, user, msg, buffer);
            }
            continue;
        } else if (strncmp(buffer, "/sendfile ", 10) == 0) {
            char *rest = buffer + 10;
            char *space = strchr(rest, ' ');
            if (space) {
                *space = 0;
                char *file = rest;
                char *user = space + 1;
                handleSendfile(sock, file, user, buffer);
            }
            continue;
        } else if (strcmp(buffer, "/exit") == 0) {
            handleExit(sock, buffer);
            break;
        } else {
            // fallback: send as normal message
            send(sock, buffer, strlen(buffer), 0);
        }

        memset(buffer, 0, BUFFER_SIZE);
        int n = read(sock, buffer, BUFFER_SIZE);
        if (n > 0) {
            printf("Server: %s\n", buffer);
        }
    }

    close(sock);
    return 0;
}
