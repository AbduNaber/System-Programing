
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
    int isTimedOut;
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

Satellite* getSatellite(int satelliteID) ;

static int availableEngineers = 0;
PriorityQueue requestQueue;
static int request = 0;
int handledId = 0;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER; 

sem_t newRequest;
sem_t requestHandled;

void initQueue(PriorityQueue *pq) {
    pq->size = 0;
}

void swap(Satellite *a, Satellite *b) {
    Satellite temp = *a;
    *a = *b;
    *b = temp;
}

Satellite* insert(PriorityQueue *pq, Satellite s) {
    if (pq->size >= MAX_SATELLITE) {
        printf("Queue full!\n");
        return NULL;
    }
    int i = pq->size++;
    pq->data[i] = s;

    // Heapify up
    while (i > 0 && pq->data[i].priority > pq->data[(i - 1) / 2].priority) {
        swap(&pq->data[i], &pq->data[(i - 1) / 2]);
        i = (i - 1) / 2;
    }

    return &pq->data[i];  // Return pointer to final position
}


Satellite extractMax(PriorityQueue *pq) {
    if (pq->size <= 0) {
        Satellite empty = {-1, -1, 0, 0};
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

void* satellite(void* arg) {
    

    Satellite* s = (Satellite*)arg;
    int satelliteID = s->satelliteID;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 5;

    printf("[SATELLITE] Satellite %d requesting (Priority %d)\n", satelliteID, s->priority);
    sem_post(&newRequest);

    if (sem_timedwait(&requestHandled, &ts) == -1) {
        if (errno == ETIMEDOUT) {
            printf("[SATELLITE] Satellite %d timed out\n", satelliteID);
        } else {
            perror("sem_timedwait");
        }
        s->isTimedOut = 1;
        pthread_exit(NULL);
    }

    pthread_mutex_lock(&lock);
    if (handledId == satelliteID) {
        pthread_mutex_unlock(&lock);
        pthread_exit(NULL);
    }
    pthread_mutex_unlock(&lock);
    pthread_exit(NULL);
}


Satellite* getSatellite(int satelliteID) {
    for (int i = 0; i < requestQueue.size; i++) {
        if (requestQueue.data[i].satelliteID == satelliteID) {
            return &requestQueue.data[i];
        }
    }
    return NULL;
}

void* engineer(void* engineerID_ptr) {
    int engineerID = *(int*)engineerID_ptr;
    
    while (1) {
        sem_wait(&newRequest);

        Satellite s;

        pthread_mutex_lock(&lock);
        if (requestQueue.size == 0) {
            pthread_mutex_unlock(&lock);
            break;
        }

        availableEngineers--;

        s = extractMax(&requestQueue);

        
        if (s.isTimedOut) {
            pthread_mutex_unlock(&lock);
            continue;
        }

        pthread_mutex_unlock(&lock);  

       
        printf("[ENGINEER] Engineer %d handling Satellite %d (Priority %d)\n",
               engineerID, s.satelliteID, s.priority);

        sleep(7);  

        pthread_mutex_lock(&lock);
        handledId = s.satelliteID;
        sem_post(&requestHandled);
        availableEngineers++;
        pthread_mutex_unlock(&lock);

        printf("[ENGINEER %d] finished Satellite %d\n", engineerID, s.satelliteID);

        sleep(1);  // optional rest between tasks
    }
    pthread_exit(NULL);
    return NULL;
}
    
int main(){

    srand(time(NULL));
    int satellite_number = rand() % 10 + 1;
    printf("Number of satellites: %d\n", satellite_number);
    int MAX_PRIORITY = satellite_number;
    
    Satellite satellites[satellite_number];

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
    sem_init(&newRequest, 0, 0);
    sem_init(&requestHandled, 0, 0);
    initQueue(&requestQueue);

    for (int i = 0; i < satellite_number; i++) {
        satellites[i].satelliteID = i + 1;
        satellites[i].priority = priorities[i];
        satellites[i].isTimedOut = 0;
      
        insert(&requestQueue, satellites[i]);  // copy into heap
    }


    for (int i = 0; i < satellite_number; i++) {
        
        pthread_create(&satellites[i].threadID, NULL, satellite, &satellites[i].satelliteID);
    }



    
    

    sleep(1); 
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
        printf("[ENGINEER] Engineer %d finished\n", engineers[i].engineerID);
    }
    // Wait for all satellites to finish
    for (int i = 0; i < satellite_number; i++) {
        pthread_join(satellites[i].threadID, NULL);
        printf("[SATELLITE] Satellite %d finished\n", satellites[i].satelliteID);
    }
    

}
