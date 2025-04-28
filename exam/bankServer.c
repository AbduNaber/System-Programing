 
#include "bankDefination.h"


#define TELLER_SHM_NAME "/teller_shm"


// global variables
pid_t tellers_pid[MAX_TX_PER_CLIENT];
int  active_tellers = 0;
pid_t mainServer_pid = 0;

client_t *clients_db = NULL;
int client_count_db = 0;
int log_fd = 0;
char *server_fifo_name = NULL;

int parse_clients_log(int fd, client_t *clients, int *client_count_out) ;
int get_or_create_client(client_t **clients, int *client_count, int *client_capacity, char * id) ;
void print_clients(client_t * clients, int client_count);
void clear_heap(client_t *clients, int client_count) ;
pid_t Teller (void* func, void* arg_func);
int find_client_index(client_t *clients, int client_count, const char *id);
void intToStr(int N, char *str);
server_response update_client(client_t *client, transaction_t *transaction) ;
int teller_client_map_find(teller_client_map_t *map,int teller_count, int teller_id) ;
void handle_sigint(int sig);
int update_log_file(int fd, client_t *clients, int client_count) ;
void save_log();
pid_t Teller (void* func, void* arg_func){
    pid_t teller_pid = fork();
    if(teller_pid == 0){
        // Child process
        if(func != NULL && arg_func != NULL){
            ((void (*)(void*))func)(arg_func);
        }
        exit(0);
    }
    return teller_pid;
}

void teller_helper(void *arg) {

    teller_arg_t *teller_arg = (teller_arg_t *)arg;
    int teller_fifo_number = teller_arg->teller_id;
    char teller_fifo_name[256];
    sleep(1);
    snprintf(teller_fifo_name, sizeof(teller_fifo_name), "/tmp/teller_%d_req.fifo", teller_fifo_number);
    
    int teller_fifo_req_fd = open(teller_fifo_name, O_RDONLY);
    if (teller_fifo_req_fd == -1) {
        write(STDERR_FILENO, "Error opening teller_req FIFO\n", 31);
        return;
    }
    
    snprintf(teller_fifo_name, sizeof(teller_fifo_name), "/tmp/teller_%d_res.fifo", teller_fifo_number);
    int teller_fifo_res_fd = open(teller_fifo_name, O_WRONLY);
    if (teller_fifo_res_fd == -1) {
        write(STDERR_FILENO, "Error opening teller FIFO\n", 27);
        return;
    }
    
    transaction_t transaction;
    read(teller_fifo_req_fd, &transaction, sizeof(transaction));
    // char output[256];
    // snprintf(output, sizeof(output), "Teller %d: Processing transaction for client %s: %s %d\n", teller_fifo_number, transaction.bank_id, transaction.op == DEPOSIT ? "Deposit" : "Withdraw", transaction.amount);
    // write(STDOUT_FILENO, output, strlen(output));

    int written = 0;
    while (!written) {
        sem_wait(teller_arg->sem);
    
        if (teller_arg->shared_teller->server_read == 1) {
            teller_arg->shared_teller->teller_id = teller_fifo_number;
            teller_arg->shared_teller->transaction = transaction;
            teller_arg->shared_teller->teller_pid = getpid();
            teller_arg->shared_teller->server_read = 0;  // Mark: server must read now
            written = 1; // exit the while loop
        }
    
        sem_post(teller_arg->sem);
    
        if (!written) sleep(0.1);  
    }
    
    while(teller_arg->response->response == INITIALIZE){
        sleep(0.1);
    }
    teller_res_t teller_res;
    teller_res.teller_id = teller_fifo_number; 
    teller_res.client_id = teller_arg->client_id;  
    memcpy(&teller_res.response, teller_arg->response, sizeof(server_response));
    write(teller_fifo_res_fd, &teller_res, sizeof(teller_res_t));
    


}

