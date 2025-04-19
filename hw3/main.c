
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h> /* For mode constants */
#include <fcntl.h> /* For O_* constants */ 
#include <errno.h>
#include <string.h>          // For memset
#include <semaphore.h>
#include <sys/wait.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>

#define MAX_SATELLITE 100

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

typedef struct  {
    int satelliteID;
    int priority;
    pthread_t threadID;
} Satellite;

typedef struct {
    int engineerID;
    pthread_t threadID;
} Engineer;

typedef struct {
    Satellite data[MAX_SATELLITE];
    int size;
} PriorityQueue;

static int availableEngineers = 0;
static PriorityQueue requestQueue;
static int requestID = 0;

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER; 
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

void initQueue(PriorityQueue *pq) {
    pq->size = 0;
}

void swap(Satellite *a, Satellite *b) {
    Satellite temp = *a;
    *a = *b;
    *b = temp;
}

void insert(PriorityQueue *pq, Satellite s) {
    if (pq->size >= MAX_SATELLITE) {
        printf("Queue full!\n");
        return;
    }
    int i = pq->size++;
    pq->data[i] = s;
    // Heapify up
    while (i > 0 && pq->data[i].priority > pq->data[(i - 1) / 2].priority) {
        swap(&pq->data[i], &pq->data[(i - 1) / 2]);
        i = (i - 1) / 2;
    }
}

Satellite extractMax(PriorityQueue *pq) {
    if (pq->size <= 0) {
        Satellite empty = {-1, -1, 0};
        return empty;
    }

    Satellite max = pq->data[0];
    pq->data[0] = pq->data[--pq->size];

    // Heapify down
    int i = 0;
    while (1) {
        int largest = i;
        int left = 2 * i + 1;
        int right = 2 * i + 2;

        if (left < pq->size && pq->data[left].priority > pq->data[largest].priority)
            largest = left;
        if (right < pq->size && pq->data[right].priority > pq->data[largest].priority)
            largest = right;
        if (largest == i)
            break;

        swap(&pq->data[i], &pq->data[largest]);
        i = largest;
    }

    return max;
}

int satellite(void* satelliteID_ptr) {
    struct timespec ts;
    int rc;
    int satelliteID = *((int*)satelliteID_ptr);
    // Get the current time
    clock_gettime(0, &ts);
    ts.tv_sec += 5;
    
    pthread_mutex_lock(&lock);
    while (availableEngineers == 0) {
        rc = pthread_cond_timedwait(&cond, &lock, &ts);
        if (rc == ETIMEDOUT) {
            printf("Satellite %d timout 5\n", satelliteID);
            pthread_mutex_unlock(&lock);
            pthread_exit(NULL);
            }
    }



    pthread_mutex_unlock(&lock);
    return 0;
}

int findSatelliteIDByThreadID(pthread_t threadID) {
    for (int i = 0; i < requestQueue.size; i++) {
        if (pthread_equal(requestQueue.data[i].threadID, threadID)) {
            return requestQueue.data[i].satelliteID;
        }
    }
    return -1;
}

int engineer(void* engineerID_ptr){
    int engineerID = *((int*)engineerID_ptr);
    while(1){
        
        Satellite s = extractMax(&requestQueue);
        if(s.satelliteID == -1) {
            printf("No satellite to assist\n");
            pthread_mutex_unlock(&lock);
            pthread_exit(NULL);
        }
        printf("[ENGINEER %d] handling satellite %d (priority %d)\n", engineerID,s.satelliteID,s.priority);
        pthread_mutex_lock(&lock);
        availableEngineers--;
        sleep(4); // Simulate time taken to assist the satellite
        pthread_mutex_unlock(&lock);
        availableEngineers++;
        printf("[ENGINEER %d] finished satellite %d\n", engineerID, s.satelliteID);
        
        
    }
    
    
}


int main(){

    srand(time(NULL));
    int satellite_number = rand() % 10 + 1;
    requestQueue.size = satellite_number;
    int MAX_PRIORITY = satellite_number;

    // Generate unique priorities
    int priorities[MAX_PRIORITY];
    for (int i = 0; i < MAX_PRIORITY; i++)
        priorities[i] = i + 1;

    // Shuffle them (Fisher-Yates)
    for (int i = MAX_PRIORITY - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = priorities[i];
        priorities[i] = priorities[j];
        priorities[j] = tmp;
    }

    pthread_mutex_init(&lock, NULL);
    pthread_cond_init(&cond, NULL);
    initQueue(&requestQueue);

    // Create satellites
    for (int i = 0; i < satellite_number; i++) {
        Satellite s;
        s.satelliteID = i + 1;
        s.priority = priorities[i]; // Random priority between 1 and 10
        pthread_create(&s.threadID, NULL, (void *)satellite, &s.satelliteID);
        insert(&requestQueue, s);
    }

    // Create engineers
    int engineer_number = 3;
    Engineer engineers[engineer_number];
    for (int i = 0; i < engineer_number; i++) {
        engineers[i].engineerID = i + 1;
        pthread_create(&engineers[i].threadID, NULL, (void *)engineer, &engineers[i].engineerID);
        availableEngineers++;
    }
    

    // Wait for all engineers to finish
    for (int i = 0; i < engineer_number; i++) {
        pthread_join(engineers[i].threadID, NULL);
    }
    // Wait for all satellites to finish
    for (int i = 0; i < satellite_number; i++) {
        pthread_join(requestQueue.data[i].threadID, NULL);
    }
    

}
