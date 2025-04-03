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
#include <time.h>
#include <limits.h>
#include <linux/limits.h>

#define EXIT_NO_ARGS 1
volatile sig_atomic_t wakeup_signal = 0;
bool verbose_mode = false; //-v flag boolean
bool is_searching = false;
bool triggeredSigusr1 = false;
bool triggeredSigusr2 = false;
int lookup(char **args,char* path);
void checkForFile(char *dName,char **args,char *full_path);
void handle_signal(int sig);
void sleep_with_signals(int sleep_time);
void daemonize();

int main(int argc, char **argv) {
    int opt;
    int sleep_time = 60;

    while ((opt = getopt(argc, argv, "t:v")) != -1) {
        switch (opt) {
            case 't':
                sleep_time = atoi(optarg);
                if (sleep_time <= 0) {
                    fprintf(stderr, "Invalid sleep time. Using default 60s.\n");
                    sleep_time = 60;
                }
                break;
            case 'v':
                verbose_mode = true;
                break;
            default:
                fprintf(stderr, "Usage: %s [-t sleep_time] [-v] FileName ...\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (optind >= argc) 
    {
        fprintf(stderr, "Usage: %s [-t sleep_time] [-v] FileName ...\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char **file_names = &argv[optind];

    //DEBUG
    if (file_names == NULL) {
        printf("No file names provided.\n");
    }

    printf("Searching for files: ");
    for (int i = 0; file_names[i] != NULL; i++) {
        printf("%s ", file_names[i]);
    }
    printf("\n");
    //END DEBUG

    daemonize();

    syslog(LOG_INFO, "Daemon is starting for file search. Hello!");

    while (1) 
    {
        if (verbose_mode)
        {
            syslog(LOG_INFO, "File search begins: /home");
        }

        syslog(LOG_INFO, "File search begins: /home");
        is_searching = true;
        int file_counter = lookup(file_names, "/");
        is_searching = false;
        
        // if(searchComplete)
        // {
        //     syslog(LOG_INFO, "Search complete. Scanned files %d. Sleeping for %d seconds...", file_counter,sleep_time);
        // }

        if(triggeredSigusr1)
        {
            syslog(LOG_INFO, "Recived SIGUSR1 while searching. Reseting daemon.");
            triggeredSigusr1 = false;
        }

        if(triggeredSigusr2)
        {
            syslog(LOG_INFO, "SIGUSR2 - going to sleep for %d seconds. Search interrupted.", sleep_time);
            triggeredSigusr2 = false;
            // sleep_with_signals(sleep_time);
        }

        if (verbose_mode)
        {
            syslog(LOG_INFO, "[-v flag]: Daemon going to sleep...");
        }
        sleep_with_signals(sleep_time);
    }
    return 0;
}


int lookup(char **args,char* path){
    DIR *directory;
    struct dirent *dp;
    int file_counter = 0;

    if((directory = opendir(path)) == NULL){
        printf("Cannot open: %s\n ",path);
        return file_counter;
    }

    // is_searching = true;
    while((dp = readdir(directory)) != NULL)
    {
        if(strcmp(dp->d_name,".") == 0 || strcmp(dp->d_name,"..") == 0)
        {
            continue;
        }
        file_counter++;

        if(triggeredSigusr1)
        {
            closedir(directory);
            // is_searching = false;
            return file_counter;
        }
        else if(triggeredSigusr2)
        {
            closedir(directory); 
            return file_counter;
        }

        char fullPath[PATH_MAX];
        snprintf(fullPath,sizeof(fullPath),"%s/%s",path,dp->d_name);

        struct stat statbuf;
        if(lstat(fullPath,&statbuf) == -1)continue;
        checkForFile(dp->d_name,args,fullPath);

        if (verbose_mode) 
        {
            syslog(LOG_INFO, "Checking file: %s", dp->d_name);
        }

        if(S_ISDIR(statbuf.st_mode) || S_ISLNK(statbuf.st_mode)){
            if(access(fullPath,R_OK | X_OK) == 0){
                file_counter+=lookup(args,fullPath);
            }
        }
    }
    // is_searching = false;
    closedir(directory);
    return file_counter;
}

void checkForFile(char *dName,char **args,char *full_path){
    time_t now;
    struct tm *t;
    char timestamp[20]; //YYYY-MM-DD HH:MM:SS

    time(&now);
    t = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);
    
    int i = 0;
    char* temp = args[i];
    while(temp != NULL){
        if(strcmp(temp,dName) == 0){
            printf("File found %s \n",full_path);
            syslog(LOG_INFO, "[%s] File [%s] found: %s", timestamp, dName, full_path);
        }
        i++;
        temp = args[i];
    }
}

void handle_signal(int sig) 
{
    if (sig == SIGUSR1) 
    {
        if(is_searching)
        {
            triggeredSigusr1 = true;
        }
        else if(wakeup_signal == 0 && is_searching == false)
        {
            if (verbose_mode)
            {
                syslog(LOG_INFO, "Received SIGUSR1, while sleeping. Waking up instantly");
            }
            else
            {
                syslog(LOG_INFO, "Received SIGUSR1, while sleeping. Waking up instantly");
            }
            wakeup_signal = 1;
        }
    }
    else if (sig == SIGUSR2) 
    {
        if(is_searching)
        {
            triggeredSigusr2 = true;
        }
        else
        {
            if(verbose_mode)
            {
                syslog(LOG_INFO, "Received SIGUSR2, while sleeping. Signal ignored");
            }
            else
            {
                syslog(LOG_INFO, "Received SIGUSR2, while sleeping. Signal ignored");
            }
        }
    }
}

void sleep_with_signals(int sleep_time) 
{
    for (int i = 0; i < sleep_time; i++) 
    {
        if (wakeup_signal) 
        {
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
    if (pid < 0) 
    {
        perror("fork failed");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    if (setsid() < 0) 
    {
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
