#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>

#define EXIT_NO_ARGS 1
void lookup(char **args,char* path);
void checkForFile(char *dName,char **args);

int main(int argc, char ** argv){
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
        }
        i++;
        temp = args[i];
    }
}
