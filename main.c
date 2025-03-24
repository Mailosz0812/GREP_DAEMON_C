#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>

#define EXIT_NO_ARGS 1
void lookup(char **args,char* path);
void checkForFile(char *dName,char **args);
void daemonize();

int main(int argc, char ** argv){

    daemonize();

    lookup(argv,"/home");
    exit(0);
}

void lookup(char **args,char* path){
    DIR *directory;
    struct dirent *dp;

    if((directory = opendir(path)) == NULL){
        printf("Cannot open: %s\n ",path);
        return;
    }

    while((dp = readdir(directory)) != NULL){
        if(strcmp(dp->d_name,".") == 0 || strcmp(dp->d_name,"..") == 0){
            continue;
        }
        char fullPath[PATH_MAX];
        snprintf(fullPath,sizeof(fullPath),"%s/%s",path,dp->d_name);

        struct stat statbuf;
        if(lstat(fullPath,&statbuf) == -1)continue;
        checkForFile(dp->d_name,args);

        if(S_ISDIR(statbuf.st_mode) || S_ISLNK(statbuf.st_mode)){
            if(access(fullPath,R_OK | X_OK) == 0){
                lookup(args,fullPath);
            }
        }
    }
    closedir(directory);
}
void checkForFile(char *dName,char **args){
    int i = 1;
    char* temp = args[i];
    while(temp != NULL){
        if(strcmp(temp,dName) == 0){
            printf("File found %s \n",dName);
            syslog(LOG_INFO, "File found %s", dname);
        }
        i++;
        temp = args[i];
    }
}

void daemonize() {
    pid_t pid;

    printf("Starting daemon...\n");
    fflush(stdout);

    pid = fork();

    if (pid < 0) 
    {
        perror("fork failed");
        exit(EXIT_FAILURE);
    }

    if (pid > 0) 
    {
        exit(EXIT_SUCCESS);
    }

    if (setsid() < 0) 
    {
        perror("setsid failed");
        exit(EXIT_FAILURE);
    }

    // Ignorujemy sygnał SIGHUP
    // signal(SIGHUP, SIG_IGN);

    pid = fork();
    if (pid < 0) 
    {
        perror("Second fork failed");
        exit(EXIT_FAILURE);
    }

    if (pid > 0) 
    {
        exit(EXIT_SUCCESS);
    }

    // Zmieniamy katalog roboczy na root, aby nie blokować unmountowania
    if (chdir("/") < 0) 
    {
        perror("chdir failed");
        exit(EXIT_FAILURE);
    }
    
    // Zamykamy standardowe deskryptory plików
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    // Przekierowujemy STDIN, STDOUT, STDERR na /dev/null
    open("/dev/null", O_RDONLY);
    open("/dev/null", O_WRONLY);
    open("/dev/null", O_RDWR);
    
    // Ustawienie umask na 0 dla pełnej kontroli nad plikami
    umask(0);

    openlog("file_search_daemon", LOG_PID, LOG_DAEMON);
    syslog(LOG_INFO, "Daemon started successfully");
}