int waitTeller(pid_t pid, int *status) {
    return waitpid(pid, status, 0);
}

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

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // Or SA_RESTART if you want to auto-restart syscalls
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        write(STDERR_FILENO, "Error setting up signal handler\n", 33);
        exit(EXIT_FAILURE);
    }
    mainServer_pid = getpid();

    char *bank_name = argv[1];
    server_fifo_name = argv[2];

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

    int shm_fd = shm_open(TELLER_SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        
        if(errno == EEXIST) {
           write(STDERR_FILENO, "Shared memory for tellers already exists continue..\n", 53);
        } else {
            write(STDERR_FILENO, "Error creating shared memory\n", 30);
            return 1;
        }
    }

    ftruncate(shm_fd, sizeof(teller_t));
    teller_t *shared_teller = mmap(0, sizeof(teller_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_teller == MAP_FAILED) {
        write(STDERR_FILENO, "Error mapping shared memory\n", 29);
        return 1;
    }

   



    // Initialize the shared teller
    shared_teller->teller_id = -1;
    shared_teller->teller_pid =-1;
    shared_teller->transaction = (transaction_t){0};
    shared_teller->server_read = 1;

    sem_t *shared_sem = mmap(NULL, sizeof(sem_t),
    PROT_READ | PROT_WRITE,
    MAP_SHARED | MAP_ANONYMOUS,
    -1, 0);



    sem_init(shared_sem, 1, 1); 

    

    write_output(2,bank_name, " is waiting for clients...\n");

    char log_file_name[256] = "";
    strcat(log_file_name, bank_name);
    strcat(log_file_name, ".bankLog");
    int client_capacity = INITIAL_CLIENTS;
    clients_db = malloc(sizeof(client_t) * client_capacity);
    client_count_db = 0;
    log_fd = open(log_file_name, O_RDWR, 0644);
    if(log_fd == -1){
        if(errno == ENOENT){
            write_output(1,"No previous logs.. Creating the bank database\n");
            log_fd = open(log_file_name, O_RDWR | O_CREAT, 0644);
            if(log_fd == -1){
                write(STDERR_FILENO, "Error creating log file\n", 25); 
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
        if(parse_clients_log(log_fd,clients_db,&client_count_db) == -1){
            write(STDERR_FILENO, "Error loading logs\n", 20); 
            return 1;
        }
    }

    
        
        
    int served_client_counter = 0;
    while(1){

    

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
        write(STDERR_FILENO, "Error reading client info\n", 27);
        return 1;
    }

    

    char console_msg[256]= "";
    
    snprintf(console_msg, sizeof(console_msg), "Received %d clients from %dClient...",client_info.client_counter, client_info.pid);
    write_output(2, console_msg, "\n");
    
    int client_number = client_info.client_counter;
    
    
    teller_arg_t teller_arg[client_number];
    
    teller_client_map_t teller_client_map[client_number];

    server_response *responses = mmap(NULL, sizeof(server_response)*client_number, PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (responses == MAP_FAILED) {
        write(STDERR_FILENO, "Error mapping shared memory for responses\n", 43);
        return 1;
    }
    
    for (int i = 0; i < client_number; i++) {
        responses[i].response = INITIALIZE;
    }

    for (int i = 0; i < client_number; i++) {
        teller_client_map[i].teller_id = -1;
        teller_client_map[i].client_id = -1;
    }
   

    int teller_count = 0;
    // create tellers for each client
    for (int i = 0; i < client_number; i++) {

        int teller_client_id = find_client_index(clients_db,client_count_db, client_info.clients_name[i]) ;


        teller_arg[i].teller_id = i;
        teller_arg[i].shared_teller = shared_teller;
        teller_arg[i].sem = shared_sem; 
        teller_arg[i].response = &responses[i]; 
        teller_arg[i].client_id = teller_client_id == -1 ? i + served_client_counter : teller_client_id;
        tellers_pid[teller_count] = Teller(teller_helper, &teller_arg[i]);   
        active_tellers++;
        if (tellers_pid[teller_count++] == -1) {
            write(STDERR_FILENO, "Error creating teller\n", 23);
            return 1;
        }        
        if(teller_client_id == -1){
            snprintf(console_msg, sizeof(console_msg), "Teller PID%d is active serving client%d", tellers_pid[teller_count-1],i+served_client_counter);
            teller_client_map[teller_count-1].teller_id = tellers_pid[teller_count-1];
            teller_client_map[teller_count-1].client_id = i+served_client_counter;
            
        }
        else{
            snprintf(console_msg, sizeof(console_msg), "Teller PID%d is active serving client%d. Welcome Back ", tellers_pid[teller_count-1], teller_client_id);
            teller_client_map[teller_count-1].teller_id = tellers_pid[teller_count-1];
            teller_client_map[teller_count-1].client_id = teller_client_id;
        }
        
        write(client_fifo_fd, &teller_client_map[teller_count-1], sizeof(teller_client_map_t));
        write_output(2, console_msg, "\n");
        
    }
    teller_client_map_t empty = {-1,-1};
    write(client_fifo_fd, &empty, sizeof(teller_client_map_t));
    served_client_counter += client_number;

    for (int i = 0; i < teller_count; i++) {
        int updated = 0;
        while (!updated) {
            sem_wait(shared_sem);
            if (shared_teller->server_read == 0) {
                int teller_client_id = teller_client_map_find(teller_client_map, teller_count,shared_teller->teller_pid);
                snprintf(console_msg, sizeof(console_msg), "client%d %s %d credits… ",teller_client_id, shared_teller->transaction.op == DEPOSIT ? "deposited" : "withdraws", shared_teller->transaction.amount);
                write(STDOUT_FILENO, console_msg, strlen(console_msg));
                
                int client_index = get_or_create_client(&clients_db, &client_count_db, &client_capacity, shared_teller->transaction.bank_id);
                if( client_index == -1) {
                    responses[shared_teller->teller_id].response = FAILURE;
                    write(STDERR_FILENO, "operation not permitted..\n", 27);
                   
                }
                
                else{
                    server_response answer = update_client(&clients_db[client_index], &shared_teller->transaction);
                    responses[shared_teller->teller_id] = answer;
                    write(STDOUT_FILENO, "updating log..\n", 16);
                    if(update_log_file(log_fd, clients_db, client_count_db) == -1){
                        write(STDERR_FILENO, "Error updating log file\n", 25);
                    }
                }
                shared_teller->server_read = 1; // Mark as read
                updated = 1; // Done
            }
            sem_post(shared_sem);
            if (!updated) sleep(0.1); // Sleep 1ms
        }
        
    }

    close(server_fifo_fd);
    close(client_fifo_fd);
    int status;
    for (int i = 0; i < teller_count; i++) {
        
        waitTeller(tellers_pid[i], &status);
        
    }

}
    return 0;
}

int update_log_file(int fd, client_t *clients, int client_count) {

    if (ftruncate(fd, 0) == -1) {
        write(STDERR_FILENO, "Error truncating log file\n", 27);
        return -1;
    }
    // Reset file offset to beginning
    if (lseek(fd, 0, SEEK_SET) == -1) {
        write(STDERR_FILENO, "Error seeking in log file\n", 27);
        return -1;
    }

    char buffer[512];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);

    // 1. Write header
    strftime(buffer, sizeof(buffer), "# Adabank Log file updated @%H:%M %B %d %Y\n", tm_info);
    if (write(fd, buffer, strlen(buffer)) == -1) return -1;

    // 2. Write client lines
    for (int i = 0; i < client_count; i++) {
        client_t *client = &clients[i];

        int len = 0;
        if (client->credits == 0) {
            len = snprintf(buffer, sizeof(buffer), "# %s", client->bank_id);
        } else {
            len = snprintf(buffer, sizeof(buffer), "%s", client->bank_id);
        }

        if (len < 0 || len >= (int)sizeof(buffer)) return -1;

        for (int j = 0; j < client->transaction_count; j++) {
            transaction_t *t = &client->transactions[j];

            const char *op_str = (t->op == 1) ? "D" : "W";
            int written = snprintf(buffer + len, sizeof(buffer) - len, " %s %d", op_str, t->amount);
            if (written < 0 || written >= (int)(sizeof(buffer) - len)) return -1;
            len += written;
        }

        // Write credits at the end
        int written = snprintf(buffer + len, sizeof(buffer) - len, " %d\n", client->credits);
        if (written < 0 || written >= (int)(sizeof(buffer) - len)) return -1;
        len += written;

        if (write(fd, buffer, len) == -1) return -1;
    }

    // 3. Write footer
    const char *end_log = "## end of log.\n";
    if (write(fd, end_log, strlen(end_log)) == -1) return -1;

    return 0;
}

int teller_client_map_find(teller_client_map_t *map,int teller_count, int teller_id) {
    for (int i = 0; i < teller_count; i++) {
        if (map[i].teller_id == teller_id) {
            return map[i].client_id;
        }
    }
    return -1; 
}

server_response update_client(client_t *client, transaction_t *transaction) {
    server_response res;
    strcpy(res.bank_id, client->bank_id);

   
    if (transaction->op == DEPOSIT) {
        client->credits += transaction->amount;
    } else if (transaction->op == WITHDRAW) {
        if (client->credits < transaction->amount) {
            res.response = INSUFFICIENT_CREDITS;
            return res; // Insufficient credits
        }
        client->credits -= transaction->amount;
        
    }
    for (int i = 0; i < MAX_TX_PER_CLIENT; i++) {
        if (client->transactions[i].op == 0) { // Find an empty slot
            client->transactions[i] = *transaction;
            break;
        }

    }
    client->transaction_count++;
    if(client->credits == 0){
        res.response = ACCOUNT_DELETED;
        return res; // Account deleted
    }
    res.response = SUCCESS;
    return res; // Success
    
}

int get_or_create_client(client_t **clients, int *client_count, int *client_capacity, char * id) {
    // Check if the client already exists
    for (int i = 0; i < *client_count; i++) {
        if (strcmp((*clients)[i].bank_id, id) == 0) {
            return i; // Client already exists
        }
    }
    if(strcmp(id, "N") == 0){
            // Need to create a new one
        if (*client_count >= *client_capacity) {
            int new_cap = (*client_capacity) * 2;
            client_t *new_clients = realloc(*clients, sizeof(client_t) * new_cap);
            if (!new_clients) return -1;
            *clients = new_clients;
            *client_capacity = new_cap;
        }

        client_t *c = &(*clients)[*client_count];
        snprintf(c->bank_id, sizeof(c->bank_id), "BankID_%d",*client_count + 1);
        
        c->credits = 0;
        c->transactions = malloc(sizeof(transaction_t) * MAX_TX_PER_CLIENT);
        if (!c->transactions) return -1;
        (*client_count)++;
        return (*client_count - 1);
    }
    else{
        return -1;
    }   
    
}

int load_client_from_log(client_t **clients, int *client_count, int *client_capacity, char * id){
   
    if (*client_count >= *client_capacity) {
        int new_cap = (*client_capacity) * 2;
        client_t *new_clients = realloc(*clients, sizeof(client_t) * new_cap);
        if (!new_clients) return -1;
        *clients = new_clients;
        *client_capacity = new_cap;
    }

    client_t *c = &(*clients)[*client_count];
    snprintf(c->bank_id, sizeof(c->bank_id), "%s", id);
    
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
            int client_index = load_client_from_log(&clients, &client_count, &client_capacity, client_id);
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
                client->transaction_count = tx_count;
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
        printf("Client ID: %s\n", clients[i].bank_id);
        printf("Credits: %d\n", clients[i].credits);
        printf("Transactions:\n");
        for(int j = 0; j < MAX_TX_PER_CLIENT; j++) {
            if(clients[i].transactions[j].op == DEPOSIT) {
                printf("Deposit: %d\n", clients[i].transactions[j].amount);
            } else if(clients[i].transactions[j].op == WITHDRAW) {
                printf("Withdraw: %d\n", clients[i].transactions[j].amount);
            }
        }
        printf("------------------------\n");
    }
}

