
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

#include "bankDefination.h"




int parse_clients_log(int fd, client_t *clients, int *client_count_out) ;
int get_or_create_client(client_t **clients, int *client_count, int *client_capacity, char * id) ;
void print_clients(client_t * clients, int client_count);
void clear_heap(client_t *clients, int client_count) ;


void write_error(int count, ...) {
    va_list args;
    va_start(args, count);

    for (int i = 0; i < count; i++) {
        const char* msg = va_arg(args, const char*);
        write(STDERR_FILENO, msg, strlen(msg));
    }

    va_end(args);
}

void write_output(int count, ...) {
    va_list args;
    va_start(args, count);

    for (int i = 0; i < count; i++) {
        const char* msg = va_arg(args, const char*);
        write(STDOUT_FILENO, msg, strlen(msg));
    }

    va_end(args);
}


int main(int argc, char *argv[])
{
    if(argc != 3){
        write_error(3, "Usage: ", argv[0], " <bank_name> <server_fifo_name>\n");
        return 1;
    }
    char *bank_name = argv[1];
    char *server_fifo_name = argv[2];

    if (mkfifo(server_fifo_name, 0666) == -1) {
        if (errno == EEXIST) {
            
        } else {
            write(STDERR_FILENO, "Error creating FIFO\n", 21);
            return 1;
        }
       
    }

    if(mkfifo(CLIENT_FIFO_NAME,0666)== -1){
        if (errno == EEXIST) {
            
        } else {
            write(STDERR_FILENO, "Error creating FIFO\n", 21);
            return 1;
        }
    }

    write_output(2,bank_name, " is waiting for clients...\n");

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
            write_output(1,"No previous logs.. Creating the bank database\n");
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
        write_output(2, "Previous logs found!\n","Loading logs...\n");
        if(parse_clients_log(log_fd,clients,&client_count) == -1){
            write(STDERR_FILENO, "Error loading logs\n", 20); 
            return 1;
        }
    }
   
   


    write_output(3 ,"Waiting for clients @", server_fifo_name, "...\n");

    int server_fifo_fd = open(server_fifo_name, O_RDONLY);
    if (server_fifo_fd == -1) {
        if(errno == ENOENT) {
            write(STDERR_FILENO, "Server FIFO not found\n", 23);
        } else {
            write(STDERR_FILENO, "Error opening server FIFO\n", 27);
        }
        return 1;
    }
    
    int client_fifo_fd = open(CLIENT_FIFO_NAME, O_WRONLY);
    if (client_fifo_fd == -1) {
        if(errno == ENOENT) {
            write(STDERR_FILENO, "client FIFO not found\n", 23);
        } else {
            write(STDERR_FILENO, "Error opening server FIFO\n", 27);
        }
        return 1;
    }
    char msg[BUFSIZ];
    strcpy(msg,bank_name);
    write(client_fifo_fd,msg,sizeof(msg));

    client_info_t client_info;
    read(server_fifo_fd, &client_info, sizeof(client_info_t));
    if (client_info.client_counter == -1) {
        write(STDERR_FILENO, "Error reading client info\n", 26);
        return 1;
    }

    char client_info_str[256]= "";
    snprintf(client_info_str, sizeof(client_info_str), "%dClientX...", client_info.pid);
    write_output(2, client_info_str, "\n");
    close(log_fd);
    unlink(server_fifo_name);
    unlink(CLIENT_FIFO_NAME);

    clear_heap(clients, client_count);

    write_output(2, bank_name, " says Bye...\n");
    return 0;
}

int get_or_create_client(client_t **clients, int *client_count, int *client_capacity, char * id) {
    // Check if the client already exists
    for (int i = 0; i < *client_count; i++) {
        if (strcmp((*clients)[i].bank_id, id) == 0) {
            return i; // Client already exists
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
    strcpy(c->bank_id, id);
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

            char *client_id = token;
            int client_index = get_or_create_client(&clients, &client_count, &client_capacity, client_id);
            if (client_index == -1) return -1;

            client_t *client = &clients[client_index];
            strcpy(client->bank_id, client_id);
            int tx_count = 0;

            while ((token = strtok_r(NULL, " ", &saveptr))) {

                if (strcmp(token, "0") == 0) break;

                transaction_t tx;
                strcpy(tx.bank_id, client->bank_id);
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
        snprintf(id_buf, sizeof(id_buf), "%s\n", clients[i].bank_id);
        write(STDOUT_FILENO, id_buf, strlen(id_buf));
        write(STDOUT_FILENO, "Credits: ", 9);
        char credits_buf[20];
        snprintf(credits_buf, sizeof(credits_buf), "%d\n", clients[i].credits);
        write(STDOUT_FILENO, credits_buf, strlen(credits_buf));
    }
}



void clear_heap(client_t *clients, int client_count) {
    for (int i = 0; i < client_count; i++) {
        free(clients[i].transactions);
    }
    free(clients);
}



void intToStr(int N, char *str) {
    int i = 0;
    int sign = N;
    if (N < 0)
        N = -N;
    while (N > 0) {
        str[i++] = N % 10 + '0';
      	N /= 10;
    } 
    if (sign < 0) {
        str[i++] = '-';
    }
    str[i] = '\0';
    for (int j = 0, k = i - 1; j < k; j++, k--) {
        char temp = str[j];
        str[j] = str[k];
        str[k] = temp;
    }
}