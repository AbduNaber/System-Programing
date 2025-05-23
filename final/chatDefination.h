#ifndef CHATDEFINITION_H
#define CHATDEFINITION_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <signal.h>

#define PORT 12345
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 10
#define MAX_MESSAGE_LENGTH 256
#define MAX_USERNAME_LENGTH 32
#define MAX_GROUP_NAME_LENGTH 32
#define MAX_GROUPS 10
#define MAX_GROUP_MEMBERS 10

#endif // CHATDEFINITION_H