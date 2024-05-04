// Funcionalidades deste ficheiro, system manager:
// Lê e valida as informações no ficheiro de configurações
// ● Cria os processos Authorization Requests Manager e Monitor Engine
// ● Escreve no log file;
//  Captura o sinal SIGINT para terminar o programa, libertando antes todos os recursos.
/*O ficheiro de configurações deverá seguir a seguinte estrutura:
MOBILE USERS (>=1) - número de Mobile Users que podem ser lançados
QUEUE_POS(>=0) -
AUTH_SERVERS_MAX  (>=1)
AUTH_PROC_TIME -(>=0)
MAX_VIDEO_WAIT -(>=1)
MAX_OTHERS_WAIT(>=1)*/

// #define DEBUG

#define _XOPEN_SOURCE 700
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
#include "structs.h"

FILE *configFile, *logFile;
int config[5];
int mobile_users, queue_pos, auth_servers_max, auth_proc_time, max_video_wait, max_others_wait;
sem_t *logSem, *shmSem, *authEngineAvailableSem, *shmChangesSem, *extraAuthEngineSem;
sem_t queuesEmpty;
pid_t originalPid, authRequestManagerPid, monitorEnginePid;
// mutual exclusion is now a pthread mutex
pthread_mutex_t mutualExclusion;
pthread_t senderThread, receiverThread, statsThread;
int shmid, mQueueId;
sharedMemory *shm;
int fdUserPipe, fdBackPipe;
userMessage *video_streaming_queue, *others_services_queue;
int (*authEnginePipes)[2];
int extraSemValue, timeToDie = 0;
void writeToLog(char *message)
{
    time_t now = time(NULL);
    struct tm *date_time = localtime(&now);
    sem_wait(logSem);
    printf("%02d:%02d:%02d %s\n", date_time->tm_hour, date_time->tm_min, date_time->tm_sec, message);
    fprintf(logFile, "%4d/%02d/%02d %02d:%02d:%02d %s\n", date_time->tm_year + 1900, date_time->tm_mon + 1, date_time->tm_mday, date_time->tm_hour, date_time->tm_min, date_time->tm_sec, message);
    fflush(logFile);
    sem_post(logSem);
}
void errorHandler(char *errorMessage)
{
    writeToLog(errorMessage);

    if (getpid() != originalPid && getpid() != authRequestManagerPid)
    {
        exit(0);
    }
    else if (getpid() == authRequestManagerPid)
    {
        sem_wait(shmSem);
        for (int i = 0; i < auth_servers_max; i++)
        {
            // error can occur before auth engine is created therefore we need to check if pid is different from NULL
            if (shm->authEngines[i].pid != 0)
            {
#ifdef DEBUG
                printf("Waiting for auth engine %d\n", i);
#endif
                waitpid(shm->authEngines[i].pid, NULL, 0);
            }
        }
        sem_post(shmSem);

        exit(0);
    }
    else
    {
        waitpid(authRequestManagerPid, NULL, 0);
    }
    // Authorization Engine processes
    // if pid is different from the original pid, exit(0)
    writeToLog("SIGNAL SIGINT RECEIVED");
    writeToLog("5G_AUTH_PLATFORM SIMULATOR CLOSING");

    if (configFile != NULL)
    {
        fclose(configFile);
    }
    // Threads
    // pthread_cancel(senderThread);
    // pthread_cancel(receiverThread);
    // pthread_join(senderThread, NULL);
    // pthread_join(receiverThread, NULL);

    // Shared Memory
    shmdt(shm);
    shmctl(shmid, IPC_RMID, NULL);

    // Authorization Engine pipes
    for (int i = 0; i < auth_servers_max; i++)
    {

        close(authEnginePipes[i][0]);
        close(authEnginePipes[i][1]);
    }

    free(authEnginePipes);

    // Semaphores
    sem_close(logSem);
    sem_close(shmSem);
    sem_close(authEngineAvailableSem);
    sem_close(shmChangesSem);
    sem_close(extraAuthEngineSem);
    sem_unlink("LOG_SEM");
    sem_unlink("SHM_SEM");
    sem_unlink("AUTH_ENGINE_SEM");
    sem_unlink("SHM_CHANGES_SEM");
    sem_unlink("EXTRA_AUTH_ENGINE_SEM");
    // Named Pipes
    close(fdUserPipe);
    unlink("USER_PIPE");
    close(fdBackPipe);
    unlink("BACK_PIPE");
    // Queues
    free(video_streaming_queue);
    free(others_services_queue);

    // Message Queue
    msgctl(mQueueId, IPC_RMID, NULL);

    // Mutex
    pthread_mutex_destroy(&mutualExclusion);

    // Log File
    fclose(logFile);

    exit(0);
}
void handleSigterm(int sig)
{
    if (getpid() == monitorEnginePid)
    {
        // cancel stats thread
        pthread_cancel(statsThread);
        pthread_join(statsThread, NULL);
        writeToLog("MONITOR ENGINE CLOSING");
        exit(0);
    }
    else if (getpid() == authRequestManagerPid)
    {
        // if it is authRequestManager
        // wait for all auth engines to finish and receiver and sender threads
        sem_wait(shmSem);
        // there may be an extra auth engine running
        if (shm->extraVerifier == 1 || shm->extraVerifier == 2)
        {
            for (int i = 0; i < auth_servers_max + 1; i++)
            {
                if (shm->authEngines[i].pid != 0)
                {
                    kill(shm->authEngines[i].pid, SIGTERM);
                    waitpid(shm->authEngines[i].pid, NULL, 0);
                }
            }
        }
        else
        {
            for (int i = 0; i < auth_servers_max; i++)
            {
                if (shm->authEngines[i].pid != 0)
                {
                    kill(shm->authEngines[i].pid, SIGTERM);
                    waitpid(shm->authEngines[i].pid, NULL, 0);
                }
            }
        }
        sem_post(shmSem);
        printf("posdjfsikfjsr\n");
        // printf("Now, canceling threads\n");
        pthread_cancel(receiverThread);
        pthread_join(receiverThread, NULL);
        pthread_cancel(senderThread);
        pthread_join(senderThread, NULL);
        printf("Somebody\n");

        // print all the tasks that were left on the queues
        // for (int i = 0; i < queue_pos; i++)
        // {
        //     if (video_streaming_queue[i].isMessageHere == 1)
        //     {
        //         char messageToSend[256];
        //         sprintf(messageToSend, "IN VIDEO_QUEUE[%d]: %s %d ", i, video_streaming_queue[i].category, video_streaming_queue[i].dataToReserve);
        //         writeToLog(messageToSend);
        //     }
        //     if (others_services_queue[i].isMessageHere == 1)
        //     {
        //         char messageToSend[256];
        //         sprintf(messageToSend, "IN OTHERS QUEUE[%d]: %s %d ", i, video_streaming_queue[i].category, video_streaming_queue[i].dataToReserve);
        //         writeToLog(messageToSend);
        //     }
        // }
        writeToLog("AUTHORIZATION REQUESTS MANAGER CLOSING");
        printf("Ekmfvldfmvmfdvfd\n");
        exit(0);
    }
    else
    {
        // if it is an auth engine
        timeToDie = 1;
    }
}

