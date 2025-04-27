#ifndef BANKDEFINITION_H
#define BANKDEFINITION_H


#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/mman.h>  
#include <sys/wait.h>  
#include <semaphore.h>
#include <signal.h>

#define MAX_LINE 256
#define MAX_TX_PER_CLIENT 1024
#define INITIAL_CLIENTS 1024
#define INITIAL_TX 1024
#define CLIENT_FIFO_NAME "client_fifo.fifo"


int transaction_counter =  0 ;
int client_counter = 0;

typedef enum{
    DEPOSIT = 1,
    WITHDRAW = 2
} operation;

typedef enum{
    INITIALIZE = 0,
    SUCCESS = 1,
    FAILURE = 2,
    ACCOUNT_DELETED = 3,
    INSUFFICIENT_CREDITS = 4,
} server_response;


typedef struct{
    char bank_id[20];
    operation op;
    int amount;
} transaction_t;

typedef struct{
    char bank_id[20];
    int credits;
    transaction_t *transactions;
    int transaction_count;
} client_t;

typedef struct{
    pid_t pid;
    int client_counter;
    char clients_name[INITIAL_CLIENTS][20];
} client_info_t;

typedef struct{
    char client_name[20];
    char client_fifo_name[20];
} client_fifo_t;

typedef struct{
    transaction_t transaction;
    pid_t teller_pid;
    int teller_id;
    int server_read;
} teller_t;

typedef struct{
    int teller_id;
    int client_id;
} teller_client_map_t;

typedef struct{
    int teller_req_fifo_fd;
    int teller_res_fifo_fd;
 } teller_fifo_t;

typedef struct{
    int client_id;
    char bank_id[20];
 } teller_res_t;

 typedef struct {
    int teller_id;
    int client_id;
    teller_t *shared_teller;
    sem_t *sem;
    server_response *response;  
} teller_arg_t;
//#define BUFSIZ 1024




#endif
