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
#include <signal.h>
#include <getopt.h> 

#define EXIT_NO_ARGS 1
volatile sig_atomic_t wakeup_signal = 0;

void lookup(char **args,char* path);
void checkForFile(char *dName,char **args);
void handle_signal(int sig);
void sleep_with_signals(int sleep_time);
void daemonize();

// int main(int argc, char ** argv){

//     // daemonize();

//     // lookup(argv,"/home");
//     // exit(0);

//     if (argc < 2) 
//     {
//         fprintf(stderr, "Usage: %s Location FileName ...\n", argv[0]);
//         exit(EXIT_FAILURE);
//     }

//     daemonize();

//     int sleep_time = 60;
//     while (1) 
//     {
//         syslog(LOG_INFO, "Starting file search...");
//         lookup(argv, "/home");
//         syslog(LOG_INFO, "Search complete. Sleeping for %d seconds...", sleep_time);
//         sleep_with_signals(sleep_time);
//     }
//     return 0;
// }

int main(int argc, char **argv) {
    int opt;
    int sleep_time = 60;

    while ((opt = getopt(argc, argv, "t:")) != -1) {
        switch (opt) {
            case 't':
                sleep_time = atoi(optarg);
                if (sleep_time <= 0) {
                    fprintf(stderr, "Invalid sleep time. Using default 60s.\n");
                    sleep_time = 60;
                }
                break;
            default:
                fprintf(stderr, "Usage: %s [-t sleep_time] FileName ...\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Usage: %s [-t sleep_time] FileName ...\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char **file_names = &argv[optind];

    daemonize();

    while (1) {
        syslog(LOG_INFO, "Starting file search...");
        lookup(file_names, "/home");  // Poprawione przekazanie argumentów
        syslog(LOG_INFO, "Search complete. Sleeping for %d seconds...", sleep_time);
        sleep_with_signals(sleep_time);
    }
    return 0;
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
    int i = 0;
    char* temp = args[i];
    while(temp != NULL){
        if(strcmp(temp,dName) == 0){
            printf("File found %s \n",dName);
            syslog(LOG_INFO, "File found %s", dName);
        }
        i++;
        temp = args[i];
    }
}

void handle_signal(int sig) {
    if (sig == SIGUSR1) {
        syslog(LOG_INFO, "Received SIGUSR1: Waking up");
        wakeup_signal = 1;
    } else if (sig == SIGUSR2) {
        syslog(LOG_INFO, "Received SIGUSR2: Stopping daemon");
        exit(EXIT_SUCCESS);
    }
}

void sleep_with_signals(int sleep_time) {
    for (int i = 0; i < sleep_time; i++) {
        if (wakeup_signal) {
            wakeup_signal = 0;
            return;
        }
        sleep(1);
    }
}

void daemonize() {
    pid_t pid;

    printf("Starting daemon...\n");
    fflush(stdout);

    pid = fork();
    if (pid < 0) {
        perror("fork failed");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    if (setsid() < 0) {
        perror("setsid failed");
        exit(EXIT_FAILURE);
    }

    pid = fork();
    if (pid < 0) {
        perror("Second fork failed");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    chdir("/");
    umask(0);

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    openlog("file_search_daemon", LOG_PID, LOG_DAEMON);
    syslog(LOG_INFO, "Daemon started successfully");

    signal(SIGUSR1, handle_signal);
    signal(SIGUSR2, handle_signal);
}
