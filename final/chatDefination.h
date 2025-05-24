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

#include <netinet/in.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>

#define PORT 12345
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 10
#define MAX_MESSAGE_LENGTH 256
#define MAX_USERNAME_LENGTH 16
#define MAX_GROUP_NAME_LENGTH 32
#define MAX_GROUPS 10
#define MAX_GROUP_MEMBERS 10
#define FILE_CHUNK_SIZE 4096
#define FILE_META_MSG_LEN 256


typedef struct {
    char sender[32];
    char recipient[32];
    char filename[128];
    size_t filesize;
} FileMeta;

#endif // CHATDEFINITION_H