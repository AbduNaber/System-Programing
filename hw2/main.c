#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <signal.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1


#define LOG_PATH "/home/abdu/System-Programing/hw2/log.txt"

static int process_count = 0;

int daemon_procces( ) {

    pid_t pid = fork();
    if (pid < 0) {
        write(STDERR_FILENO, "Error, fork() failed.\n", 23);
        return -1;
    }
    // in parent process, go to main function
    if (pid > 0) {
    
        return 0;
    }
    
    
    
    //Create a new session
    if (setsid() < 0) {
        if (errno == EPERM) {
            write(STDERR_FILENO, "Error, Permission Denied.\n" , 27);
        }
       else {
            write(STDERR_FILENO, "Error, setsid() failed.\n", 25);
        }
        return -1;
       }
    
    pid = fork();
    if (pid < 0) {
        write(STDERR_FILENO, "Error, fork() failed.\n", 23);
        return -1;
    }
    else if (pid > 0) {
        _exit(EXIT_SUCCESS);
    }   

    // change the working directory to root
    if (chdir("/") < 0) {
        if(errno == EACCES) {
            write(STDERR_FILENO, "Error, Permission Denied.\n" , 27);
        }
        else {
            write(STDERR_FILENO, "Error, chdir() failed.\n", 24);
        }
    }

    // Reset file permissions
    umask(0);


    
    
    /// Close all open file descriptors
    int x;
    for (x = sysconf(_SC_OPEN_MAX); x >= 0; x--) {
        if (x != STDOUT_FILENO && x != STDERR_FILENO && x != STDIN_FILENO) {
            close(x);
        }
    }

    // Open the necessary files
    int log_fd = open(LOG_PATH, O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (log_fd == -1) {
        perror("Failed to open log file");
        return 1;
    }

    int dev_null = open("/dev/null", O_RDWR);
    if (dev_null == -1) {
        perror("Failed to open /dev/null");
        close(log_fd);
        return 1;
    }

    
    dup2(dev_null, STDIN_FILENO);  // Redirect stdin to /dev/null
    dup2(log_fd, STDOUT_FILENO);   // Redirect stdout to log file
    dup2(log_fd, STDERR_FILENO);   // Redirect stderr to log file

    const char *message = "Daemon is running...\n";
    
    // Main daemon loop
    while (1) {
        sleep(1);
        write(STDOUT_FILENO, message, strlen(message));
    }

    return 0; // Daemon initialized successfully

}

int main(int argc, char *argv[]) {


    if(argc != 3) {


        write(1, "Error, Invalid number of arguments.\n", 37);
        return EXIT_FAILURE;
    }
    else {

        // check if the args are int 
        for(int i = 1; i < 3; i++) {
            for(int j = 0; argv[i][j] != '\0'; j++) {
                if(argv[i][j] < '0' || argv[i][j] > '9') {
                    write(1, "Error, Invalid argument.\n", 26);
                    return EXIT_FAILURE;
                }
            }
        }
    }

    // create the daemon process
    if (daemon_procces() < 0) {
        _exit(EXIT_FAILURE);
    }

    
    
    int result = 0;
    int arg1 = atoi(argv[1]);
    int arg2 = atoi(argv[2]);


    
    const char *fifo1 = "pipe1";
    const char *fifo2 = "pipe2";
    int fifo1_fd = -1;
    int fifo2_fd = -1;
    
    // Create the first pipe
    if (mkfifo(fifo1, 0666) == -1) {
        if (errno == EEXIST) {
            printf("Note: pipe1 already exists, continuing...\n");
        } else {
            perror("Error creating pipe1");
            return EXIT_FAILURE;
        }
    }
    
    // Create the second pipe
    if (mkfifo(fifo2, 0666) == -1) {
        if (errno == EEXIST) {
            printf("Note: pipe2 already exists, continuing...\n");
        } else {
            perror("Error creating pipe2");
            // Clean up resources before exiting
            unlink(fifo1);
            return EXIT_FAILURE;
        }
    }
    
    // Open the first pipe for writing
    fifo1_fd = open(fifo1, O_WRONLY);
    if (fifo1_fd == -1) {
        if (errno == ENOENT) {
            perror("Error opening pipe1 due to non-existing file");
        } else {
            perror("Error opening pipe1");
        }
        // Clean up resources before exiting
        unlink(fifo1);
        unlink(fifo2);
        return EXIT_FAILURE;
    }
    
    // Open the second pipe for writing
    fifo2_fd = open(fifo2, O_WRONLY);
    if (fifo2_fd == -1) {
        if (errno == ENOENT) {
            perror("Error opening pipe2 due to non-existing file");
        } else {
            perror("Error opening pipe2");
        }
        // Clean up resources before exiting
        close(fifo1_fd);
        unlink(fifo1);
        unlink(fifo2);
        return EXIT_FAILURE;
    }

    write(fifo1_fd, &arg1, sizeof(arg1));
    write(fifo1_fd, &arg2, sizeof(arg2));
   
    int pid1 = fork();
    process_count++;
    if (pid1 < 0) {
        perror("Error creating child process");
        
        // Clean up resources before exiting
        close_all_fifo(fifo1_fd, fifo2_fd, fifo1, fifo2);
        process_count--;
        return EXIT_FAILURE;
    } 
    else if (pid1 == 0) {
        
        
        child1(fifo1_fd);
        // Close the FIFO file descriptor
        _exit(EXIT_SUCCESS);
    } 
    else {
        // Parent process
        int pid2 = fork();
        process_count++;
        if (pid2 < 0) {
            perror("Error creating child process");
            
            // Clean up resources before exiting
            close_all_fifo(fifo1_fd, fifo2_fd, fifo1, fifo2);
            process_count--;
            return EXIT_FAILURE;
        } 
        else if (pid2 == 0) {
            // Child process 2
            
            child2(fifo2_fd);
            
            _exit(EXIT_SUCCESS);
        }

        // Parent process
        // Wait for child processes to finish
        int status;
        waitpid(pid1, &status, 0);
        if (WIFEXITED(status)) {
            printf("Child process 1 exited with status %d\n", WEXITSTATUS(status));
        } else {
            printf("Child process 1 terminated abnormally\n");
        }
        waitpid(pid2, &status, 0);
        if (WIFEXITED(status)) {
            printf("Child process 2 exited with status %d\n", WEXITSTATUS(status));
        } else {
            printf("Child process 2 terminated abnormally\n");
        }
        // Close the FIFO file descriptors
        close_all_fifo(fifo1_fd, fifo2_fd, fifo1, fifo2);
       
    }
    

    
    return 0;
}


int child1(char *fifo1) {

    sleep(10);

    int fifo1_fd = open(fifo1, O_RDONLY);
    if (fifo1_fd == -1) {
        if (errno == ENOENT) {
            perror("Error opening pipe1 due to non-existing file");
        } else {
            perror("Error opening pipe1");
        }
        return EXIT_FAILURE;
    }

    int arg1, arg2;
    // Read the integers from the FIFO
    if (read(fifo1_fd, &arg1, sizeof(arg1)) == -1) {
        perror("Error reading from pipe1");
        close(fifo1_fd);
        return EXIT_FAILURE;
    }
    if (read(fifo1_fd, &arg2, sizeof(arg2)) == -1) {
        perror("Error reading from pipe1");
        close(fifo1_fd);
        return EXIT_FAILURE;
    }

    int biggest = arg1 > arg2 ? arg1 : arg2;

    int fifo2_fd = open("pipe2", O_WRONLY);
    if (fifo2_fd == -1) {
        if (errno == ENOENT) {
            perror("Error opening pipe2 due to non-existing file");
        } else {
            perror("Error opening pipe2");
        }
        close(fifo1_fd);
        return EXIT_FAILURE;
    }
    else {
        // Write the biggest number to the second FIFO
        if (write(fifo2_fd, &biggest, sizeof(biggest)) == -1) {
            perror("Error writing to pipe2");
            close(fifo1_fd);
            close(fifo2_fd);
            return EXIT_FAILURE;
        }
    }

    _exit(EXIT_SUCCESS);
}


int child2(char *fifo2) {
    sleep(10);
    int fifo2_fd = open(fifo2, O_RDONLY);
    if (fifo2_fd == -1) {
        if (errno == ENOENT) {
            perror("Error opening pipe2 due to non-existing file");
        } else {
            perror("Error opening pipe2");
        }
        return EXIT_FAILURE;
    }
    int biggest;
    // Read the biggest number from the FIFO
    if (read(fifo2_fd, &biggest, sizeof(biggest)) == -1) {
        perror("Error reading from pipe2");
        close(fifo2_fd);
        return EXIT_FAILURE;
    }
    printf("The biggest number is: %d\n", biggest);
    
   
    _exit(EXIT_SUCCESS);
}

/**
 * Close all open FIFO file descriptors and unlink the FIFOs.
 */
void close_all_fifo(int fifo1_fd, int fifo2_fd , char *fifo1, char *fifo2) {
    if (fifo1_fd != -1) {
        close(fifo1_fd);
    }
    if (fifo2_fd != -1) {
        close(fifo2_fd);
    }
    unlink(fifo1);
    unlink(fifo2);
}

int SIGCHLD_handler(int signum ) {
    int status;
    pid_t pid;
 
    
    pid = waitpid(-1, &status, WNOHANG);
    if (pid == -1) {
        perror("waitpid");
        return -1;
    }
    else if (pid == 0) {
        // No child process has exited
        return 0;
    }
    else if (WIFEXITED(status)) {
        // Child process exited normally
        printf("process %d exited with status %d\n", pid, WEXITSTATUS(status));
    } else {
        // Child process terminated abnormally
        printf("Child process %d terminated abnormally\n", pid);
    }

    // Decrement the process count
    // and check if all child processes have exited
    // If all child processes have exited, exit the daemon
    if(process_count > 0) {
        process_count--;

    }
    else if (process_count == 0) {
        
        _exit(EXIT_SUCCESS);
    }
   
    
    return 0;
}