void handleSigInt(int sig)
{
    writeToLog("Signal SIGINT received");
    kill(authRequestManagerPid, SIGTERM);
    waitpid(authRequestManagerPid, NULL, 0);
    printf("TESTE\n");
    kill(monitorEnginePid, SIGTERM);
    waitpid(monitorEnginePid, NULL, 0);
    printf("TESTE1\n");

    if (configFile != NULL)
    {
        fclose(configFile);
    }
    // Threads

    // Shared Memory
    shmdt(shm);
    shmctl(shmid, IPC_RMID, NULL);

    // Authorization Engine pipes
    for (int i = 0; i < auth_servers_max; i++)
    {

        close(authEnginePipes[i][0]);
        close(authEnginePipes[i][1]);
    }

    free(authEnginePipes);

    writeToLog("5G_AUTH_PLATFORM SIMULATOR CLOSING");
    // Semaphores
    sem_close(logSem);
    sem_close(shmSem);
    sem_close(authEngineAvailableSem);
    sem_close(shmChangesSem);
    sem_close(extraAuthEngineSem);
    sem_unlink("LOG_SEM");
    sem_unlink("SHM_SEM");
    sem_unlink("AUTH_ENGINE_SEM");
    sem_unlink("SHM_CHANGES_SEM");
    sem_unlink("EXTRA_AUTH_ENGINE_SEM");
    sem_destroy(&queuesEmpty);
    // Named Pipes
    close(fdUserPipe);
    unlink("USER_PIPE");
    close(fdBackPipe);
    unlink("BACK_PIPE");
    // Queues
    free(video_streaming_queue);
    free(others_services_queue);

    printf("TESTE3\n");

    // free(others_services_queue);
    // Mutex
    pthread_mutex_destroy(&mutualExclusion);

    // Message Queue
    msgctl(mQueueId, IPC_RMID, NULL);

    // Log File
    if (logFile != NULL)
        fclose(logFile);

    exit(0);
}
void setupLogFile()
{
    logFile = fopen("log.txt", "w");
    if (logFile == NULL)
    {
        errorHandler("ERROR: Could not open log file");
    }
    logSem = sem_open("LOG_SEM", O_CREAT | O_EXCL, 0700, 1);
    if (logSem == SEM_FAILED)
    {
        errorHandler("ERROR: Not possible to create log semaphore\n");
    }
}
void readConfigFile(char *fileName)
{

    configFile = fopen(fileName, "r");
    if (configFile == NULL)
    {
        errorHandler("ERROR: Could not open config file\n");
    }
    char line[30];
    int i;
    for (i = 0; i < 6; i++)
    {
        fgets(line, 30, configFile);
        if ((sscanf(line, "%d", &config[i]) != 1))
        {
            errorHandler("ERROR: Wrong config file format");
        }
        if (config[i] < 0)
        {
            errorHandler("ERROR: Config file values must be positive");
        }
        else if ((i == 0 || i == 1 || i == 3 || i == 4) && config[i] < 1)
        {
            errorHandler("ERROR: MOBILE USERS, AUTH_SERVERS_MAX, MAX_VIDEO_WAIT and MAX_OTHERS_WAIT >=1");
        }
    } // carateres estranhos
    fclose(configFile);
    configFile = NULL;
}
// pipeCreator: cria os pipes
void pipeCreator()
{
    // Criação dos named pipes
    if (mkfifo("USER_PIPE", 0666) == -1)
    {
        errorHandler("ERROR: Not possible to create USER_PIPE\n");
    }
    if ((fdUserPipe = open("USER_PIPE", O_RDWR)) < 0)
    {
        errorHandler("ERROR: Not possible to open USER_PIPE\n");
    }
    if (mkfifo("BACK_PIPE", 0666) == -1)
    {
        errorHandler("ERROR: Not possible to create BACK_PIPE\n");
    }
    if ((fdBackPipe = open("BACK_PIPE", O_RDWR)) < 0)
    {
        errorHandler("ERROR: Not possible to open BACK_PIPE\n");
    }
}
// syncCreator: cria os semáforos
void syncCreator()
{
    // Semaforo para a memoria partilhada
    shmSem = sem_open("SHM_SEM", O_CREAT | O_EXCL, 0700, 1);
    if (shmSem == SEM_FAILED)
    {
        errorHandler("ERROR: Not possible to create shared memory semaphore\n");
    }
    // Semaforo para a fila de video
    sem_init(&queuesEmpty, 0, 0);
    // Zona de exclusão mútuaj
    if (pthread_mutex_init(&mutualExclusion, NULL) != 0)
    {
        errorHandler("ERROR: Not possible to create mutual exclusion mutex\n");
    }
    shmChangesSem = sem_open("SHM_CHANGES_SEM", O_CREAT | O_EXCL, 0700, 0);
    if (shmChangesSem == SEM_FAILED)
    {
        errorHandler("ERROR: Not possible to create shared memory changes semaphore\n");
    }
    extraAuthEngineSem = sem_open("EXTRA_AUTH_ENGINE_SEM", O_CREAT | O_EXCL, 0700, 0);
    if (extraAuthEngineSem == SEM_FAILED)
    {
        errorHandler("ERROR: Not possible to create extra auth engine verifier semaphore\n");
    }
}
// queueCreator: cria as filas
void queueCreator()
{
    video_streaming_queue = (userMessage *)malloc(sizeof(userMessage) * queue_pos);
    if (video_streaming_queue == NULL)
    {
        errorHandler("ERROR: Not possible to create Video_Streaming_Queue\n");
    }
    // initialize every element of queue .isMessageHere to 0
    for (int i = 0; i < queue_pos; i++)
    {
        video_streaming_queue[i].isMessageHere = 0;
    }

    others_services_queue = (userMessage *)malloc(sizeof(userMessage) * queue_pos);
    if (others_services_queue == NULL)
    {
        errorHandler("ERROR: Not possible to create Others_Services_Queue\n");
    }
    // initialize every element of queue .isMessageHere to 0
    for (int i = 0; i < queue_pos; i++)
    {
        others_services_queue[i].isMessageHere = 0;
    }
}
// authEngineFunction: função que executa o código de cada Authorization Engine
void authEngineFunction(int index)
{
    char messageToSend[256];
    sprintf(messageToSend, "AUTHORIZATION ENGINE %d CREATED", index);
    writeToLog(messageToSend);

    sem_wait(shmSem);
    // Mark this as available
    shm->authEngines[index].available = 1;
    sprintf(messageToSend, "AUTHORIZATION ENGINE %d AVAILABLE", index);
    writeToLog(messageToSend);
    sem_post(shmSem);

    // post semaphore
    sem_post(authEngineAvailableSem);

    // wait for message from sender
    char messageToRead[256];
    int readValue;
    while (!timeToDie)
    {
        if ((readValue = read(authEnginePipes[index][0], &messageToRead, sizeof(messageToRead))) <= 0)
        {
            if (timeToDie != 1)
                errorHandler("ERROR: Not possible to read from Authorization Engine pipe\n");
            else
                break;
        }
        messageToRead[readValue] = '\0';
        char messageToSend[256];
        sprintf(messageToSend, "[AE] %d received message", index);
        writeToLog(messageToSend);
        sleep(auth_proc_time);
#ifdef DEBUG
        printf("[AE] %d: %s\n", index, messageToRead);
#endif
        int count = 0;
        char *ptr = messageToRead;
        char *tokens[3];
        int token_count = 0;
        char *token_start = messageToRead;
        while (*ptr != '\0' && token_count < 3)
        {
            if (*ptr == '#')
            {
                count++;
                *ptr = '\0';
                tokens[token_count++] = token_start;
                token_start = ptr + 1;
            }
            ptr++;
        }
        tokens[token_count++] = token_start;
        // message can either be a registration message or a data request message
        if (count == 2)
        {
            // data request message
            // message structure: idToRequest#category#dataToReserve
            // find user in shm and update data
            sem_wait(shmSem);
            if (strcmp(tokens[1], "registration") == 0) // if tokens[1] is "registration" we register the user
            {

                for (int i = 0; i < mobile_users; i++)
                {
                    if (shm->users[i].userID == 0) // if we find an empty user, we register the new user
                    {
                        shm->users[i].userID = atoi(tokens[0]);
                        shm->users[i].currentPlafond = atoi(tokens[2]);
                        shm->users[i].musicUsed = 0;
                        shm->users[i].socialUsed = 0;
                        shm->users[i].videoUsed = 0;
                        shm->users[i].originalPlafond = atoi(tokens[2]);
                        shm->users[i].wasNotified = 0;
                        sprintf(messageToSend, "[AE]: USER %d REGISTERED", atoi(tokens[0]));
                        writeToLog(messageToSend);
                        break;
                    }
                    else if (i == mobile_users - 1) // if we reach the end of the array and no empty user is found, we discard the message
                    {
                        printf("User %d could not be registered\n", atoi(tokens[0]));
                    }
                }
            } // or it can be a backofficeUserCommand
            else if (strcmp(tokens[1], "data_stats") == 0 || strcmp(tokens[1], "reset") == 0)
            {

                if (strcmp(tokens[1], "data_stats") == 0)
                {
                    // write to message queue
                    mQMessageBackOffice message;
                    message.mtype = 1;
                    message.stats.totalRequestsMusic = shm->stats.totalRequestsMusic;
                    message.stats.totalRequestsSocial = shm->stats.totalRequestsSocial;
                    message.stats.totalRequestsVideo = shm->stats.totalRequestsVideo;
                    message.stats.nRequestsMusic = shm->stats.nRequestsMusic;
                    message.stats.nRequestsSocial = shm->stats.nRequestsSocial;
                    message.stats.nRequestsVideo = shm->stats.nRequestsVideo;
                    msgsnd(mQueueId, &message, sizeof(message), 0);
                }
                else
                {
                    // reset all the stats
                    shm->stats.totalRequestsMusic = 0;
                    shm->stats.totalRequestsSocial = 0;
                    shm->stats.totalRequestsVideo = 0;
                    shm->stats.nRequestsMusic = 0;
                    shm->stats.nRequestsSocial = 0;
                    shm->stats.nRequestsVideo = 0;
                }
            }

            else // it is a data request message
            {
                for (int i = 0; i < mobile_users; i++)
                {
                    if (shm->users[i].userID == atoi(tokens[0]))
                    {
                        // if the user's plafond isnt enought to fulfill the request, we discard the message and write to the log
                        if (shm->users[i].currentPlafond < atoi(tokens[2]))
                        {
                            sprintf(messageToSend, "[AE] %d:REQUEST (ID = %d) DISCARDED NOT ENOUGH PLAFOND", index, atoi(tokens[0]));
                            writeToLog(messageToSend);
                            break;
                        }
                        else
                        {
                            shm->users[i].currentPlafond -= atoi(tokens[2]);
                            if (strcmp(tokens[1], "MUSIC") == 0)
                            {
                                shm->users[i].musicUsed += atoi(tokens[2]);
                                shm->stats.totalRequestsMusic += atoi(tokens[2]);
                                shm->stats.nRequestsMusic++;
                                sprintf(messageToSend, "[AE] %d:MUSIC AUTH REQ (ID = %d) COMPLETED", index, atoi(tokens[0]));
                                writeToLog(messageToSend);
                            }
                            else if (strcmp(tokens[1], "SOCIAL") == 0)
                            {
                                shm->users[i].socialUsed += atoi(tokens[2]);
                                shm->stats.totalRequestsSocial += atoi(tokens[2]);
                                shm->stats.nRequestsSocial++;
                                sprintf(messageToSend, "[AE] %d:SOCIAL AUTH REQ (ID = %d) COMPLETED", index, atoi(tokens[0]));
                                writeToLog(messageToSend);
                            }
                            else if (strcmp(tokens[1], "VIDEO") == 0)
                            {
                                shm->users[i].videoUsed += atoi(tokens[2]);
                                shm->stats.totalRequestsVideo += atoi(tokens[2]);
                                shm->stats.nRequestsVideo++;
                                sprintf(messageToSend, "[AE] %d:VIDEO AUTH REQ (ID = %d) COMPLETED", index, atoi(tokens[0]));
                                writeToLog(messageToSend);
                            }
                            else
                            {
                                errorHandler("ERROR: Received invalid category from sender\n");
                            }
                            break;
                        }
                    }
                }
            }
            // mark this auth engine as available
            shm->authEngines[index].available = 1;
            sprintf(messageToSend, "[AE] %d AVAILABLE", index);
            writeToLog(messageToSend);
            sem_post(shmChangesSem);
            sem_post(shmSem);
            sem_post(authEngineAvailableSem);
        }
        else
        {
            errorHandler("ERROR: Received invalid message format from sender\n");
        }
    }
    sprintf(messageToSend, "AUTHORIZATION ENGINE %d CLOSING", index);
    writeToLog(messageToSend);
    exit(0);
}

