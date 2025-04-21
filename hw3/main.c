#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <semaphore.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>

#define MAX_SATELLITE 100
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

typedef struct {
    int satelliteID;
    int priority;
    int isTimedOut;
    int isHandled;
    sem_t newRequest;
    sem_t requestHandled;
    pthread_t threadID;
    pthread_mutex_t stateMutex;
} Satellite;

typedef struct {
    int engineerID;
    pthread_t threadID;
} Engineer;

typedef struct {
    Satellite* satellite_ptr[MAX_SATELLITE];
    int size;
} PriorityQueue;


static PriorityQueue requestQueue;
pthread_mutex_t engineerMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t printLock = PTHREAD_MUTEX_INITIALIZER; 

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
        pthread_mutex_lock(&printLock);
        printf("Queue full!\n");
        pthread_mutex_unlock(&printLock);
        return NULL;
    }
    int i = pq->size++;
    pq->satellite_ptr[i] = s;

    while (i > 0 && pq->satellite_ptr[i]->priority > pq->satellite_ptr[(i - 1) / 2]->priority) {
        swap(&pq->satellite_ptr[i], &pq->satellite_ptr[(i - 1) / 2]);
        i = (i - 1) / 2;
    }
    return pq->satellite_ptr[i];
}

Satellite* getSatellite(int satelliteID) {
    for (int i = 0; i < requestQueue.size; i++) {
        if (requestQueue.satellite_ptr[i]->satelliteID == satelliteID) {
            return requestQueue.satellite_ptr[i];
        }
    }
    return NULL;
}

void* satellite(void* arg) {
    Satellite* s = (Satellite*)arg;
    int satelliteID = s->satelliteID;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 5;

    pthread_mutex_lock(&printLock);
    printf("[SATELLITE] Satellite %d requesting (Priority %d)\n", satelliteID, s->priority);
    pthread_mutex_unlock(&printLock);

    sem_post(&s->newRequest);

    if (sem_timedwait(&s->requestHandled, &ts) == -1) {
        if (errno == ETIMEDOUT) {
            pthread_mutex_lock(&s->stateMutex);
            if (s->isHandled == 0) {
                s->isTimedOut = 1;
                pthread_mutex_lock(&printLock);
                printf("[SATELLITE] Satellite %d timed out\n", satelliteID);
                pthread_mutex_unlock(&printLock);
            }
            pthread_mutex_unlock(&s->stateMutex);
        }
    }

    pthread_exit(NULL);
}

void* engineer(void* arg) {
    int engineerID = *(int*)arg;

    while (1) {
        Satellite *target = NULL;

        pthread_mutex_lock(&engineerMutex);
        for (int i = 0; i < requestQueue.size; i++) {
            Satellite *s = requestQueue.satellite_ptr[i];

            pthread_mutex_lock(&s->stateMutex);
            int handled = s->isHandled;
            int timedOut = s->isTimedOut;
            pthread_mutex_unlock(&s->stateMutex);

            if (!handled && !timedOut && sem_trywait(&s->newRequest) == 0) {
                target = s;
                break;
            }
        }
        pthread_mutex_unlock(&engineerMutex);

        if (!target)
            break;

        pthread_mutex_lock(&target->stateMutex);
        target->isHandled = 1;
        pthread_mutex_unlock(&target->stateMutex);

        pthread_mutex_lock(&printLock);
        printf("[ENGINEER] Engineer %d handling Satellite %d (Priority %d)\n",
               engineerID, target->satelliteID, target->priority);
        pthread_mutex_unlock(&printLock);

        sleep(3);

        sem_post(&target->requestHandled);

        pthread_mutex_lock(&printLock);
        printf("[ENGINEER %d] finished Satellite %d\n", engineerID, target->satelliteID);
        pthread_mutex_unlock(&printLock);
    }

    pthread_mutex_lock(&printLock);
    printf("[ENGINEER %d] finished\n", engineerID);
    pthread_mutex_unlock(&printLock);

    return NULL;
}

int main() {
    srand(time(NULL));
    int satellite_number = 5;
    int MAX_PRIORITY = satellite_number;
    Satellite satellites[satellite_number];

    int priorities[MAX_PRIORITY];
    for (int i = 0; i < MAX_PRIORITY; i++)
        priorities[i] = i + 1;

    for (int i = MAX_PRIORITY - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = priorities[i];
        priorities[i] = priorities[j];
        priorities[j] = tmp;
    }

    initQueue(&requestQueue);

    for (int i = 0; i < satellite_number; i++) {
        satellites[i].satelliteID = i + 1;
        satellites[i].priority = priorities[i];
        satellites[i].isTimedOut = 0;
        satellites[i].isHandled = 0;
        sem_init(&satellites[i].newRequest, 0, 0);
        sem_init(&satellites[i].requestHandled, 0, 0);
        pthread_mutex_init(&satellites[i].stateMutex, NULL);
        insert(&requestQueue, &satellites[i]);
    }

    pthread_mutex_lock(&printLock);
    printf("Number of satellites: %d\n", satellite_number);
    pthread_mutex_unlock(&printLock);

    for (int i = 0; i < satellite_number; i++) {
        pthread_create(&satellites[i].threadID, NULL, satellite, &satellites[i]);
    }

    int engineer_number = 3;
    Engineer engineers[engineer_number];
    for (int i = 0; i < engineer_number; i++) {
        engineers[i].engineerID = i + 1;
        pthread_create(&engineers[i].threadID, NULL, engineer, &engineers[i].engineerID);
    }

    for (int i = 0; i < engineer_number; i++) {
        pthread_join(engineers[i].threadID, NULL);
    }

    for (int i = 0; i < satellite_number; i++) {
        pthread_join(satellites[i].threadID, NULL);
    }

    for (int i = 0; i < satellite_number; i++) {
        sem_destroy(&satellites[i].newRequest);
        sem_destroy(&satellites[i].requestHandled);
        pthread_mutex_destroy(&satellites[i].stateMutex);
    }

    pthread_mutex_destroy(&printLock);

    return 0;
}
