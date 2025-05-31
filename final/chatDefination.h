#ifndef CHATDEFINITION_H
#define CHATDEFINITION_H



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <time.h>
#include <netinet/in.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>
#include <fcntl.h>



#define PORT 12345
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 15
#define MAX_MESSAGE_LENGTH 256
#define MAX_USERNAME_LENGTH 16
#define MAX_GROUP_NAME_LENGTH 32
#define MAX_GROUPS 15
#define MAX_GROUP_MEMBERS 10
#define FILE_META_MSG_LEN 256*2


#define MAX_SIMULTANEOUS_TRANSFERS 5
#define MAX_FILE_QUEUE 5

#define MAX_FILE_SIZE (1024 * 1024 * 3) // 3 MB



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


typedef struct {
    char sender[MAX_USERNAME_LENGTH];
    char recipient[MAX_USERNAME_LENGTH]; 
    char filename[256];
    size_t filesize;
    int sender_socket;
    int recipient_socket;
    time_t enqueue_time;
    time_t start_time;  // Add this to track when transfer starts
} FileMeta;



typedef struct {
    FileMeta files[MAX_FILE_QUEUE];
    int front;
    int rear;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    int active_transfers; // Number of currently active file transfers
} FileQueue;

#endif // CHATDEFINITION_H