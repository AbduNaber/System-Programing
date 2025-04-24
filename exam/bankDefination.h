#ifndef BANKDEFINITION_H
#define BANKDEFINITION_H

typedef enum{
    DEPOSIT = 1,
    WITHDRAW = 2
} operation;

typedef struct{
    
    operation op;
    int amount;
} transaction_t;

typedef struct{
    unsigned int client_id;
    int credits;
    transaction_t *transactions;
} client_t;


//#define BUFSIZ 1024
#define MAX_LINE 256
#define MAX_TX_PER_CLIENT 1024
#define INITIAL_CLIENTS 1024

int transaction_counter =  0 ;
int client_counter = 0;
#endif
