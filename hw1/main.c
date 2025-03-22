
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>


// Function prototypes
int createDir(const char *folderName);
int createFile(const char *fileName);
int listDir(const char *folderName);
int listFilesByExtension(const char *folderName, const char *extension);
int readFile(const char *fileName);
int appendToFile(const char *fileName,const char *content);
int deleteFile(const char *fileName);
int deleteDir(const char *folderName);
int showLogs();
void log(const char *message);
#define	EXIT_FAILURE	1	
#define	EXIT_SUCCESS	0	


int main(int argc, char const *argv[])
{
    
   if(argc == 1){
        write(1, "fileManager\n\n", 14);
        write(1, "Example output:\nUsage: fileManager <command> [arguments]\n\n", 59);
        write(1, "createDir \"folderName\"                      - Create a new directory\n", 70);
        write(1, "createFile \"fileName\"                       - Create a new file\n", 65);
        write(1, "listDir \"folderName\"                        - List all files in a directory\n", 77);
        write(1, "listFilesByExtension \"folderName\" \".txt\"    - List files with specific extension\n", 82);
        write(1, "readFile \"fileName\"                         - Read a file's content\n", 69);
        write(1, "appendToFile \"fileName\" \"new content\"       - Append content to a file\n", 72);
        write(1, "deleteFile \"fileName\"                       - Delete a file\n", 61);
        write(1, "deleteDir \"folderName\"                      - Delete an empty directory\n", 73);
        write(1, "showLogs                                    - Display operation logs\n", 70);
   }
   
   else{
        if(strcmp(argv[1], "createDir") == 0 && argc == 3){
            createDir(argv[2]);
        }
        else if(strcmp(argv[1], "createFile") == 0 && argc == 3){
            createFile(argv[2]);
        }
        else if(strcmp(argv[1], "listDir") == 0 && argc == 3){ 
            listDir(argv[2]);
        }
        else if(strcmp(argv[1], "listFilesByExtension") == 0 && argc == 4){
            listFilesByExtension(argv[2], argv[3]);
        }
        else if(strcmp(argv[1], "readFile") == 0 && argc == 3){ 
            readFile(argv[2]);
        }
        else if(strcmp(argv[1], "appendToFile") == 0 && argc == 4){
            appendToFile(argv[2], argv[3]);
        }
        else if(strcmp(argv[1], "deleteFile") == 0 && argc == 3){
            deleteFile(argv[2]);
        }
        else if(strcmp(argv[1], "deleteDir") == 0 && argc == 3){
            deleteDir(argv[2]);
        }
        else if(strcmp(argv[1], "showLogs") == 0 && argc == 2){
            showLogs();
        }
        else{
            
            write(1, "Invalid command\n", 17);
        }
        
   }
   
    return 0;
}


// Function to create a directory
int createDir(const char *folderName){

    if(folderName == NULL){
        write(1,"Error: Folder name is null." , 28);
        return EXIT_FAILURE;
    }

    // Check if the directory already exists
    struct stat st = {0};
    if(stat(folderName, &st) == 0){
        write(1,"Error: Directory already exists." , 33);
        return EXIT_FAILURE;
    }
    else if(mkdir(folderName, 0777 ) == 0 && errno != EEXIST){
        
        write(1,"Directory created successfully\n" , 32);
        return EXIT_SUCCESS;
    }
    else{
        return EXIT_FAILURE;
    }

    
}

int createFile(const char *fileName){

    

    int fd = open(fileName,O_CREAT | O_EXCL | O_WRONLY	, 0644); 
    if(fd == -1){
       
        if(errno == EEXIST){
            write(1,"Error: File already exists.\n" , 29);
        }
        else if (errno == EACCES){
            write(1,"Error: Permission denied.\n" , 27);
        }
        else {
            write(1,"Error: File could not be created.\n" , 35);
        }
    }
    else{
        
        time_t timestamp = time(NULL);
        char * timeStr = ctime(&timestamp);
        write(fd, timeStr, strlen(timeStr));
        close(fd);
        return EXIT_SUCCESS;
    }
   
}


int listDir(const char *folderName){

    int pid = fork();

    if(pid == 0){
        DIR *mydir;
        struct dirent *myfile;
        mydir = opendir(folderName);
        if(mydir == NULL){
        if(errno == EACCES)
            write(1,"Error: Permission denied.\n" , 27);
        else if (errno == ENOENT)
            write(1,"Error: Directory does not exist.\n" , 34);
        else
            write(1,"Error: Could not open directory.\n" , 34);
        }
        while((myfile = readdir(mydir)) != NULL){
        write(1, myfile->d_name, strlen(myfile->d_name));
        write(1, "\n", 1);
        }
        closedir(mydir);
        _exit(0);
        
    }
    else{
        int status = 0;
        waitpid(pid, &status, 0);
        if (status != 0){
            write(1,"Error: Could not list directory.\n" , 34);
            return EXIT_FAILURE;
        }        
    }
    return EXIT_SUCCESS;
    
    
    
}

