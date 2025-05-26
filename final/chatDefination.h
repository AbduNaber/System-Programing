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
#define MAX_CLIENTS 10
#define MAX_MESSAGE_LENGTH 256
#define MAX_USERNAME_LENGTH 16
#define MAX_GROUP_NAME_LENGTH 32
#define MAX_GROUPS 10
#define MAX_GROUP_MEMBERS 10
#define FILE_CHUNK_SIZE 4096
#define FILE_META_MSG_LEN 256


#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

typedef struct {
    char sender[32];
    char recipient[32];
    char filename[128];
    size_t filesize;
} FileMeta;

#endif // CHATDEFINITION_H