#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

int daemon_initialise() {

    pid_t pid;
    if ((pid = fork()) < 0) {
        return -1;
    } else if (pid != 0) {
        _exit(0); 
    }

    //Create a new session
    if (setsid() < 0) {
        return -1; 
    }

    // change the working directory to root
    if (chdir("/") < 0) {
        return -1; 
    }

    // Reset file permissions
    umask(0);

    
    int dev_null = open("/dev/null", O_RDWR);  

    if (dev_null == -1) {
        
        return 1;
    }

    // Close all open file descriptors
    int x;
    for (x = sysconf(_SC_OPEN_MAX); x>=0; x--)
    {
        close (x);
    }

    //Redirect stdin, stdout, and stderr to /dev/null using dup2()
    dup2(dev_null, STDIN_FILENO);   // Redirect stdin to /dev/null
    dup2(dev_null, STDOUT_FILENO);  // Redirect stdout to /dev/null
    dup2(dev_null, STDERR_FILENO);  // Redirect stderr to /dev/null

    
    
    return 0; // Daemon initialized successfully
}

int main() {
    if (daemon_initialise() < 0) {
        // Failed to initialize daemon
        exit(EXIT_FAILURE);
    }

    // Daemon logic (e.g., perform tasks in the background)
    while (1) {
        // Example: Sleep for 10 seconds
        sleep(10);
    }

    return 0;
}