int listFilesByExtension(const char *folderName, const char *extension){

    int pid = fork();

    if(pid == 0){
        DIR *mydir;
        struct dirent *myfile;
        mydir = opendir(folderName);
        if(mydir == NULL){
        if(errno == EACCES)
            write(1,"Error: Permission denied.\n" , 27);
        else if (errno == ENOENT)
            write(1,"Error: Directory does not exist.\n" , 34);
        else
            write(1,"Error: Could not open directory.\n" , 34);
        }
        while((myfile = readdir(mydir)) != NULL){
            if(strstr(myfile->d_name, extension) != NULL){
                write(1, myfile->d_name, strlen(myfile->d_name));
                write(1, "\n", 1);
            }
        }
        
    }
    else{
        int status = 0;
        waitpid(pid, &status, 0);
        if (status != 0){
            write(1,"Error: Could not list directory.\n" , 34);
            return EXIT_FAILURE;
        }        
    }
    return EXIT_SUCCESS;
}

int readFile(const char *fileName){

    int fd = open(fileName, O_RDONLY);
    if(fd == -1){
        if(errno == EACCES){
            write(1,"Error: Permission denied.\n" , 27);
        }
            
        else if (errno == ENOENT){
            write(1,"Error: " , 8);
            write(1, fileName, strlen(fileName));
            write(1," not found.\n" , 13);
        }
        else
            write(1,"Error: Could not open file.\n" , 29);
    }
    else{
        char buffer[1024];
        int bytesRead = read(fd, buffer, 1024);
        write(1, buffer, bytesRead);
        close(fd);
        return EXIT_SUCCESS;
    }


}

int appendToFile(const char *fileName,const char *content){

    int fd = open(fileName, O_WRONLY | O_APPEND);
    if(fd == -1){
        if(errno == EACCES){
            write(1,"Error: " , 8);
            write(1, fileName, strlen(fileName));
            write(1," . File is locked or read-only.\n" , 33);
        }
            
        else if (errno == ENOENT){
            write(1,"Error: " , 8);
            write(1, fileName, strlen(fileName));
            write(1," not found.\n" , 13);
        }
        else
            write(1,"Error: Could not open file.\n" , 29);
    }
    else{
        // Lock the file
        if (flock(fd, LOCK_EX) == -1) {
            close(fd);
            return 1;
        }

        // Append content to the file
        write(fd, content, strlen(content));


        flock(fd, LOCK_UN);

        close(fd);
        return EXIT_SUCCESS;
}

}

int deleteFile(const char *fileName){

    int pid = fork();

    if(pid == 0){
        if(unlink(fileName) == -1){
            if(errno == EACCES){
                write(1,"Error: Permission denied.\n" , 27);
            }
            else if (errno == ENOENT){
                write(1,"Error: " , 8);
                write(1, fileName, strlen(fileName));
                write(1," not found.\n" , 13);
            }
            else
                write(1,"Error: Could not delete file.\n" , 31);

            _exit(1);
        }
        else{
            write(1,"File deleted successfully.\n" , 28);
        }

        _exit(0);
    }
    else{
        int status = 0;
        waitpid(pid, &status, 0);
        if (status != 0){
            write(1,"Error: Could not delete file.\n" , 31);
            return EXIT_FAILURE;
        }        
    }
}

int deleteDir(const char *folderName){

    int pid = fork();

    if(pid == 0){
        if(rmdir(folderName) == -1){
            if(errno == EACCES){
                write(1,"Error: Permission denied.\n" , 27);
            }
            else if (errno == ENOENT){
                write(1,"Error: " , 8);
                write(1, folderName, strlen(folderName));
                write(1," not found.\n" , 13);
            }
            else if (errno == ENOTEMPTY){
                write(1,"Error: Directory is not empty.\n" , 32);
            }
            else
                write(1,"Error: Could not delete directory.\n" , 35);

            _exit(1);
        }
    
        _exit(0);
    }
    else{
        int status = 0;
        waitpid(pid, &status, 0);
        if (status != 0){
            write(1,"Error: Could not delete directory.\n" , 35);
            return EXIT_FAILURE;
        }        
    }
}


void log(const char *message){
    int fd = open("log.txt", O_CREAT |  O_WRONLY | O_APPEND, 0644);
    if(fd == -1){
       if (errno == EACCES){
           write(1,"Error: Permission denied for log.txt\n" , 27);
       }
       else{
           write(1,"Error: Could not open log file.\n" , 33);
       }
    }
    else{
        time_t timestamp = time(NULL);
        char * timeStr = ctime(&timestamp);
        write(fd, timeStr, strlen(timeStr));
        write(fd, " ", 2);
        write(fd, message, strlen(message));
        write(fd, "\n", 2);
        close(fd);
    }



}


int showLogs(){}