void authEngineCreator()
{
    // Criação do semáforo para os Authorization Engines
    authEngineAvailableSem = sem_open("AUTH_ENGINE_SEM", O_CREAT | O_EXCL, 0700, 0);
    if (authEngineAvailableSem == SEM_FAILED)
    {
        errorHandler("ERROR: Not possible to create Authorization Engine available semaphore\n");
    }
    // Criação dos unnamed pipes para os Authorization Engines
    for (int i = 0; i < auth_servers_max; i++)
    {
        if (pipe(authEnginePipes[i]) == -1)
        {
            errorHandler("ERROR: Not possible to create Authorization Engine pipes\n");
        }
    }
    if (pipe(authEnginePipes[auth_servers_max]) == -1)
    {
        errorHandler("ERROR: Not possible to create extra Authorization Engine pipe\n");
    }
    // Criação dos Authorization Engines
    for (int i = 0; i < auth_servers_max; i++)
    {
        pid_t pid = fork();
        if (pid == -1)
        {
            errorHandler("ERROR: Not possible to create Authorization Engine\n");
        }
        if (pid == 0)
        {
            // setup sigTerm handler
            struct sigaction sigterm;
            sigterm.sa_handler = handleSigterm;
            sigfillset(&sigterm.sa_mask);
            sigterm.sa_flags = 0;
            sigaction(SIGTERM, &sigterm, NULL);
            // child process
            sem_wait(shmSem);
            shm->authEngines[i].pid = getpid();
            sem_post(shmSem);
            authEngineFunction(i);
        }
    }
    // receiver tem variavel global que lhe indica se uma das queues está cheia ou não.(a variavel vai chamar-se extraVerifier)
    // Essa variavel vai estar a 0 se as queues não estiverem cheias, a 1 se a video queue estiver cheia
    // e a 2 se a other services queue estiver cheia. Se a variavel estiver a 0 ele vai verificar se as queues estão cheias
    // se estiverem cheias ele vai notificar este processo para criar um authorization requests manager extra e muda a variavel para 1 ou 2 dependendo da queue que estiver cheia
    // se a variavel estiver a 1 ou a 2 ele vai verificar se a queue respetiva já está a meia capacidade e se estiver vai notificar este processo para remover o authorization requests manager extra

    while (1)
    {
        sem_wait(extraAuthEngineSem);
        // create extra auth engine in the last position
        sem_wait(shmSem);
        if (shm->extraVerifier == 1 || shm->extraVerifier == 2)
        {
            // create extra auth engine
            pid_t pid = fork();
            if (pid == -1)
            {
                errorHandler("ERROR: Not possible to create extra Authorization Engine\n");
            }
            if (pid == 0)
            {
                // child process
                // add pid to shared memory

                shm->authEngines[auth_servers_max].pid = getpid();

                authEngineFunction(auth_servers_max);
            }
        }
        else
        {
            writeToLog("REMOVING EXTRA AUTHORIZATION ENGINE");
            // remove extra auth engine
            if (shm->authEngines[auth_servers_max].pid != 0)
            {
                kill(shm->authEngines[auth_servers_max].pid, SIGKILL);
                shm->authEngines[auth_servers_max].pid = 0;
            }
        }
        sem_post(shmSem);
    }
}
void *senderFunction()
{
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    writeToLog("SENDER THREAD CREATED");

    while (1)
    {
        // WAIT FOR AUTHORIZATION ENGINE TO BE AVAILABLE
        sem_wait(authEngineAvailableSem);
        // get index of the first available auth engine
        sem_wait(shmSem);
        int index = -1;
        if (shm->extraVerifier == 1 || shm->extraVerifier == 2)
        {
            for (int i = 0; i < auth_servers_max + 1; i++)
            {
#ifdef DEBUG
                printf("authEngineAvailable[%d] = %d\n", i, shm->authEngines[i].available);
#endif

                if (shm->authEngines[i].available == 1)
                {

                    shm->authEngines[i].available = 0;
                    index = i;
                    char messageToSend[256];
                    sprintf(messageToSend, "[AE] %d BUSY", i);
                    writeToLog(messageToSend);
                    break;
                }
            }
        }
        else
        {
            for (int i = 0; i < auth_servers_max; i++)
            {
#ifdef DEBUG
                printf("authEngineAvailable[%d] = %d\n", i, shm->authEngines[i].available);
#endif

                if (shm->authEngines[i].available == 1)
                {

                    shm->authEngines[i].available = 0;
                    index = i;
                    char messageToSend[256];
                    sprintf(messageToSend, "[S]: FOUND AVAILABLE [AE]: %d ", i);
                    writeToLog(messageToSend);
                    break;
                }
            }
        }
        sem_post(shmSem);
        // wait for message from receiver

        sem_getvalue(&queuesEmpty, &extraSemValue);
#ifdef DEBUG
        printf("value of queueEmpty semaphore: %d\n", extraSemValue);
#endif
        sem_wait(&queuesEmpty);
// lock for mutual exclusion mutex
#ifdef DEBUG
        if (index == -1)
        {
            printf("\nindex is -1\n");
        }
#endif
        pthread_mutex_lock(&mutualExclusion);
        // go through the queue to find message to send
        int foundMessage = 0;
        for (int i = 0; i < queue_pos; i++)
        {
            if (video_streaming_queue[i].isMessageHere == 1)
            {
                foundMessage = 1;
                // check if message has not expired (if time video wait hasnt passed)
                if (video_streaming_queue[i].timeOfRequest + max_video_wait < time(NULL))
                {
                    char messageToSend[256];
                    sprintf(messageToSend, "[S]: VIDEO ID %d EXPIRED", video_streaming_queue[i].userID);
                    writeToLog(messageToSend);
                    video_streaming_queue[i].isMessageHere = 0;
                    sem_post(authEngineAvailableSem);
                    sem_wait(shmSem);
                    shm->authEngines[index].available = 1;
                    sem_post(shmSem);
                    break;
                }
                else
                {
                    // send message to Authorization Engine
                    char messageToSend[256];
                    sprintf(messageToSend, "%d#%s#%d", video_streaming_queue[i].userID, video_streaming_queue[i].category, video_streaming_queue[i].dataToReserve);
                    if (write(authEnginePipes[index][1], messageToSend, sizeof(messageToSend)) <= 0)
                    {
                        errorHandler("ERROR: Not possible to write to Authorization Engine pipe\n");
                    }
                    sprintf(messageToSend, "[S]: SENT TO [AE] %d: %s", index, video_streaming_queue[i].category);
                    writeToLog(messageToSend);
                    // remove message from queue
                    video_streaming_queue[i].isMessageHere = 0;
                    break;
                }
            }
        }
        if (!foundMessage)
        { // look for message in other services queue
            for (int i = 0; i < queue_pos; i++)
            {
                // if message is here
                if (others_services_queue[i].isMessageHere == 1)
                {

                    // message can either be registration or data request OR BACKOFFICE COMMAND
                    if (others_services_queue[i].isRegistration == 1)
                    {
                        // send message to Authorization Engine
                        char messageToSend[256];
                        sprintf(messageToSend, "%d#%s#%d", others_services_queue[i].userID, "registration", others_services_queue[i].dataToReserve);
                        if (write(authEnginePipes[index][1], messageToSend, sizeof(messageToSend)) <= 0)
                        {
                            errorHandler("ERROR: Not possible to write to Authorization Engine pipe\n");
                        }
                        sprintf(messageToSend, "[S]: SENT TO [AE] %d: %s", index, others_services_queue[i].category);
                        writeToLog(messageToSend);
                        // remove message from queue
                        others_services_queue[i].isMessageHere = 0;
                        break;
                    }
                    else if (strcmp(others_services_queue[i].category, "data_stats") == 0 || strcmp(others_services_queue[i].category, "reset") == 0)
                    {
                        if (others_services_queue[i].timeOfRequest + max_others_wait < time(NULL))
                        {
                            char messageToSend[256];
                            sprintf(messageToSend, "[S]: BACKOFFICE ID %d EXPIRED", others_services_queue[i].userID);
                            writeToLog(messageToSend);
                            video_streaming_queue[i].isMessageHere = 0;
                            sem_post(authEngineAvailableSem);
                            sem_wait(shmSem);
                            shm->authEngines[index].available = 1;
                            sem_post(shmSem);
                            break;
                        }
                        else
                        {
                            // send message to Authorization Engine
                            char messageToSend[256];
                            sprintf(messageToSend, "%d#%s#%d", others_services_queue[i].userID, others_services_queue[i].category, others_services_queue[i].dataToReserve);
                            if (write(authEnginePipes[index][1], messageToSend, sizeof(messageToSend)) <= 0)
                            {
                                errorHandler("ERROR: Not possible to write to Authorization Engine pipe\n");
                            }
                            // say to log
                            sprintf(messageToSend, "[S]: SENT TO [AE] %d: %s", index, others_services_queue[i].category);
                            writeToLog(messageToSend);
                            // remove message from queue
                            others_services_queue[i].isMessageHere = 0;
                            break;
                        }
                    }
                    else
                    {
                        if (others_services_queue[i].timeOfRequest + max_others_wait < time(NULL))
                        {
                            char messageToSend[256];
                            sprintf(messageToSend, "[S]: OTHER SERVICE %d EXPIRED", others_services_queue[i].userID);
                            writeToLog(messageToSend);
                            others_services_queue[i].isMessageHere = 0;
                            sem_post(authEngineAvailableSem);
                            sem_wait(shmSem);
                            shm->authEngines[index].available = 1;
                            sem_post(shmSem);
                            break;
                        }
                        else
                        {
                            // send message to Authorization Engine
                            char messageToSend[256];
                            sprintf(messageToSend, "%d#%s#%d", others_services_queue[i].userID, others_services_queue[i].category, others_services_queue[i].dataToReserve);
                            if (write(authEnginePipes[index][1], messageToSend, sizeof(messageToSend)) <= 0)
                            {
                                errorHandler("ERROR: Not possible to write to Authorization Engine pipe\n");
                            }
                            sprintf(messageToSend, "[S]: SENT TO [AE] %d: %s", index, others_services_queue[i].category);
                            writeToLog(messageToSend);
                            // remove message from queue
                            others_services_queue[i].isMessageHere = 0;
                            break;
                        }
                    }
                }
            }
        }

        sem_wait(shmSem);
        if (shm->extraVerifier == 0)
        {
            // check if video queue is full
            int count = 0;
            for (int i = 0; i < queue_pos; i++)
            {
                if (video_streaming_queue[i].isMessageHere == 1)
                {
                    count += 1;
                }
            }
            if (count >= queue_pos)
            {
                if (sem_getvalue(extraAuthEngineSem, &extraSemValue) == 0 && extraSemValue == 0)
                {
                    sem_post(extraAuthEngineSem);
                    shm->extraVerifier = 1;
                }
            }
#ifdef DEBUG
            printf("Video queue count = %d\n", count);
#endif
            // check if others queue is full
            count = 0;
            for (int i = 0; i < queue_pos; i++)
            {
                if (others_services_queue[i].isMessageHere == 1)
                {
                    count += 1;
                }
            }
            if (count >= queue_pos)
            {
                if (sem_getvalue(extraAuthEngineSem, &extraSemValue) == 0 && extraSemValue == 0)
                {
                    sem_post(extraAuthEngineSem);
                    shm->extraVerifier = 2;
                }
            }
#ifdef DEBUG
            printf("Others queue count = %d\n", count);
#endif
        }
        else if (shm->extraVerifier == 1)
        { // means video queue is full and an extra authEngine was created so we check to see if it now half-full
            int count = 0;
            for (int i = 0; i < queue_pos; i++)
            {
                if (video_streaming_queue[i].isMessageHere == 1)
                {
                    count += 1;
                }
            }
            if (count <= queue_pos / 2)
            {
                if (sem_getvalue(extraAuthEngineSem, &extraSemValue) == 0 && extraSemValue == 0)
                {
                    sem_post(extraAuthEngineSem);
                    shm->extraVerifier = 0;
                }
            }
        }
        else if (shm->extraVerifier == 2)
        { // means others queue is full and an extra authEngine was created so we check to see if it now half-full
            int count = 0;
            for (int i = 0; i < queue_pos; i++)
            {
                if (others_services_queue[i].isMessageHere == 1)
                {
                    count += 1;
                }
            }
            if (count <= queue_pos / 2)
            {
                if (sem_getvalue(extraAuthEngineSem, &extraSemValue) == 0 && extraSemValue == 0)
                {
                    sem_post(extraAuthEngineSem);
                    shm->extraVerifier = 0;
                }
            }
        }
        sem_post(shmSem);

        // unlock for mutual exclusion mutex
        pthread_mutex_unlock(&mutualExclusion);
    }

    pthread_exit(NULL);
}
int userExists(int IDtoCheck)
{
    sem_wait(shmSem);
    for (int i = 0; i < mobile_users; i++)
    {
        if (shm->users[i].userID == IDtoCheck)
        {
            sem_post(shmSem);
            return 1;
        }
    }
    sem_post(shmSem);
    return 0;
}
void *receiverFunction()
{
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    writeToLog("RECEIVER THREAD CREATED");
    int readValue;
    char messageToRead[256];
    fd_set readSet;
    int maxfd = (fdUserPipe > fdBackPipe) ? fdUserPipe : fdBackPipe;
    while (1)
    {
        FD_ZERO(&readSet);
        FD_SET(fdUserPipe, &readSet);
        FD_SET(fdBackPipe, &readSet);
        if (select(maxfd + 1, &readSet, NULL, NULL, NULL) == -1)
        {
            errorHandler("ERROR: Not possible to select pipe\n");
        }
        if (FD_ISSET(fdUserPipe, &readSet))
        {
            if ((readValue = read(fdUserPipe, &messageToRead, sizeof(messageToRead))) <= 0)
            {
                errorHandler("ERROR: Not possible to read from USER_PIPE\n");
            }
        }

        if (FD_ISSET(fdBackPipe, &readSet))
        {
            if ((readValue = read(fdBackPipe, &messageToRead, sizeof(messageToRead))) <= 0)
            {
                errorHandler("ERROR: Not possible to read from BACK_PIPE\n");
            }
        }

        messageToRead[readValue] = '\0';

        int count = 0;
        char *ptr = messageToRead;
        char *tokens[3];
        int token_count = 0;
        char *token_start = messageToRead;
        while (*ptr != '\0' && token_count < 3)
        {
            if (*ptr == '#')
            {
                count++;
                *ptr = '\0';
                tokens[token_count++] = token_start;
                token_start = ptr + 1;
            }
            ptr++;
        }
        tokens[token_count++] = token_start;
        if (strcmp(tokens[0], "1") == 0)
        { // comes from backoffice
            if ((strcmp(tokens[1], "data_stats") == 0) || (strcmp(tokens[1], "reset") == 0))
            {

                userMessage backOfficeMessage;
                backOfficeMessage.userID = atoi(tokens[0]);
                backOfficeMessage.isRegistration = 0;
                strcpy(backOfficeMessage.category, tokens[1]);
                backOfficeMessage.timeOfRequest = time(NULL);
                for (int i = 0; i < queue_pos; i++)
                {
                    if (others_services_queue[i].isMessageHere == 0)
                    {
                        others_services_queue[i] = backOfficeMessage;
                        others_services_queue[i].isMessageHere = 1;
#ifdef DEBUG
                        printf("Added message from BackOffice to others services queue\n");
#endif
                        char messageToSend[256];
                        sprintf(messageToSend, "[R]: ADDED MESSAGE FROM BACKOFFICE USER TO OTHERS SERVICES QUEUE INDEX %d\n", i);
                        writeToLog(messageToSend);
                        sem_post(&queuesEmpty);
                        break;
                    }
                }
            }
            else
            {
                writeToLog("ERROR: Received invalid message format from backoffice user\n");
            }
        }
        else
        { // comes from mobile user

            userMessage *newMessage = (userMessage *)malloc(sizeof(userMessage));
            if (count == 1)
            { // message structure id#initialplafond

                newMessage->userID = atoi(tokens[0]);
                newMessage->dataToReserve = atoi(tokens[1]);
                newMessage->isRegistration = 1;
            }
            else if (count == 2)
            { // message structure id#category#dataToReserve
                newMessage->userID = atoi(tokens[0]);
                strcpy(newMessage->category, tokens[1]);
                newMessage->dataToReserve = atoi(tokens[2]);
                newMessage->isRegistration = 0;

                // if user does not exist, we discard the message
                int user = userExists(newMessage->userID);
                if (user == 0)
                {
                    writeToLog("Received message from unregistered mobile user\n");
                    continue;
                }
            }
            else
            {
                writeToLog("ERROR: Received invalid message format from mobile user\n");
            }
            time_t now = time(NULL);
            newMessage->timeOfRequest = now;

            // lock for mutual exclusion mutex
            pthread_mutex_lock(&mutualExclusion);

            if (count == 1 || strcmp(newMessage->category, "MUSIC") == 0 || strcmp(newMessage->category, "SOCIAL") == 0)
            {
                int found = 0;

                for (int i = 0; i < queue_pos; i++)
                {
                    if (others_services_queue[i].isMessageHere == 0)
                    {
                        others_services_queue[i] = *newMessage;
                        others_services_queue[i].isMessageHere = 1;
                        char messageToSend[256];
                        sprintf(messageToSend, "[R]: ADDED MESSAGE FROM MOBILE USER TO OTHERS SERVICES QUEUE INDEX %d\n", i);
                        writeToLog(messageToSend);
                        found = 1;
                        sem_post(&queuesEmpty);

                        break;
                    }
                }
                // if no space in others queue, discard message and write to log
                if (!found)
                {
                    // if message is not registration
                    if (count != 1)
                    {
                        char messageToSend[256];
                        sprintf(messageToSend, "OTHERS QUEUE IS FULL DISCARDING MESSAGE: %s#%s#%s\n", tokens[0], tokens[1], tokens[2]);
                        writeToLog(messageToSend);
                    }
                    else
                    {
                        char messageToSend[256];
                        sprintf(messageToSend, "OTHERS QUEUE IS FULL DISCARDIGN REGISTRATION MESSAGE: %s#%s\n", tokens[0], tokens[1]);
                        writeToLog(messageToSend);
                    }
                }
            }
            else if (strcmp(newMessage->category, "VIDEO") == 0)
            {
                int found = 0;
                for (int i = 0; i < queue_pos; i++)
                {
                    if (video_streaming_queue[i].isMessageHere == 0)
                    {
                        video_streaming_queue[i] = *newMessage;
                        video_streaming_queue[i].isMessageHere = 1;
                        char messageToSend[256];
                        sprintf(messageToSend, "[R]: ADDED MESSAGE FROM MOBILE USER TO VIDEO STREAMING QUEUE INDEX %d\n", i);
                        writeToLog(messageToSend);
                        found = 1;
                        sem_post(&queuesEmpty);

                        break;
                    }
                }
                // if no space in video queue, discard message and write to log
                if (!found)
                {
                    char messageToSend[256];
                    sprintf(messageToSend, "[R]: VIDEO QUEUE IS FULL, DISCARDING MESSAGE: %s#%s#%s\n", tokens[0], tokens[1], tokens[2]);
                    writeToLog(messageToSend);
                }
            }

            // unlock for mutual exclusion mutex
            pthread_mutex_unlock(&mutualExclusion);
        }
    }

    pthread_exit(NULL);
}

