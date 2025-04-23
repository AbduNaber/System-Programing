#ifndef BANKDEFINITION_H
#define BANKDEFINITION_H

typedef enum{
    DEPOSIT = 1,
    WITHDRAW = 2
} operation;

typedef struct{
    char client_id[100];
    operation op;
    int amount;
} transaction_t;

typedef struct{
    unsigned int client_id;
    int credits;
} client_t;


#define BUFSIZ 1024
#define MAX_LINE 256
#define MAX_TRANSACTIONS 1024
#define MAX_CLIENTS 1024

#endif
