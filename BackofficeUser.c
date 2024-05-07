/*Guilherme Eufrásio Rodrigues - 2021218943
Tiago Monteiro Dias - 2021219480*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/msg.h>
#define _XOPEN_SOURCE 700

int mQueueID;
pthread_t queueThread;

typedef struct stats
{
    int totalRequestsMusic;
    int nRequestsMusic;
    int totalRequestsSocial;
    int nRequestsSocial;
    int totalRequestsVideo;
    int nRequestsVideo;

} stats;
typedef struct mQMessageBackOffice
{
    long mtype;
    stats stats;

} mQMessageBackOffice;

void handleSigInt(int sig)
{
    printf("Received SIGINT\n");
    pthread_cancel(queueThread);
    pthread_join(queueThread, NULL);
    exit(0);
}
void *readFromQueue()
{
    key_t key = ftok("./config.txt", 65);
    if ((mQueueID = msgget(key, 0666)) == -1)
    {
        printf("Error creating message queue\n");
        exit(1);
    }

    while (1)
    {
        mQMessageBackOffice message;
        if (msgrcv(mQueueID, &message, sizeof(message), 1, 0) == -1)
        {
            printf("Error receiving message from queue\n");
            exit(1);
        }
        printf("Service\t Total Data\t Auth Reqs\n\n");
        printf("Video\t\t%d\t\t%d\n", message.stats.totalRequestsVideo, message.stats.nRequestsVideo);
        printf("Music\t\t%d\t\t%d\n", message.stats.totalRequestsMusic, message.stats.nRequestsMusic);
        printf("Social\t\t%d\t\t%d\n", message.stats.totalRequestsSocial, message.stats.nRequestsSocial);
    }
}
int main()
{
    int backOfficeUserID = 1;
    struct sigaction ctrlc;
    ctrlc.sa_handler = handleSigInt;
    sigfillset(&ctrlc.sa_mask);
    ctrlc.sa_flags = 0;
    sigaction(SIGINT, &ctrlc, NULL);
    int fdBackPipe;

    // Open named pipe
    if ((fdBackPipe = open("BACK_PIPE", O_WRONLY)) < 0)
    {
        printf("Error opening BACK_PIPE\n");
        exit(1);
    }
    // Create reading thread for message queue
    pthread_create(&queueThread, NULL, readFromQueue, NULL);

    while (1)
    {
        char command[100];
        // Receber comandos do utilizador
        if (scanf("%s", command) == 1)
        {
            if (strcmp(command, "data_stats") == 0)
            {
                sprintf(command, "%d#%s", backOfficeUserID, "data_stats");
                write(fdBackPipe, command, strlen(command) + 1);
                printf("Data stats:\n\n");
            }
            else if (strcmp(command, "reset") == 0)
            {
                sprintf(command, "%d#%s", backOfficeUserID, "reset");
                write(fdBackPipe, command, strlen(command) + 1);
                printf("Resetting stats\n\n");
            }
            else if (strcmp(command, "exit") == 0)
            {
                break;
            }
            else
            {
                printf("Invalid command\n");
            }
        }
    }
}
