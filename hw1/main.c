#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

// Function prototypes
int createDir(char *folderName);
int createFile(char *fileName);
int listDir(char *folderName);
int listFilesByExtension(char *folderName, char *extension);
int readFile(char *fileName);
int appendToFile(char *fileName, char *content);
int deleteFile(char *fileName);
int deleteDir(char *folderName);
int showLogs();



int main(int argc, char const *argv[])
{
    // when user enters no arguments, show the usage
   if(argc == 0){
        printf("fileManager\n\n");
        printf("Example output:\nUsage: fileManager <command> [arguments]\n\n");
        printf("%-40s - %s\n", "createDir \"folderName\"", "Create a new directory");
        printf("%-40s - %s\n", "createFile \"fileName\"", "Create a new file");
        printf("%-40s - %s\n", "listDir \"folderName\"", "List all files in a directory");
        printf("%-40s - %s\n", "listFilesByExtension \"folderName\" \".txt\"", "List files with specific extension");
        printf("%-40s - %s\n", "readFile \"fileName\"", "Read a file's content");
        printf("%-40s - %s\n", "appendToFile \"fileName\" \"new content\"", "Append content to a file");
        printf("%-40s - %s\n", "deleteFile \"fileName\"", "Delete a file");
        printf("%-40s - %s\n", "deleteDir \"folderName\"", "Delete an empty directory");
        printf("%-40s - %s\n", "showLogs", "Display operation logs");
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
            printf("Invalid command\n");
        }
   }
    return 0;
}


// Function to create a directory
int createDir(char *folderName){

    // Check if the directory already exists
    struct stat st = {0};
    if(stat(folderName, &st) == 0){
        printf("Error: Directory %s already exists.",folderName);
        return EXIT_FAILURE;
    }
    else if(mkdir(folderName) == 0 && errno != EEXIST){
        printf("Directory created successfully\n");
        return EXIT_SUCCESS;
    }
    else{
        return EXIT_FAILURE;
    }

    
}

int createFile(char *fileName){

    struct stat st = {0};
    if(stat(fileName, &st) == 0){
        printf("Error: File %s already exists.",fileName);
        return EXIT_FAILURE;
    }


    int fd = open(fileName,O_CREAT | O_EXCL | O_WRONLY	, 0644); 
    if(fd == -1){
       
        return EXIT_FAILURE;
    }
    else{
        time_t timestamp = time(NULL);
        char formatted_time[100];
        strftime(formatted_time, sizeof(formatted_time), "%l %p", timestamp);
        write(fd, formatted_time, strlen(formatted_time));
        
        close(fd);
        return EXIT_SUCCESS;
    }
   
}