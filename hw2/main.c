#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1


#define LOG_PATH "/home/abdu/System-Programing/hw2/log.txt"
int daemon_procces() {

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
    char commands[100][100] = {
        "add",
        "sub",
        "mul",
        "div",
        "largest",
        "smallest",
        "avg",
        "mod",
        "exit"
    };

    return 0;
}