void authorizationRequestsManager()
{

    // Create named pipes
    pipeCreator();
    // Create Sender
    if (pthread_create(&senderThread, NULL, senderFunction, NULL) != 0)
    {
        errorHandler("ERROR: Not able to create thread sender");
    }
    // Create receiver
    if (pthread_create(&receiverThread, NULL, receiverFunction, NULL) != 0)
    {
        errorHandler("ERROR: Not able to create thread receiver");
    }
    // Create Video_Streaming_Queue and Others_Services_Queue
    queueCreator();
    // Create authorization engine processes
    authEngineCreator();
    pause();
}

void *statsThreadFunction()
{
    while (1)
    {
        sleep(30);

        // write to mqueue mtype 1 the stats
        mQMessageBackOffice message;
        message.mtype = 1;
        sem_wait(shmSem);
        message.stats.nRequestsMusic = shm->stats.nRequestsMusic;
        message.stats.nRequestsSocial = shm->stats.nRequestsSocial;
        message.stats.nRequestsVideo = shm->stats.nRequestsVideo;
        message.stats.totalRequestsMusic = shm->stats.totalRequestsMusic;
        message.stats.totalRequestsSocial = shm->stats.totalRequestsSocial;
        message.stats.totalRequestsVideo = shm->stats.totalRequestsVideo;
        sem_post(shmSem);
#ifdef DEBUG
        printf("About to send stats\n");
        printf("Total requests music: %d\n", message.stats.totalRequestsMusic);
#endif
        msgsnd(mQueueId, &message, sizeof(message), 0);
    }
    return NULL;
}
void monitorEngine()
{
    // create a thread that sends stats every 30 seconds
    pthread_create(&statsThread, NULL, statsThreadFunction, NULL);
    // This will try to access the shared memory every time there is a change in the shared memory
    // To accomplish this, a semaphore will be used
    /*every single time there is a change, this monitor thread would be signaled to look into all the users to see if any of them has reached a limit*/
    while (1)
    {

        sem_wait(shmChangesSem);
        sem_wait(shmSem);
        for (int i = 0; i < mobile_users; i++)
        {
            // if user is registered
            if (shm->users[i].userID != 0)
            {
                int plafond80 = shm->users[i].originalPlafond * 0.2;
                int plafond90 = shm->users[i].originalPlafond * 0.1;
                // if plafoond is 80% used and user was not notified yet,notify
                // if plafond is 90% used and user was not notified yet,notify
                // if plafond is 100% used, notify if not notified yet
                if (shm->users[i].currentPlafond <= plafond80 && shm->users[i].wasNotified == 0)
                {
                    // #ifdef DEBUG
                    char messageToSend[256];
                    sprintf(messageToSend, "ALERT 80%% (%d) TRIGGERED", shm->users[i].userID);
                    writeToLog(messageToSend);
                    // #endif
                    shm->users[i].wasNotified += 1;
                    // Create message of type mQMessage
                    mQMessageMobile message;
                    message.mtype = shm->users[i].userID;
                    message.typeOfAlert = 1;
                    msgsnd(mQueueId, &message, sizeof(message), 0);
                }
                if (shm->users[i].currentPlafond <= plafond90 && shm->users[i].wasNotified == 1)
                {
                    // #ifdef DEBUG
                    char messageToSend[256];
                    sprintf(messageToSend, "ALERT 90%% (%d) TRIGGERED", shm->users[i].userID);
                    writeToLog(messageToSend);
                    // #endif
                    shm->users[i].wasNotified += 1;
                    // Create message of type mQMessage
                    mQMessageMobile message;
                    message.mtype = shm->users[i].userID;
                    message.typeOfAlert = 2;
                    msgsnd(mQueueId, &message, sizeof(message), 0);
#ifdef DEBUG
                    printf("Sent type of alert %d", message.typeOfAlert);
#endif
                }
                if (shm->users[i].currentPlafond <= 0 && shm->users[i].wasNotified == 2)
                {
                    // #ifdef DEBUG
                    char messageToSend[256];
                    sprintf(messageToSend, "ALERT 100%% (%d) TRIGGERED", shm->users[i].userID);
                    writeToLog(messageToSend);
                    // #endif
                    shm->users[i].wasNotified += 1;
                    // Create message of type mQMessage
                    mQMessageMobile message;
                    message.mtype = shm->users[i].userID;
                    message.typeOfAlert = 3;
                    msgsnd(mQueueId, &message, sizeof(message), 0);
                }
            }
        }
        sem_post(shmSem);
    }

    pause();
}
void initializeSharedMemory()
{
    shmid = shmget(IPC_PRIVATE, sizeof(sharedMemory) + (mobile_users * sizeof(user)) + (auth_servers_max + 1 * sizeof(engines)), IPC_CREAT | 0700);
    if (shmid == -1)
    {
        errorHandler("ERROR: Not able to create shared memory");
    }
    shm = (sharedMemory *)shmat(shmid, NULL, 0);
    if (shm == (void *)-1)
    {
        errorHandler("ERROR: Not able to attach shared memory");
    }
    sem_wait(shmSem);
    shm->users = ((void *)shm + sizeof(sharedMemory));
    shm->authEngines = ((void *)shm + sizeof(int) + sizeof(stats) + (mobile_users * sizeof(user)) + sizeof(engines));
    shm->stats.totalRequestsMusic = 0;
    shm->stats.totalRequestsSocial = 0;
    shm->stats.totalRequestsVideo = 0;
    shm->stats.nRequestsMusic = 0;
    shm->stats.nRequestsSocial = 0;
    shm->stats.nRequestsVideo = 0;

    // mark every auth engine as occupied(0)
    for (int i = 0; i < auth_servers_max; i++)
    {
        shm->authEngines[i].available = 0;
        shm->authEngines[i].pid = 0;
    }
    sem_post(shmSem);
}

