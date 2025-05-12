#ifndef BUFFER_H
#define BUFFER_H

#include <pthread.h>
#include <stdbool.h> // Include for bool type

#define LINE_SIZE 1024

typedef struct {
    char **lines;
    int size;
    int in;
    int out;
    int count;
    bool manager_done; // Flag to indicate manager finished
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} buffer_t;

void buffer_init(buffer_t *buf, int size);
void buffer_destroy(buffer_t *buf);
void buffer_add(buffer_t *buf, char *line);
char *buffer_get(buffer_t *buf);

#endif