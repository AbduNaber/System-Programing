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

#define MAX_SATELLITE 100

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

typedef struct  {
    int satelliteID;
    int priority;
    pthread_t threadID;
} Satellite; 

typedef struct {
    Satellite data[MAX_SATELLITE];
    int size;
} PriorityQueue;

static int availableEngineers = 0;
static PriorityQueue requestQueue;

pthread_mutex_t lock; 


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


int satellite(){
    // Simulate satellite work
    sleep(2);
    return 0;
}

int engineer(){
    // Simulate engineer work
    sleep(3);
    return 0;
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

    for (int i = 0; i < satellite_number; i++) {
        Satellite s;
        s.satelliteID = i + 1;
        s.priority = priorities[i]; // Random priority between 1 and 10
        insert(&requestQueue, s);
    }
    
}
