
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

#include "bankDefination.h"



void write_error(const char* msg) {
    write(STDERR_FILENO, msg, strlen(msg));
}

void write_output(const char* msg) {
    write(STDOUT_FILENO, msg, strlen(msg));
}

int main(int argc, char *argv[])
{
    if(argc != 3){
        write_error("Usage: ");
        write(STDERR_FILENO, argv[0], strlen(argv[0])); 
        write_error(" <bank_name> <server_fifo_name>\n");
        return 1;
    }
    char *bank_name = argv[1];
    char *server_fifo_name = argv[2];

    write(STDOUT_FILENO, bank_name, strlen(bank_name));
    write_output(" is active…\n");

    char log_file_name[256] = "";
    strcat(log_file_name, bank_name);
    strcat(log_file_name, ".bankLog");

    char buffer[BUFSIZ];
    char time_buf[100];

    transaction_t *transactions = malloc(sizeof(transaction_t) * MAX_TRANSACTIONS);
    client_t *clients = malloc(sizeof(client_t) * MAX_CLIENTS);

    int log_fd = open(log_file_name, O_RDWR, 0644);
    if(log_fd == -1){
        if(errno == ENOENT){
            write_output("No previous logs.. Creating the bank database\n");
            log_fd = open(log_file_name, O_RDWR | O_CREAT, 0644);
            if(log_fd == -1){
                write(STDERR_FILENO, "Error creating log file\n", 24); 
                return 1;
            }
            
            
        }
        else{
            write(STDERR_FILENO, "Error opening log file\n", 24); 
            return 1;   
        }
    }
    else{
        write_output("Previous logs found!\n");
        write_output("Loading logs...\n");
        if(load_logs() == -1){
            write(STDERR_FILENO, "Error loading logs\n", 20); 
            return 1;
        }
    }
    write_output("Waiting for clients @");
    write_output(server_fifo_name);
    write_output("...\n");

    free(transactions);
    free(clients);
    close(log_fd);
    unlink(server_fifo_name);

    write_output(bank_name);
    write_output(" says “Bye...\n");
    return 0;
}

int load_log(int fd,transaction_t *transactions){
    int count = parse_log_fd(fd, transactions, MAX_TRANSACTIONS);
    write_output("Previous log loaded\n");
    return count;
}



int parse_log_fd(int fd, transaction_t *transactions, int max_transactions) {
    char buf[1];
    char line[MAX_LINE];
    int line_len = 0;
    int tx_count = 0;

    while (read(fd, buf, 1) == 1) {
        if (buf[0] == '\n' || line_len >= MAX_LINE - 1) {
            line[line_len] = '\0';
            line_len = 0;

            // Skip comment lines
            if (line[0] == '#' || line[0] == '\0') continue;

            // Parse line
            char *saveptr;
            char *token = strtok_r(line, " ", &saveptr);
            if (!token) continue;

            char client_id[100];
            strncpy(client_id, token, sizeof(client_id));
            client_id[sizeof(client_id) - 1] = '\0';

            while ((token = strtok_r(NULL, " ", &saveptr))) {
                if (strcmp(token, "0") == 0) break;

                transaction_t tx;
                strncpy(tx.client_id, client_id, sizeof(tx.client_id));
                tx.client_id[sizeof(tx.client_id) - 1] = '\0';

                if (strcmp(token, "D") == 0) {
                    tx.op = DEPOSIT;
                } else if (strcmp(token, "W") == 0) {
                    tx.op = WITHDRAW;
                } else {
                    continue;
                }

                token = strtok_r(NULL, " ", &saveptr);
                if (!token) break;

                tx.amount = atoi(token);

                transactions[tx_count++] = tx;
                if (tx_count >= max_transactions) return tx_count;
            }

        } else {
            line[line_len++] = buf[0];
        }
        // Check if we need to reallocate memory for transactions
        if(tx_count >= max_transactions) {
            transactions = realloc(transactions, sizeof(transaction_t) * (sizeof(transactions) + MAX_TRANSACTIONS));
            if(transactions == NULL) {
                write(STDERR_FILENO, "Error reallocating memory for transactions\n", 44);
                return -1;
            }
        };
    }

    return tx_count;
}


int load_transactions(transaction_t *transactions, client_t * clients) {
    
}

void get_time(char * time_buf){

    time_t now;
    struct tm *t;
    time(&now);
    t = localtime(&now);
    strftime(time_buf, 100, "%H:%M %B %d %Y", t);

}


 