void messageQueueCreator()
{
    key_t key = ftok("./config.txt", 65);
    mQueueId = msgget(key, 0666 | IPC_CREAT);
    if (mQueueId == -1)
    {
        errorHandler("ERROR: Not able to create message queue");
    }
}

int main(int argc, char *argv[])
{
    sem_destroy(&queuesEmpty);
    sem_unlink("LOG_SEM");
    sem_unlink("SHM_SEM");
    sem_unlink("AUTH_ENGINE_SEM");
    sem_unlink("SHM_CHANGES_SEM");
    sem_unlink("EXTRA_AUTH_ENGINE_SEM");
    pthread_mutex_destroy(&mutualExclusion);
    // unlink named pipes
    unlink("USER_PIPE");
    unlink("BACK_PIPE");

    msgctl(mQueueId, IPC_RMID, NULL);
    shmctl(shmid, IPC_RMID, NULL);

    // Initialize the signal handler
    struct sigaction ctrlc;
    ctrlc.sa_handler = handleSigInt;
    sigfillset(&ctrlc.sa_mask);
    ctrlc.sa_flags = 0;
    sigaction(SIGINT, &ctrlc, NULL);

    originalPid = getpid();
    //  Setup log file
    setupLogFile();
    if (argc != 2)
    {
        printf("Usage: ./5g_auth_platform <config file name>\n");
        exit(1);
    }

    // Read config file
    readConfigFile(argv[1]);

    // Initialize config file variables
    mobile_users = config[0];
    queue_pos = config[1];
    auth_servers_max = config[2];
    auth_proc_time = config[3];
    max_video_wait = config[4];
    max_video_wait = max_video_wait / 1000;
    max_others_wait = config[5];
    max_others_wait = max_others_wait / 1000;
    writeToLog("5G_AUTH_PLATFORM SIMULATOR STARTING");

    authEnginePipes = malloc(sizeof(*authEnginePipes) * auth_servers_max + 1);
    if (authEnginePipes == NULL)
    {
        errorHandler("ERROR: Not possible to create Authorization Engine pipes\n");
    }

    // Semaphore creation function
    syncCreator();
    // Initialize shared memory
    initializeSharedMemory();
    // Create message queue
    messageQueueCreator();
    // Create Authorization Requests Manager
    pid_t pid = fork();
    if (pid == -1)
        errorHandler("ERROR: Not able to create Authorization Requests Manager");
    if (pid == 0)
    {
        authRequestManagerPid = getpid();
        signal(SIGINT, SIG_IGN);
        struct sigaction sigterm;
        sigterm.sa_handler = handleSigterm;
        sigfillset(&sigterm.sa_mask);
        sigterm.sa_flags = 0;
        sigaction(SIGTERM, &sigterm, NULL);

        writeToLog("AUTHORIZATION REQUESTS MANAGER CREATED");
        authorizationRequestsManager();
    }
    // Create Monitor Engine

    pid = fork();
    if (pid == -1)
        errorHandler("ERROR: Not able to create Monitor Engine");
    if (pid == 0) //&& getpid() != authManagerPid
    {
        monitorEnginePid = getpid();
        signal(SIGINT, SIG_IGN);
        struct sigaction sigterm;
        sigterm.sa_handler = handleSigterm;
        sigfillset(&sigterm.sa_mask);
        sigterm.sa_flags = 0;
        sigaction(SIGTERM, &sigterm, NULL);

        writeToLog("MONITOR ENGINE CREATED");
        monitorEngine();
    }

    pause();
    return 0;
}