void clear_heap(client_t *clients, int client_count) {
    for (int i = 0; i < client_count; i++) {
        free(clients[i].transactions);
    }
    free(clients);
}

int find_client_index(client_t *clients, int client_count, const char *id) {
    for (int i = 0; i < client_count; i++) {
        if (strcmp(clients[i].bank_id, id) == 0) {
            return i;
        }
    }
    return -1; // Not found
}

int extract_client_id(char *bank_id) {
    char *underscore = strchr(bank_id, '_');
    return atoi(underscore + 1);
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

void handle_sigint(int sig) {

    if (sig != SIGINT) return;
    if (mainServer_pid != getpid()) return;
    write(STDOUT_FILENO, "\nSignal received closing active tellers\n", 41);
    for (int i = 0; i < active_tellers; i++) {
        if (kill(tellers_pid[i], SIGKILL) == 0) {
        } else if (errno == ESRCH) {
        } else {
            write(STDERR_FILENO, "Error killing teller\n", 22);
        }
    }
    write(STDOUT_FILENO, "Removing ServerFIFO… Updating log file…\n" , 45);
    save_log();
    clear_heap(clients_db, client_count_db);
    client_count_db = 0;
    clients_db = NULL;
    if (log_fd != -1) {
        close(log_fd);
    }
    unlink(CLIENT_FIFO_NAME);
    unlink(server_fifo_name);
    write(STDOUT_FILENO, "Bye...\n", 8);
    exit(0);
}


void save_log(){
    if (log_fd == -1) {
        write(STDERR_FILENO, "Error opening log file\n", 24); 
        return;
    }
    if (update_log_file(log_fd, clients_db, client_count_db) == -1) {
        write(STDERR_FILENO, "Error updating log file\n", 25); 
        return;
    }
    close(log_fd);
}