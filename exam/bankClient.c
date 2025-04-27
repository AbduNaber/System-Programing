
#include "bankDefination.h"




void write_error(int count, ...);
void write_output(int count, ...);
void print_transactions(transaction_t *tx, int tx_count);
int create_teller_fifo(int tx_count);
void clear_heap(client_t *clients, int client_count) ;
void open_teller_fifo(int tx_count, teller_fifo_t * teller_fifo_fd_list);
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

int parse_client_file(int fd, transaction_t *transactions, int size) {
    char buf[1];
    char line[MAX_LINE];
    int line_len = 0;
    int tx_count = 0;
    int flag = 0;
    while (!flag) {

        ssize_t bytes_read = read(fd, buf, sizeof(buf));
        if (bytes_read == 0) {
            flag = 1; // End of file
        } else if (bytes_read == -1) {
            write_error(1,"Error reading from file\n");
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
        
        write_output(3,"Usage: ", argv[0], " <client_file_name> <server_fifo_name>\n");
        return 1;
    }
    char *client_file_name = argv[1];

    int client_file_fd = open(client_file_name, O_RDONLY);
    if (client_file_fd == -1) {
        write_error(1,"Error opening client file\n");
        return 1;
    }
    char output[BUFSIZ];
   
    write_output(3,"Reading ", client_file_name, "...\n");

    transaction_t * transactions = malloc(sizeof(transaction_t) * INITIAL_TX);
    int tx_count = 0;
    tx_count = parse_client_file(client_file_fd, transactions, INITIAL_TX);
    teller_fifo_t teller_fifo_fds[tx_count];
    snprintf(output, sizeof(output), "%d clients to connect.. creating clients..\n", tx_count);
    if(create_teller_fifo(tx_count) == -1){
        write(STDERR_FILENO, "Error creating teller FIFO\n", 28);
        return 1;
    }

    
    char *server_fifo_name = argv[2];
    int server_fifo_fd = open(server_fifo_name, O_WRONLY);
    if (server_fifo_fd == -1) {
        write_error(3, "Error opening ", server_fifo_name, "...\n");
        write_output(1,"EXITING...\n");
        
        return 1;
    }

 
    int client_fifo_fd = open(CLIENT_FIFO_NAME,O_RDONLY);
    if (client_fifo_fd == -1) {
        write(STDERR_FILENO, "Error opening server_read_fifo\n", 32);
        return 1;
    }
    char msg[BUFSIZ];
    read(client_fifo_fd,msg,BUFSIZ);

    write_output(3,"Connected to ", msg, "...\n");
    
    client_info_t client_info;
    client_info.pid = getpid();
    client_info.client_counter = tx_count;
    for (int i = 0; i < tx_count; i++) {
        printf("Client %d: %s\n", i, transactions[i].bank_id);
        strcpy(client_info.clients_name[i], transactions[i].bank_id);
    }
    write(server_fifo_fd, &client_info, sizeof(client_info_t));
    

    teller_client_map_t teller_client_map[tx_count];
    int c = 0;
    for (int i = 0; i < tx_count; i++) {
        teller_client_map[i].teller_id = 0;
        teller_client_map[i].client_id = 0;
    }

    teller_client_map_t temp = {0,0};
    while(temp.teller_id != -1 && temp.client_id != -1) {
       
        read(client_fifo_fd, &temp, sizeof(teller_client_map_t));
        
        if(temp.teller_id != -1 && temp.client_id != -1) {
            teller_client_map[c].teller_id = temp.teller_id;
            teller_client_map[c].client_id = temp.client_id;
            snprintf(output, sizeof(output), "Client%d connected.. %s %d credits", temp.client_id,transactions[c-1].op == WITHDRAW ? "withdrawing" :  "depositing" , transactions[c-1].amount);
            write_output(2, output, "\n");
            c++;
        }
        
    }
    

    open_teller_fifo(tx_count, teller_fifo_fds);
    for (int i = 0; i < tx_count; i++) {
        write(teller_fifo_fds[i].teller_req_fifo_fd, &transactions[i], sizeof(transaction_t));
        close(teller_fifo_fds[i].teller_req_fifo_fd);

    }
    for (int i = 0; i < tx_count; i++) {
        char fifo_name[BUFSIZ];
        snprintf(fifo_name, sizeof(fifo_name), "/tmp/teller_%d.fifo", i);
        unlink(fifo_name);
    }

    return 0;
}


int create_teller_fifo(int tx_count){
    
    for (int i = 0; i < tx_count; i++) {
        char fifo_name[BUFSIZ];
        snprintf(fifo_name, sizeof(fifo_name), "/tmp/teller_%d_req.fifo", i);
        if (mkfifo(fifo_name, 0666) == -1) {
            write(STDERR_FILENO, "Error creating FIFO\n", 21);
            return -1;
        }
        snprintf(fifo_name, sizeof(fifo_name), "/tmp/teller_%d_res.fifo", i);
        if (mkfifo(fifo_name, 0666) == -1) {
            write(STDERR_FILENO, "Error creating FIFO\n", 21);
            return -1;
        }
        
    }
    return 0;
    
}

void open_teller_fifo(int tx_count, teller_fifo_t * teller_fifo_fds){
    char fifo_name[BUFSIZ];
    for (int i = 0; i < tx_count; i++) {
        snprintf(fifo_name, sizeof(fifo_name), "/tmp/teller_%d_req.fifo", i);
        teller_fifo_fds[i].teller_req_fifo_fd = open(fifo_name, O_WRONLY);
        if (teller_fifo_fds[i].teller_req_fifo_fd  == -1) {
            write(STDERR_FILENO, "Error opening FIFO\n", 20);
            return;
        }
        
    }
    for (int i = 0; i < tx_count; i++) {
        snprintf(fifo_name, sizeof(fifo_name), "/tmp/teller_%d_res.fifo", i);
        teller_fifo_fds[i].teller_res_fifo_fd = open(fifo_name, O_RDONLY);
        if (teller_fifo_fds[i].teller_res_fifo_fd  == -1) {
            write(STDERR_FILENO, "Error opening FIFO\n", 20);
            return;
        }    
    }
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


