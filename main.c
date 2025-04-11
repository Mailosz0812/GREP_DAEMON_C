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

volatile sig_atomic_t wakeup_signal = 0;
bool verbose_mode = false;
bool is_searching = false;
bool triggeredSigusr1 = false;
bool triggeredSigusr2 = false;
int sleep_time = 60;

pid_t *child_pids = NULL;
int num_children = 0;

int lookup(char **args, char* path);
void checkForFile(char *dName, char **args, char *full_path);
void handle_signal(int sig);
void handle_signal_child(int sig);
void sleep_with_signals(int sleep_time);
void daemonize();
void supervisor_loop(char **file_names);
void spawn_children(char **file_names);
pid_t spawn_child(char *file_name, int index);
void forward_signal_to_children(int sig);

int main(int argc, char **argv) {
    int opt;

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
    num_children = argc - optind;
    child_pids = malloc(num_children * sizeof(pid_t));
    
    //DEBUG
    // if (file_names == NULL) {
    //     printf("No file names provided.\n");
    // }

    // printf("Searching for files: ");
    // for (int i = 0; file_names[i] != NULL; i++) {
    //     printf("%s ", file_names[i]);
    // }
    // printf("\n");
    //END DEBUG
    
    if (child_pids == NULL) 
    {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }

    if (verbose_mode)
    {
        printf("Searching for files: ");
        for (int i = 0; file_names[i] != NULL; i++) {
            printf("%s ", file_names[i]);
        }
        printf("\n");
    }

    time_t now;
    struct tm *t;
    char timestamp[20]; //YYYY-MM-DD HH:MM:SS

    time(&now);
    t = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);

    daemonize();
    if(verbose_mode)
    {
        syslog(LOG_INFO, "[%s] Supervisor Daemon is starting for file search. Hello!", timestamp);
    }
    else
    {
        syslog(LOG_INFO, "Supervisor Daemon is starting for file search. Hello!");
    }

    supervisor_loop(file_names);
    free(child_pids);
    return 0;
}

// daemon main loop - waits for children to finish and restarts them if necessary
void supervisor_loop(char **file_names) 
{
    spawn_children(file_names);
    syslog(LOG_INFO, "File search begins: /");

    int status;
    pid_t pid;
    while (1) 
    {
        pid = wait(&status);

        if (pid == -1) 
        {
            syslog(LOG_ERR, "wait() failed or no children: %m");
            continue;
        }

        for (int i = 0; i < num_children; i++) 
        {
            if (child_pids[i] == pid) 
            {
                if (WIFEXITED(status)) 
                {
                    syslog(LOG_INFO, "Child %d exited with status %d. Creating new child.", pid, WEXITSTATUS(status));
                } 
                else if (WIFSIGNALED(status)) 
                {
                    syslog(LOG_INFO, "Child %d terminated by signal %d. Restarting...", pid, WTERMSIG(status));
                }

                pid_t new_pid = spawn_child(file_names[i], i);
                if (new_pid > 0) 
                {
                    child_pids[i] = new_pid;
                }
                break;
            }
        }
    }
}


// creates child processes for each of the given files to search for
void spawn_children(char **file_names) 
{
    for (int i = 0; i < num_children; i++) 
    {
        child_pids[i] = spawn_child(file_names[i], i);
    }
}

// creates one child process to search for a specific file
pid_t spawn_child(char *file_name, int index)
{
    pid_t pid = fork();
    if (pid < 0) 
    {
        syslog(LOG_ERR, "Fork failed for file: %s", file_name);
        return -1;
    } 
    else if (pid == 0) 
    {
        // Proces potomny
        char *single_file[2] = {file_name, NULL};
        signal(SIGUSR1, handle_signal_child);
        signal(SIGUSR2, handle_signal_child);
        syslog(LOG_INFO, "Child process %d started searching for file: %s", getpid(), file_name);
        while (1) 
        {
            syslog(LOG_INFO, "Child is searching. Looking for file: %s", single_file[0]);
            is_searching = true;
            lookup(single_file, "/");
            is_searching = false;

            if (triggeredSigusr1) 
            {
                syslog(LOG_INFO, "Child %d restarted. Searching started.", getpid());
                triggeredSigusr1 = false;
                continue;
            }

            if (triggeredSigusr2) 
            {
                syslog(LOG_INFO, "Search interrupted, child %d is going to sleep for %d seconds.", getpid(), sleep_time);
                triggeredSigusr2 = false;
                sleep_with_signals(sleep_time);
                continue;
            }

            syslog(LOG_INFO, "Child %d is going to sleep for %d seconds. Search completed. Child will die after sleep time.", getpid(), sleep_time);
            sleep_with_signals(sleep_time);
            syslog(LOG_INFO, "Child is dying")
            exit(EXIT_SUCCESS);
        }
        exit(EXIT_SUCCESS);
    } 
    else 
    {
        syslog(LOG_INFO, "Supervisor created child process %d for file: %s", pid, file_name);
        return pid;
    }
}

