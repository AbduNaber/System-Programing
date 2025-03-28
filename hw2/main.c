#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1


#define LOG_PATH "/home/abdu/System-Programing/hw2/log.txt"
int daemon_initialise() {

    pid_t pid;
    if ((pid = fork()) < 0) {
        return -1;
    } else if (pid != 0) {
        _exit(0); 
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


    
    // Close all open file descriptors
    int x;
    for (x = sysconf(_SC_OPEN_MAX); x>=0; x--)
    {
        close (x);
        
    }

    int log_fd = open(LOG_PATH, O_CREAT | O_WRONLY | O_APPEND , 0777);  

    if (log_fd == -1) {
        
        if (errno == ENOENT) {
            write(1, "Error, logFile not found.\n" , 27);
        }
        else if (errno == EACCES) {
            write(1, "Error, Permission Denied.\n" , 27);
        }
        else {
            write(1, "Error, open() failed.\n", 23);
        }
        return -1;
    }

    int dev_null = open("/dev/null", O_RDWR);  
    

    dup2(log_fd, STDOUT_FILENO);  // Redirect stdout to log.txt
    dup2(log_fd, STDERR_FILENO);  // Redirect stderr to log.txt
    dup2(dev_null, STDIN_FILENO);  // redirect to stdin to dev_null
    
    
    return 0; // Daemon initialized successfully
}

int main() {
    if (daemon_initialise() < 0) {
        _exit(EXIT_FAILURE);
    }

    
    
    while(1){
        printf("test\n");
        fflush(stdout); 
        
        sleep(1);
    }

    

    return 0;
}
