#ifndef BANKDEFINITION_H
#define BANKDEFINITION_H



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

typedef struct{
    char bank_id[20];
    operation op;
    int amount;
} transaction_t;

typedef struct{
    char bank_id[20];
    int credits;
    transaction_t *transactions;
} client_t;

typedef struct{
    pid_t pid;
    int client_counter;
    char clients_name[20][MAX_TX_PER_CLIENT];
} client_info_t;

typedef struct{
    transaction_t transaction;
    pid_t teller_pid;
} teller_t;


//#define BUFSIZ 1024




#endif