// passes a signal (SIGUSR1 or SIGUSR2) to all children
void forward_signal_to_children(int sig) 
{
    for (int i = 0; i < num_children; i++) 
    {
        if (child_pids[i] > 0) 
        {
            kill(child_pids[i], sig);
            syslog(LOG_INFO, "Forwarded signal %d to child process %d", sig, child_pids[i]);
        }
    }
}

// recursive function that searches directories for files
int lookup(char **args, char* path) {
    DIR *directory;
    struct dirent *dp;
    int file_counter = 0;

    // attempt to open directory
    if((directory = opendir(path)) == NULL) 
    {
        if (verbose_mode) 
        {
            syslog(LOG_INFO, "Cannot open: %s", path);
        }
        return file_counter;
    }

    while((dp = readdir(directory)) != NULL) 
    {
        if(strcmp(dp->d_name,".") == 0 || strcmp(dp->d_name,"..") == 0) 
        {
            continue;
        }
        file_counter++;

        // signal handling – if a signal comes in, we stop searching
        if(triggeredSigusr1) 
        {
            closedir(directory);
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
        if(lstat(fullPath,&statbuf) == -1) continue;
        checkForFile(dp->d_name,args,fullPath);

        if (verbose_mode) 
        {
            syslog(LOG_INFO, "Checking file: %s", dp->d_name);
        }

        if(S_ISDIR(statbuf.st_mode) || S_ISLNK(statbuf.st_mode)) 
        {
            if(access(fullPath,R_OK | X_OK) == 0) {
                file_counter+=lookup(args,fullPath);
            }
        }
    }
    closedir(directory);
    return file_counter;
}

// checks if the given file/directory has a name that matches the file being searched for
void checkForFile(char *dName, char **args, char *full_path) {
    time_t now;
    struct tm *t;
    char timestamp[20]; //YYYY-MM-DD HH:MM:SS

    time(&now);
    t = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);
    
    int i = 0;
    char* temp = args[i];
    while(temp != NULL) 
    {
        if(strcmp(temp,dName) == 0) 
        {
            syslog(LOG_INFO, "[%s] File [%s] found: %s", timestamp, dName, full_path);
        }
        i++;
        temp = args[i];
    }
}

// handles signals in parent process (passes on to children)
void handle_signal(int sig) 
{
    // passing signals to children
    if (sig == SIGUSR1) 
    {
        forward_signal_to_children(SIGUSR1);
    }
    else if (sig == SIGUSR2) 
    {
        forward_signal_to_children(SIGUSR2);
    }
}

// handles signals in the child process (interrupts, restarts search, or wakes up)
void handle_signal_child(int sig)
{
    if(sig == SIGUSR1)
    {
        syslog(LOG_INFO, "Received SIGUSR1 - child");
        if(is_searching)
        {
            triggeredSigusr1 = true;
            syslog(LOG_INFO, "Received SIGUSR1, while searching. Reseting - child");
        }
        else if(is_searching == false && wakeup_signal == 0)
        {
            syslog(LOG_INFO, "Received SIGUSR1, while sleeping. Waking up instantly - child");
            wakeup_signal = 1;
        }
    }
    else if(sig == SIGUSR2)
    {
        syslog(LOG_INFO, "Received SIGUSR2 - child");
        if(is_searching) 
        {
            triggeredSigusr2 = true;
            syslog(LOG_INFO, "Received SIGUSR2, while searching. Going to sleep - child");
        }
        else
        {
            syslog(LOG_INFO, "Received SIGUSR2, while sleeping. Signal ignored - child");
        }
    }
}


//putting processes to sleep with the possibility of interruption by a signal
void sleep_with_signals(int sleep_time) 
{
    for (int i = 0; i < sleep_time; i++) 
    {
        if (wakeup_signal) 
        {
            wakeup_signal = 0;
            return;
        }
        sleep(1); //signal check every second and then going to sleep - allows for interrupting sleep by signal
    }
}

// converts a process into a daemon - detaches from terminal, sets environment (supervisor process).
void daemonize() {
    pid_t pid;

    if (verbose_mode) {
        printf("Starting daemon...\n");
        fflush(stdout);
    }

    // separation from parent process
    pid = fork();
    if (pid < 0) 
    {
        perror("fork failed");
        exit(EXIT_FAILURE); 
    }
    if (pid > 0) 
    {
        exit(EXIT_SUCCESS); //Parent terminates the activity
    }

    if (setsid() < 0) {
        perror("setsid failed");
        exit(EXIT_FAILURE);
    }

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

    // set default directory and permission mask
    chdir("/");
    umask(0);

    // closing the standard descriptors
    close(STDIN_FILENO); //standard input
    close(STDOUT_FILENO); //standard output
    close(STDERR_FILENO); //standard error output

    // start logging to syslog
    openlog("file_search_daemon", LOG_PID, LOG_DAEMON);
    syslog(LOG_INFO, "Daemon started successfully");

    // signal handling for the daemon
    signal(SIGUSR1, handle_signal);
    signal(SIGUSR2, handle_signal);
}