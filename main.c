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
#include <sys/wait.h>

#define EXIT_NO_ARGS 1
#define MAX_FILES 100
volatile sig_atomic_t wakeup_signal = 0;
bool verbose_mode = false;
pid_t search_pids[MAX_FILES];
int num_files = 0;

void lookup(char *file_name, char *path);
void checkForFile(char *dName, char *file_name);
void handle_signal(int sig);
void sleep_with_signals(int sleep_time);
void daemonize();
void spawn_search_processes(char **file_names);
void supervisor_loop(int sleep_time, char **file_names);

int main(int argc, char **argv) 
{
    int opt;
    int sleep_time = 60;

    while ((opt = getopt(argc, argv, "t:v")) != -1) 
    {
        switch (opt) 
        {
            case 't':
                sleep_time = atoi(optarg);
                if (sleep_time <= 0) 
                {
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

    num_files = argc - optind;
    char **file_names = &argv[optind];

    daemonize();
    printf("Daemon starting");
    syslog(LOG_INFO, "Starting daemon for file search...");

    spawn_search_processes(file_names);
    supervisor_loop(sleep_time, file_names);

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

    while((dp = readdir(directory)) != NULL){
        file_counter++;
        if(strcmp(dp->d_name,".") == 0 || strcmp(dp->d_name,"..") == 0){
            continue;
        }
        char fullPath[PATH_MAX];
        snprintf(fullPath,sizeof(fullPath),"%s/%s",path,dp->d_name);

        struct stat statbuf;
        if(lstat(fullPath,&statbuf) == -1)continue;
        checkForFile(dp->d_name,args,fullPath);

        if (verbose_mode) syslog(LOG_INFO, "[-v flag]: Checking file: %s", dp->d_name); //added for logs

        if(S_ISDIR(statbuf.st_mode) || S_ISLNK(statbuf.st_mode)){
            if(access(fullPath,R_OK | X_OK) == 0){
                file_counter+=lookup(args,fullPath);
            }
        }
    }
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
            syslog(LOG_INFO, "[%s] File found: %s", timestamp, full_path);
        }
        i++;
        temp = args[i];
    }
}

void handle_signal(int sig) 
{
    if (sig == SIGUSR1) 
    {
        syslog(LOG_INFO, "Received SIGUSR1: Waking up");
        if (verbose_mode) 
        {
            syslog(LOG_INFO, "[-v flag]: Daemon received SIGUSR1, waking up...");
        }
        wakeup_signal = 1;
    } 
    
    for (int i = 0; i < num_files; i++) 
    {
        if (search_pids[i] > 0) 
        {
            kill(search_pids[i], sig);
        }
    }

    if (sig == SIGUSR2) 
    {
        syslog(LOG_INFO, "Received SIGUSR2: Stopping daemon");
        if (verbose_mode) 
        {
            syslog(LOG_INFO, "[-v flag]: Daemon received SIGUSR2, shutting down...");
        }
        exit(EXIT_SUCCESS);
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

void daemonize() 
{
    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    if (setsid() < 0) exit(EXIT_FAILURE);

    signal(SIGUSR1, handle_signal);
    signal(SIGUSR2, handle_signal);

    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    chdir("/");
    umask(0);

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    openlog("file_search_daemon", LOG_PID, LOG_DAEMON);
    syslog(LOG_INFO, "Daemon started successfully");
}

void spawn_search_processes(char **file_names)
 {
    for (int i = 0; i < num_files; i++) 
    {
        pid_t pid = fork();
        if (pid == 0) 
        {
            lookup(file_names[i], "/home");
            exit(EXIT_SUCCESS);
        } 
        else if (pid > 0) 
        {
            search_pids[i] = pid;
        } 
        else 
        {
            syslog(LOG_ERR, "Fork failed for %s", file_names[i]);
        }
    }
}

void supervisor_loop(int sleep_time, char **file_names) 
{
    int status;
    while (1) 
    {
        pid_t finished_pid = wait(&status);
        if (finished_pid > 0) 
        {
            syslog(LOG_INFO, "Process %d terminated, restarting...", finished_pid);
            for (int i = 0; i < num_files; i++) 
            {
                if (search_pids[i] == finished_pid) 
                {
                    pid_t new_pid = fork();
                    if (new_pid == 0) 
                    {
                        lookup(file_names[i], "/home");  
                        exit(EXIT_SUCCESS);
                    } 
                    else 
                    {
                        search_pids[i] = new_pid;
                    }
                }
            }
        }
        if (verbose_mode) syslog(LOG_INFO, "[-v flag]: Daemon going to sleep for %d seconds", sleep_time);
        sleep_with_signals(sleep_time);
    }
}