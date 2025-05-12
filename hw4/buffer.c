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
    // Ensure all allocated lines in buffer are freed if not consumed
    // This might happen during abrupt termination (e.g., SIGINT)
    // Note: This assumes buffer_get transfers ownership of the string
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
    // Wait only if the buffer is full AND the manager is not done/stopping
    while (buf->count == buf->size && !buf->manager_done && !stop) {
        pthread_cond_wait(&buf->not_full, &buf->lock);
    }
    

    // If stopping or manager finished while waiting, just unlock and potentially drop the line
    if (stop || buf->manager_done) {
        pthread_mutex_unlock(&buf->lock);
        // Optional: Free line if we decide not to add it upon stop/manager_done signal
        // free(line);
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

    // Wait while the buffer is empty AND the manager hasn't finished AND not stopping
    while (buf->count == 0 && !buf->manager_done && !stop) {
        pthread_cond_wait(&buf->not_empty, &buf->lock);
    }

    // If the buffer is empty AND (manager is done OR stop signal received), return NULL
    if (buf->count == 0 && (buf->manager_done || stop)) {
        pthread_mutex_unlock(&buf->lock);
        return NULL; // Signal worker to terminate
    }

    // If we woke up due to stop signal but there are still items, process them
    // If buffer isn't empty, proceed regardless of manager_done or stop status
    if (buf->count > 0) {
         char *line = buf->lines[buf->out];
         buf->out = (buf->out + 1) % buf->size;
         buf->count--;

         pthread_cond_signal(&buf->not_full); // Signal producer (manager) if it was waiting
         pthread_mutex_unlock(&buf->lock);
         return line;
    }

    // Should theoretically not be reached with the above conditions, but as a safeguard:
    pthread_mutex_unlock(&buf->lock);
    return NULL;


}