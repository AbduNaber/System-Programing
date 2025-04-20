
#include <unistd.h>

#include <errno.h>
#include <string.h>          // For memset
#include <semaphore.h>
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
    int isHandled;
    sem_t newRequest;
    sem_t requestHandled;
    pthread_t threadID;
} Satellite;

typedef struct {
    int engineerID;
    pthread_t threadID;
} Engineer;

typedef struct {
    Satellite* satellite_ptr[MAX_SATELLITE];
    int size;
} PriorityQueue;

Satellite* getSatellite(int satelliteID) ;

static int availableEngineers = 0;
PriorityQueue requestQueue;

int handledId = 0;
pthread_mutex_t engineerMutex = PTHREAD_MUTEX_INITIALIZER; 


void initQueue(PriorityQueue *pq) {
    pq->size = 0;
}

void swap(Satellite **a, Satellite **b) {
    Satellite *temp = *a;
    *a = *b;
    *b = temp;
}

Satellite* insert(PriorityQueue *pq, Satellite *s) {
    if (pq->size >= MAX_SATELLITE) {
        printf("Queue full!\n");
        return NULL;
    }
    int i = pq->size++;
    pq->satellite_ptr[i] = s;

    // Heapify up
    while (i > 0 && pq->satellite_ptr[i]->priority > pq->satellite_ptr[(i - 1) / 2]->priority) {
        swap(&pq->satellite_ptr[i], &pq->satellite_ptr[(i - 1) / 2]);
        i = (i - 1) / 2;
    }

    return pq->satellite_ptr[i];  // Return pointer to final position
}

Satellite* extractMax(PriorityQueue *pq) {
    printf("pq->size : %d\n", pq->size);
    if (pq->size <= 0) {
        return NULL;
    }

    Satellite *max = pq->satellite_ptr[0];
    pq->satellite_ptr[0] = pq->satellite_ptr[--pq->size];  

    // Heapify down
    int i = 0;
    while (1) {
        int largest = i;
        int left = 2 * i + 1;
        int right = 2 * i + 2;

        if (left < pq->size && pq->satellite_ptr[left]->priority > pq->satellite_ptr[largest]->priority)
            largest = left;
        if (right < pq->size && pq->satellite_ptr[right]->priority > pq->satellite_ptr[largest]->priority)
            largest = right;
        if (largest == i)
            break;

        swap(&pq->satellite_ptr[i], &pq->satellite_ptr[largest]);
        i = largest;
    }

    // Optional: clear dangling pointer
    pq->satellite_ptr[pq->size] = NULL;

    return max;
}


void* satellite(void* arg) {
    Satellite* s = (Satellite*)arg;
    int satelliteID = s->satelliteID;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 5;

    printf("[SATELLITE] Satellite %d requesting (Priority %d)\n", satelliteID, s->priority);
    sem_post(&s->newRequest);  // Signal request

    if (sem_timedwait(&s->requestHandled, &ts) == -1) {
        if (errno == ETIMEDOUT && s->isHandled == 0) {
            printf("[SATELLITE] Satellite %d timed out\n", satelliteID);
            s->isTimedOut = 1;
            pthread_exit(NULL);
        }
    }

    pthread_exit(NULL);
}



Satellite* getSatellite(int satelliteID) {
    for (int i = 0; i < requestQueue.size; i++) {
        if (requestQueue.satellite_ptr[i]->satelliteID == satelliteID) {
            return requestQueue.satellite_ptr[i];
        }
    }
    return NULL;
}

void* engineer(void* arg) {
    int engineerID = *(int*)arg;

    while (1) {
        pthread_mutex_lock(&engineerMutex);

        Satellite *target = NULL;

        for (int i = 0; i < requestQueue.size; i++) {
            Satellite *s = requestQueue.satellite_ptr[i];
            if (!s->isHandled && !s->isTimedOut && sem_trywait(&s->newRequest) == 0) {
                target = s;
                break;
            }
        }

        if (!target) {
            pthread_mutex_unlock(&engineerMutex);
            break; 
        }

        target->isHandled = 1;
        pthread_mutex_unlock(&engineerMutex);

        printf("[ENGINEER] Engineer %d handling Satellite %d (Priority %d)\n",
               engineerID, target->satelliteID, target->priority);

        sleep(3);  

        sem_post(&target->requestHandled);

        printf("[ENGINEER %d] finished Satellite %d\n", engineerID, target->satelliteID);
    }

    printf("[ENGINEER %d] finished\n", engineerID);
    return NULL;
}

    
int main(){

    srand(time(NULL));
    int satellite_number = 9; //rand() % 10 + 1;
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

    pthread_mutex_init(&engineerMutex, NULL);

    initQueue(&requestQueue);

    for (int i = 0; i < satellite_number; i++) {
        satellites[i].satelliteID = i + 1;
        satellites[i].priority = priorities[i];
        satellites[i].isTimedOut = 0;
        satellites[i].isHandled = 0;
        sem_init(&satellites[i].newRequest, 0, 0);
        sem_init(&satellites[i].requestHandled, 0, 0);
        insert(&requestQueue, &satellites[i]);
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
        
    }
    // Wait for all satellites to finish
    for (int i = 0; i < satellite_number; i++) {
        pthread_join(satellites[i].threadID, NULL);
        
    }
    for (int i = 0; i < satellite_number; i++) {
        sem_destroy(&satellites[i].newRequest);
        sem_destroy(&satellites[i].requestHandled);
    }   
    

}
