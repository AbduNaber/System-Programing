
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

#include "bankDefination.h"


void write_error(const char* msg);
void write_output(const char* msg);
int load_log(int fd,client_t *clients);
int parse_clients_log(int fd, client_t *clients, int *client_count_out) ;
int get_or_create_client(client_t **clients, int *client_count, int *client_capacity, unsigned int id);
unsigned int extract_client_number(const char *id_str);
void print_clients(client_t * clients, int client_count);
void clear_heap(client_t *clients, int client_count) ;

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

    client_t *clients = malloc(sizeof(client_t) * INITIAL_CLIENTS);
    int client_count = 0;
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
        if(parse_clients_log(log_fd,clients,&client_count) == -1){
            write(STDERR_FILENO, "Error loading logs\n", 20); 
            return 1;
        }
    }
    print_clients(clients, client_count);
    write_output("Waiting for clients @");
    write_output(server_fifo_name);
    write_output("...\n");


    // close(log_fd);
    // unlink(server_fifo_name);
    clear_heap(clients, client_count);

    write_output(bank_name);
    write_output(" says “Bye...\n");
    return 0;
}

int get_or_create_client(client_t **clients, int *client_count, int *client_capacity, unsigned int id) {
    for (int i = 0; i < *client_count; i++) {
        if ((*clients)[i].client_id == id) {
            return i;
        }
    }

    // Need to create a new one
    if (*client_count >= *client_capacity) {
        int new_cap = (*client_capacity) * 2;
        client_t *new_clients = realloc(*clients, sizeof(client_t) * new_cap);
        if (!new_clients) return -1;
        *clients = new_clients;
        *client_capacity = new_cap;
    }

    client_t *c = &(*clients)[*client_count];
    c->client_id = id;
    c->credits = 0;
    c->transactions = malloc(sizeof(transaction_t) * MAX_TX_PER_CLIENT);
    if (!c->transactions) return -1;
    (*client_count)++;
    return (*client_count - 1);
}

int parse_clients_log(int fd, client_t *clients, int *client_count_out) {
    int client_capacity = INITIAL_CLIENTS;
    int client_count = 0;
    
    if (!clients) return -1;

    char buf[1];
    char line[MAX_LINE];
    int line_len = 0;

    while (read(fd, buf, 1) == 1) {
        if (buf[0] == '\n' || line_len >= MAX_LINE - 1) {
            line[line_len] = '\0';
            line_len = 0;

            
            if (line[0] == '#') {
                if (line[1] == '#' || strstr(line, "Log file") || strstr(line, "end of log")) continue;
                
                memmove(line, line + 1, strlen(line));  
            }
            if (line[0] == '\0') continue;

            char *saveptr;
            char *token = strtok_r(line, " ", &saveptr);
            if (!token) continue;

            unsigned int client_id = extract_client_number(token);
            int client_index = get_or_create_client(&clients, &client_count, &client_capacity, client_id);
            if (client_index == -1) return -1;

            client_t *client = &clients[client_index];
            int tx_count = 0;

            while ((token = strtok_r(NULL, " ", &saveptr))) {
                if (strcmp(token, "0") == 0) break;

                transaction_t tx;
                if (strcmp(token, "D") == 0) tx.op = DEPOSIT;
                else if (strcmp(token, "W") == 0) tx.op = WITHDRAW;
                else continue;

                token = strtok_r(NULL, " ", &saveptr);
                if (!token) break;
                tx.amount = atoi(token);

                if (tx_count >= MAX_TX_PER_CLIENT) {
                    transaction_t *new_tx = realloc(client->transactions, sizeof(transaction_t) * (tx_count + MAX_TX_PER_CLIENT));
                    if (!new_tx) return -1;
                    client->transactions = new_tx;
                }

                client->transactions[tx_count++] = tx;

                if (tx.op == DEPOSIT)
                    client->credits += tx.amount;
                else
                    client->credits -= tx.amount;
            }
        } else {
            line[line_len++] = buf[0];
        }
    }


    *client_count_out = client_count;
    return 0;
}

void get_time(char * time_buf){

    time_t now;
    struct tm *t;
    time(&now);
    t = localtime(&now);
    strftime(time_buf, 100, "%H:%M %B %d %Y", t);

}

void print_clients(client_t * clients, int client_count){
    for(int i = 0; i < client_count; i++) {
        write(STDOUT_FILENO, "Client ID: ", 11);
        char id_buf[20];
        snprintf(id_buf, sizeof(id_buf), "%u\n", clients[i].client_id);
        write(STDOUT_FILENO, id_buf, strlen(id_buf));
        write(STDOUT_FILENO, "Credits: ", 9);
        char credits_buf[20];
        snprintf(credits_buf, sizeof(credits_buf), "%d\n", clients[i].credits);
        write(STDOUT_FILENO, credits_buf, strlen(credits_buf));
    }
}

unsigned int extract_client_number(const char *id_str) {
    const char *digit_ptr = id_str;
    while (*digit_ptr && (*digit_ptr < '0' || *digit_ptr > '9')) digit_ptr++;
    return (unsigned int)atoi(digit_ptr);
}

void clear_heap(client_t *clients, int client_count) {
    for (int i = 0; i < client_count; i++) {
        free(clients[i].transactions);
    }
    free(clients);
}