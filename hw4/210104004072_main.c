#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <stdbool.h> // Include for bool type
#include "buffer.h"

#define MAX_WORKERS 128

static buffer_t shared_buffer;
static pthread_t *workers;
static int num_workers;
static char *search_term;
static int *matches;
// Removed barrier variables

volatile sig_atomic_t stop = 0;

void sigint_handler(int sig) {
    // Check if stop is already set to avoid redundant printf/broadcasts
    if (!stop) {
        printf("\nReceived SIGINT, signaling stop...\n");
        stop = 1;

        // --- Crucial addition for SIGINT ---
        // We need to wake up any threads potentially waiting on buffer conditions
        pthread_mutex_lock(&shared_buffer.lock);
        // Set manager_done as well, to ensure buffer_get condition breaks
        shared_buffer.manager_done = true;
        pthread_cond_broadcast(&shared_buffer.not_empty); // Wake up consumers
        pthread_cond_broadcast(&shared_buffer.not_full);  // Wake up producer (if waiting)
        pthread_mutex_unlock(&shared_buffer.lock);
        // --- End crucial addition ---
    }
}


void *manager_thread(void *arg) {
    FILE *fp = (FILE *)arg;
    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    printf("Manager thread started.\n");
    while (!stop && (read = getline(&line, &len, fp)) != -1) {
        // If stop occurs during getline or before buffer_add, line might be non-NULL
        if (stop) break;
        buffer_add(&shared_buffer, strdup(line)); // Add a copy
    }
    free(line); // Free buffer allocated by getline
    fclose(fp);

   

    // Signal that manager is done adding items
    pthread_mutex_lock(&shared_buffer.lock);
    shared_buffer.manager_done = true;
    pthread_cond_broadcast(&shared_buffer.not_empty); // Wake up any waiting workers
    pthread_mutex_unlock(&shared_buffer.lock);

    
    return NULL;
}

void *worker_thread(void *arg) {
    long id = (long)arg; // Use long for thread id casting
    int local_count = 0;
    char *line;

    
    while (!stop) {
        line = buffer_get(&shared_buffer);
        if (!line) {
            
            break;
        }

        if (strstr(line, search_term)) {
            local_count++;
        }

        free(line); 
       
    }

    matches[id] = local_count;
    printf("[Thread %ld] Worker exiting. Matches: %d\n", id, local_count); // Debug
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <buffer_size> <num_workers> <log_file> <search_term>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa)); 
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);  
    sa.sa_flags = SA_RESTART; 

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction failed");
        exit(EXIT_FAILURE);
    }

    int buffer_size = atoi(argv[1]);
    num_workers = atoi(argv[2]);
    FILE *fp = fopen(argv[3], "r");
    search_term = argv[4];

    if (buffer_size <= 0 || num_workers <= 0) {
         fprintf(stderr, "Buffer size and number of workers must be positive.\n");
         exit(EXIT_FAILURE);
    }
    if (!fp) {
        perror("Cannot open file");
        fprintf(stderr, "Failed to open file: %s\n", argv[3]);
        exit(EXIT_FAILURE);
    }
    if (num_workers > MAX_WORKERS) {
         fprintf(stderr, "Number of workers exceeds MAX_WORKERS (%d).\n", MAX_WORKERS);
         exit(EXIT_FAILURE);
    }


    buffer_init(&shared_buffer, buffer_size);
    // Removed barrier initialization

    matches = calloc(num_workers, sizeof(int));
    if (!matches) {
        perror("Failed to allocate memory for matches");
        fclose(fp); // Close file before exiting
        exit(EXIT_FAILURE);
    }
    workers = malloc(sizeof(pthread_t) * num_workers);
     if (!workers) {
        perror("Failed to allocate memory for workers");
        free(matches);
        fclose(fp);
        exit(EXIT_FAILURE);
    }

    pthread_t manager;
    if (pthread_create(&manager, NULL, manager_thread, fp) != 0) {
         perror("Failed to create manager thread");
         free(matches);
         free(workers);
         fclose(fp);
         exit(EXIT_FAILURE);
    }


    for (long i = 0; i < num_workers; i++) { // Use long for thread id
         if (pthread_create(&workers[i], NULL, worker_thread, (void *)i) != 0) {
             perror("Failed to create worker thread");
            
             stop = 1; 
             pthread_mutex_lock(&shared_buffer.lock);
             shared_buffer.manager_done = true; 
             pthread_cond_broadcast(&shared_buffer.not_empty);
             pthread_mutex_unlock(&shared_buffer.lock);

             pthread_join(manager, NULL); 
             for (long j = 0; j < i; j++) { 
                 pthread_join(workers[j], NULL);
             }
             free(matches);
             free(workers);
             fclose(fp); // Already closed by manager, but defensive
             exit(EXIT_FAILURE);
         }
    }

   
    pthread_join(manager, NULL);
    

  
    for (int i = 0; i < num_workers; i++) {
        pthread_join(workers[i], NULL);
        // printf("Main: Worker thread %d joined.\n", i); // Debug
    }
    printf("Main: All worker threads joined.\n");

    // Removed barrier wait

    printf("===== Summary =====\n");
    int total = 0;
    for (int i = 0; i < num_workers; i++) {
        printf("Thread %d matches: %d\n", i, matches[i]);
        total += matches[i];
    }
    printf("Total matches: %d\n", total);

    
    buffer_destroy(&shared_buffer);
    free(matches);
    free(workers);
    printf("Main: Cleanup finished. Exiting.\n");
    return 0;
}