#include "buffer.h"
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>


extern volatile sig_atomic_t stop;

void buffer_init(buffer_t *buf, int size) {
    buf->size = size;
    buf->in = buf->out = buf->count = 0;
    buf->manager_done = false; // Initialize manager_done flag
    buf->lines = malloc(sizeof(char *) * size);
    pthread_mutex_init(&buf->lock, NULL);
    pthread_cond_init(&buf->not_empty, NULL);
    pthread_cond_init(&buf->not_full, NULL);
}

void buffer_destroy(buffer_t *buf) {
   
    while(buf->count > 0) {
        free(buf->lines[buf->out]);
        buf->out = (buf->out + 1) % buf->size;
        buf->count--;
    }
    free(buf->lines);
    pthread_mutex_destroy(&buf->lock);
    pthread_cond_destroy(&buf->not_empty);
    pthread_cond_destroy(&buf->not_full);
}

void buffer_add(buffer_t *buf, char *line) {
    pthread_mutex_lock(&buf->lock);
    
    while (buf->count == buf->size && !buf->manager_done && !stop) {
        pthread_cond_wait(&buf->not_full, &buf->lock);
    }
    

    
    if (stop || buf->manager_done) {
        pthread_mutex_unlock(&buf->lock);
     
        return;
    }

    buf->lines[buf->in] = line;
    buf->in = (buf->in + 1) % buf->size;
    buf->count++;
    pthread_cond_broadcast(&buf->not_empty); // Use broadcast as multiple workers might wait
    pthread_mutex_unlock(&buf->lock);
}

char *buffer_get(buffer_t *buf) {
    pthread_mutex_lock(&buf->lock);

   
    while (buf->count == 0 && !buf->manager_done && !stop) {
        pthread_cond_wait(&buf->not_empty, &buf->lock);
    }

   
    if (buf->count == 0 && (buf->manager_done || stop)) {
        pthread_mutex_unlock(&buf->lock);
        return NULL;
    }

    
    if (buf->count > 0) {
         char *line = buf->lines[buf->out];
         buf->out = (buf->out + 1) % buf->size;
         buf->count--;

         pthread_cond_signal(&buf->not_full); 
         pthread_mutex_unlock(&buf->lock);
         return line;
    }

   
    pthread_mutex_unlock(&buf->lock);
    return NULL;


}