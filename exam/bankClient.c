
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include "bankDefination.h"



void write_error(const char* msg);
void write_output(const char* msg);
void print_transactions(transaction_t *tx, int tx_count);

void clear_heap(client_t *clients, int client_count) ;

void write_error(const char* msg) {
    write(STDERR_FILENO, msg, strlen(msg));
}

void write_output(const char* msg) {
    write(STDOUT_FILENO, msg, strlen(msg));
}


int parse_client_file(int fd, transaction_t *transactions, int size) {
    char buf[1];
    char line[MAX_LINE];
    int line_len = 0;
    int tx_count = 0;
    int current_client_id = 0;
    int flag = 0;
    while (!flag) {

        ssize_t bytes_read = read(fd, buf, sizeof(buf));
        if (bytes_read == 0) {
            flag = 1; // End of file
        } else if (bytes_read == -1) {
            write_error("Error reading from file\n");
            return -1;
        }

        if ((buf[0] != '\n' && !flag )  && line_len < MAX_LINE - 1) {
            line[line_len++] = buf[0];
        } else {
            line[line_len] = '\0'; // Null-terminate the line

            // Use strtok_r to parse the line
            char *saveptr;
            char *token = strtok_r(line, " ", &saveptr); // word1 (e.g., N)
            strcpy(transactions[tx_count].bank_id, token); // Copy the client ID
            if (token != NULL) {
                token = strtok_r(NULL, " ", &saveptr); // word2 (deposit/withdraw)
                if (token != NULL) {
                    if (strcmp(token, "deposit") == 0) {
                        transactions[tx_count].op = DEPOSIT;
                    } else if (strcmp(token, "withdraw") == 0) {
                        transactions[tx_count].op = WITHDRAW;
                    } else {
                        return -1; // Invalid operation
                    }

                    token = strtok_r(NULL, " ", &saveptr); // amount
                    if (token != NULL) {
                        
                        transactions[tx_count].amount = atoi(token);
                        
                        tx_count++;
                        if(tx_count >= size) {
                            
                            transaction_t *new_transactions = realloc(transactions, sizeof(transaction_t) * (tx_count + INITIAL_TX));
                            if (!new_transactions) {
                                return -1; 
                            }
                            transactions = new_transactions;
                        }
                    }
                }
            }

            // Reset line buffer
            line_len = 0;
        }
    }    
    return tx_count;
}


unsigned int extract_client_number(const char *id_str) {

    if('N' == *id_str) {
        return -1;
    }
    const char *digit_ptr = id_str;
    while (*digit_ptr && (*digit_ptr < '0' || *digit_ptr > '9')) digit_ptr++;
    return (unsigned int)atoi(digit_ptr);
}

int main(int argc, char *argv[])
{
    if(argc != 3){
        write_error("Usage: ");
        write(STDERR_FILENO, argv[0], strlen(argv[0])); 
        write_error(" <client_file_name> <server_fifo_name>\n");
        return 1;
    }
    char *client_file_name = argv[1];

    int client_file_fd = open(client_file_name, O_RDONLY);
    if (client_file_fd == -1) {
        write_error("Error opening client file\n");
        return 1;
    }
    write_output("Reading ");
    write(STDOUT_FILENO, client_file_name, strlen(client_file_name));
    write_output("...\n");
    transaction_t * transactions = malloc(sizeof(transaction_t) * INITIAL_TX);
    int tx_count = 0;
    tx_count = parse_client_file(client_file_fd, transactions, INITIAL_TX);
    //print_transactions(transactions, tx_count);
    //printf("Total transactions: %d\n", tx_count);


    char *server_fifo_name = argv[2];
    int server_fifo_fd = open(server_fifo_name, O_WRONLY);
    if (server_fifo_fd == -1) {
        write(STDERR_FILENO, "Error opening server_fifo\n", 27);
        return 1;
    }

 
    int client_fifo_fd = open(CLIENT_FIFO_NAME,O_RDONLY);
    if (client_fifo_fd == -1) {
        write(STDERR_FILENO, "Error opening server_read_fifo\n", 32);
        return 1;
    }
    char msg[BUFSIZ];
    read(client_fifo_fd,msg,BUFSIZ);
    write_output("Connected to ");
    write(STDOUT_FILENO,msg,strlen(msg));
    write_output("...\n");
    
    client_info_t client_info;
    client_info.pid = getpid();
    client_info.client_counter = tx_count;
    write(server_fifo_fd, &client_info, sizeof(client_info_t));
    printf("%d\n", client_info.pid);
    return 0;
}



void print_transactions(transaction_t *tx, int tx_count) {
    for (int i = 0; i < tx_count; i++) {
        printf("Client ID: %s\n", tx[i].bank_id);
        printf("Operation: %s\n", tx[i].op == DEPOSIT ? "Deposit" : "Withdraw");
        printf("Amount: %d\n", tx[i].amount);
        write(STDOUT_FILENO, "------------------------\n", 26);
    }
    
}



void clear_heap(client_t *clients, int client_count) {
    for (int i = 0; i < client_count; i++) {
        free(clients[i].transactions);
    }
    free(clients);
}