// Funcionalidades deste ficheiro, system manager:
// Lê e valida as informações no ficheiro de configurações
// ● Cria os processos Authorization Requests Manager e Monitor Engine
// ● Escreve no log file;
//  Captura o sinal SIGINT para terminar o programa, libertando antes todos os recursos.
/*O ficheiro de configurações deverá seguir a seguinte estrutura:
MOBILE USERS (>=1) - número de Mobile Users que podem ser lançados
QUEUE_POS(>=0) - número de slots nas filas que são utilizadas para armazenar os pedidos de autorização
e os comandos dos utilizadores
AUTH_SERVERS_MAX  (>=1)- número máximo de Authorization Engines que podem ser lançados
AUTH_PROC_TIME -(>=0) período (em ms) que o Authorization Engine demora para processar os pedidos
MAX_VIDEO_WAIT -(>=1) tempo máximo (em ms) que os pedidos de autorização do serviço de vídeo podem
aguardar para serem executados (>=1)
MAX_OTHERS_WAIT (>=1)- tempo máximo (em ms) que os pedidos de autorização dos serviços de música e
de redes sociais, bem como os comandos podem aguardar para serem executados (>=1)*/
/*Auth request manager:
• Cria os named pipes USER_PIPE e BACK_PIPE
• Cria os unnamed pipes para cada Authorization Engine
• Cria as threads Receiver e Sender
• Cria as estruturas de dados internas: Video_Streaming_Queue e Others_Services_Queue
• Criação e remoção dos processos Authorization Engine de acordo com a taxa de ocupação
das filas*/
/* Receiver escreve nas queues e sender lê das queues. Sender vê se há authorization engines disponíveis e, se houver,
escolhe 1 para processar o pedido. Sendar lê da queue e escreve no pipe do authorization engine. Authorization engine lê do pipe
e atualiza a shared memory.
*/

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
sem_t *logSem, *shmSem, *authEngineAvailableSem, *shmChangesSem;
sem_t queuesEmpty;
pid_t originalPid, authRequestManagerPid, monitorEnginePid;
// mutual exclusion is now a pthread mutex
pthread_mutex_t mutualExclusionVideo;
pthread_t senderThread, receiverThread, statsThread;
int shmid, mQueueId;
sharedMemory *shm;
int fdUserPipe, fdBackPipe;
userMessage *video_streaming_queue, *others_services_queue;
int (*authEnginePipes)[2];
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
    printf("Error: %s\n", errorMessage);

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
    writeToLog("SIGINT received");
    writeToLog("5G_AUTH_PLATFORM SIMULATOR CLOSING");

    if (configFile != NULL)
    {
        fclose(configFile);
    }
    // Threads
    pthread_cancel(senderThread);
    pthread_cancel(receiverThread);
    pthread_join(senderThread, NULL);
    pthread_join(receiverThread, NULL);

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
    sem_unlink("LOG_SEM");
    sem_unlink("SHM_SEM");
    sem_unlink("AUTH_ENGINE_SEM");
    sem_unlink("SHM_CHANGES_SEM");
    sem_destroy(&queuesEmpty);
    // Named Pipes
    close(fdUserPipe);
    unlink("USER_PIPE");
    close(fdBackPipe);
    unlink("BACK_PIPE");
    // Queues
    free(video_streaming_queue);

    // Message Queue
    msgctl(mQueueId, IPC_RMID, NULL);

    // free(others_services_queue);
    // Mutex
    pthread_mutex_destroy(&mutualExclusionVideo);

    // Log File
    fclose(logFile);

    exit(0);
}
void handleSigInt(int sig)
{
    if (getpid() != originalPid && getpid() != authRequestManagerPid && getpid() != monitorEnginePid)
    {
        exit(0);
    }
    else if (getpid() == authRequestManagerPid)
    {
        sem_wait(shmSem);
        for (int i = 0; i < auth_servers_max; i++)
        {
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
    else if (getpid() == monitorEnginePid)
    {
        pthread_cancel(statsThread);
        pthread_join(statsThread, NULL);
        exit(0);
    }
    else
    {
        waitpid(authRequestManagerPid, NULL, 0);
        waitpid(monitorEnginePid, NULL, 0);
    }
    // Authorization Engine processes
    // if pid is different from the original pid, exit(0)
    writeToLog("SIGINT received");
    writeToLog("5G_AUTH_PLATFORM SIMULATOR CLOSING");

    if (configFile != NULL)
    {
        fclose(configFile);
    }
    // Threads
    pthread_cancel(senderThread);
    pthread_cancel(receiverThread);
    pthread_join(senderThread, NULL);
    pthread_join(receiverThread, NULL);

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
    sem_unlink("LOG_SEM");
    sem_unlink("SHM_SEM");
    sem_unlink("AUTH_ENGINE_SEM");
    sem_unlink("SHM_CHANGES_SEM");
    sem_destroy(&queuesEmpty);
    // Named Pipes
    close(fdUserPipe);
    unlink("USER_PIPE");
    close(fdBackPipe);
    unlink("BACK_PIPE");
    // Queues
    free(video_streaming_queue);

    // free(others_services_queue);
    // Mutex
    pthread_mutex_destroy(&mutualExclusionVideo);

    // Message Queue
    msgctl(mQueueId, IPC_RMID, NULL);

    // Log File
    fclose(logFile);

    exit(0);
}
void setupLogFile()
{
    logFile = fopen("log.txt", "w");
    if (logFile == NULL)
    {
        errorHandler("Could not open log file");
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
        errorHandler("Could not open config file\n");
    }
    char line[30];
    int i;
    for (i = 0; i < 6; i++)
    {
        fgets(line, 30, configFile);
        if ((sscanf(line, "%d", &config[i]) != 1))
        {
            errorHandler("Wrong config file format");
        }
        if (config[i] < 0)
        {
            errorHandler("Config file values must be positive");
        }
        else if ((i == 0 || i == 1 || i == 3 || i == 4) && config[i] < 1)
        {
            errorHandler("MOBILE USERS, AUTH_SERVERS_MAX, MAX_VIDEO_WAIT and MAX_OTHERS_WAIT >=1");
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
    // Zona de exclusão mútua
    if (pthread_mutex_init(&mutualExclusionVideo, NULL) != 0)
    {
        errorHandler("ERROR: Not possible to create mutual exclusion mutex\n");
    }
    shmChangesSem = sem_open("SHM_CHANGES_SEM", O_CREAT | O_EXCL, 0700, 0);
    if (shmChangesSem == SEM_FAILED)
    {
        errorHandler("ERROR: Not possible to create shared memory changes semaphore\n");
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

    sem_wait(shmSem);
    // Mark this as available
    shm->authEngines[index].available = 1;
    sem_post(shmSem);

    // post semaphore
    sem_post(authEngineAvailableSem);

    // wait for message from sender
    char messageToRead[256];
    int readValue;
    while (1)
    {
        if ((readValue = read(authEnginePipes[index][0], &messageToRead, sizeof(messageToRead))) <= 0)
        {
            errorHandler("ERROR: Not possible to read from Authorization Engine pipe\n");
        }
        messageToRead[readValue] = '\0';
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
            if (atoi(tokens[2]) == 0) // if data to reserve is 0, it is a registration message
            {
                
                for (int i = 0; i < mobile_users; i++)
                {
                    if (shm->users[i].userID == 0) // if we find an empty user, we register the new user
                    {
                        shm->users[i].userID = atoi(tokens[0]);
                        shm->users[i].currentPlafond = atoi(tokens[1]);
                        shm->users[i].musicUsed = 0;
                        shm->users[i].socialUsed = 0;
                        shm->users[i].videoUsed = 0;
                        shm->users[i].originalPlafond = atoi(tokens[1]);
                        shm->users[i].wasNotified = 0;
                        break;
                    }
                    else if (i == mobile_users - 1) // if we reach the end of the array and no empty user is found, we discard the message
                    {
                        printf("User %d could not be registered\n", atoi(tokens[0]));
                    }
                }
            }
            else // if data to reserve is not 0, it is a data request message

                for (int i = 0; i < mobile_users; i++)
                {
                    if (shm->users[i].userID == atoi(tokens[0]))
                    {
                        shm->users[i].currentPlafond -= atoi(tokens[2]);
                        if (strcmp(tokens[1], "MUSIC") == 0)
                        {
                            shm->users[i].musicUsed += atoi(tokens[2]);
                            shm->stats.totalRequestsMusic += atoi(tokens[2]);
                            shm->stats.nRequestsMusic++;
                        }
                        else if (strcmp(tokens[1], "SOCIAL") == 0)
                        {
                            shm->users[i].socialUsed += atoi(tokens[2]);
                            shm->stats.totalRequestsSocial += atoi(tokens[2]);
                            shm->stats.nRequestsSocial++;
                        }
                        else if (strcmp(tokens[1], "VIDEO") == 0)
                        {
                            shm->users[i].videoUsed += atoi(tokens[2]);
                            shm->stats.totalRequestsVideo += atoi(tokens[2]);
                            shm->stats.nRequestsVideo++;
                        }
                        else
                        {
                            errorHandler("ERROR: Received invalid category from sender\n");
                        }
                        break;
                    }
                }
            }
            // mark this auth engine as available
            shm->authEngines[index].available = 1;
            sem_post(shmChangesSem);
            sem_post(shmSem);
            sem_post(authEngineAvailableSem);
        }
        else
        {
            errorHandler("ERROR: Received invalid message format from sender\n");
        }
    }
}
void authEngineCreator()
{
    // Criação do semáforo para os Authorization Engines
    authEngineAvailableSem = sem_open("AUTH_ENGINE_SEM", O_CREAT | O_EXCL, 0700, 0);
    if (authEngineAvailableSem == SEM_FAILED)
    {
        errorHandler("Not possible to create Authorization Engine available semaphore\n");
    }
    // Criação dos unnamed pipes para os Authorization Engines
    for (int i = 0; i < auth_servers_max; i++)
    {
        if (pipe(authEnginePipes[i]) == -1)
        {
            errorHandler("Not possible to create Authorization Engine pipes\n");
        }
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
            // child process
            // add pid to shared memory
            sem_wait(shmSem);
            shm->authEngines[i].pid = getpid();
            sem_post(shmSem);
            authEngineFunction(i);
        }
    }
}

void *senderFunction()
{
    writeToLog("SENDER THREAD SUCCESSFULLY CREATED");

    while (1)
    {
        // WAIT FOR AUTHORIZATION ENGINE TO BE AVAILABLE
        sem_wait(authEngineAvailableSem);

        // get index of the first available auth engine
        sem_wait(shmSem);
        int index = -1;
        for (int i = 0; i < auth_servers_max; i++)
        {
#ifdef DEBUG
            printf("authEngineAvailable[%d] = %d\n", i, shm->authEngines[i].available);
#endif
            if (shm->authEngines[i].available == 1)
            {
                shm->authEngines[i].available = 0;
                index = i;
                break;
            }
        }
        sem_post(shmSem);
        // wait for message from receiver
        sem_wait(&queuesEmpty);
        // lock for mutual exclusion mutex
        pthread_mutex_lock(&mutualExclusionVideo);
        // go through the queue to find message to send
        int foundMessage = 0;
        for (int i = 0; i < queue_pos; i++)
        {
            if (video_streaming_queue[i].isMessageHere == 1)
            {
                foundMessage = 1;
                // send message to Authorization Engine
                char messageToSend[256];
                sprintf(messageToSend, "%d#%s#%d", video_streaming_queue[i].userID, video_streaming_queue[i].category, video_streaming_queue[i].dataToReserve);
                if (write(authEnginePipes[index][1], messageToSend, sizeof(messageToSend)) <= 0)
                {
                    errorHandler("Not possible to write to Authorization Engine pipe\n");
                }
                // remove message from queue
                video_streaming_queue[i].isMessageHere = 0;
                break;
            }
        }
        if (!foundMessage)
        { // look for message in other services queue
            for (int i = 0; i < queue_pos; i++)
            {
                // if message is here
                if (others_services_queue[i].isMessageHere == 1)
                {
                    
                    // message can either be registration or data request
                    if (others_services_queue[i].isRegistration == 1)
                    {
                        // send message to Authorization Engine
                        char messageToSend[256];
                        sprintf(messageToSend, "%d#%s#%d", others_services_queue[i].userID, "registration", others_services_queue[i].dataToReserve);
                        if (write(authEnginePipes[index][1], messageToSend, sizeof(messageToSend)) <= 0)
                        {
                            errorHandler("Not possible to write to Authorization Engine pipe\n");
                        }
                        // remove message from queue
                        others_services_queue[i].isMessageHere = 0;
                    }
                    else
                    {
                        // send message to Authorization Engine
                        char messageToSend[256];
                        sprintf(messageToSend, "%d#%s#%d", others_services_queue[i].userID, others_services_queue[i].category, others_services_queue[i].dataToReserve);
                        if (write(authEnginePipes[index][1], messageToSend, sizeof(messageToSend)) <= 0)
                        {
                            errorHandler("Not possible to write to Authorization Engine pipe\n");
                        }
                        // remove message from queue
                        others_services_queue[i].isMessageHere = 0;
                    }
                }
            }
        }
        // unlock for mutual exclusion mutex
        pthread_mutex_unlock(&mutualExclusionVideo);
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
    writeToLog("RECEIVER THREAD SUCCESSFULLY CREATED");
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
            pthread_mutex_lock(&mutualExclusionVideo);

            if (count == 1 || strcmp(newMessage->category, "MUSIC") == 1 || strcmp(newMessage->category, "SOCIAL") == 1)
            {
                for (int i = 0; i < queue_pos; i++)
                {
                    if (others_services_queue[i].isMessageHere == 0)
                    {
                        printf("Adding message to others services queue\n");
                        others_services_queue[i] = *newMessage;
                        others_services_queue[i].isMessageHere = 1;
                        break;
                    }
                }
            }
            else if (strcmp(newMessage->category, "VIDEO") == 1)
            {
                for (int i = 0; i < queue_pos; i++)
                {
                    if (video_streaming_queue[i].isMessageHere == 0)
                    {
                        video_streaming_queue[i] = *newMessage;
                        video_streaming_queue[i].isMessageHere = 1;
                        break;
                    }
                }
            }

            // unlock for mutual exclusion mutex
            pthread_mutex_unlock(&mutualExclusionVideo);
            sem_post(&queuesEmpty);
        }
    }

    pthread_exit(NULL);
}

void authorizationRequestsManager()
{
    // Create authorization engine processes
    authEngineCreator();
    // Create named pipes
    pipeCreator();
    // Create Sender
    if (pthread_create(&senderThread, NULL, senderFunction, NULL) != 0)
    {
        errorHandler("Not able to create thread sender");
    }
    // Create receiver
    if (pthread_create(&receiverThread, NULL, receiverFunction, NULL) != 0)
    {
        errorHandler("Not able to create thread receiver");
    }
    // Create Video_Streaming_Queue and Others_Services_Queue
    queueCreator();

    pause();
}

void *statsThreadFunction()
{
    while (1)
    {
        sleep(10);

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
                    printf("User %d has 80%% of the plafond used\n", shm->users[i].userID);
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
                    printf("User %d has 90%% of the plafond used\n", shm->users[i].userID);
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
                    printf("User %d has 100%% of the plafond used\n", shm->users[i].userID);
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
    shmid = shmget(IPC_PRIVATE, sizeof(sharedMemory) + (mobile_users * sizeof(user)) + (auth_servers_max * sizeof(engines)), IPC_CREAT | 0700);
    if (shmid == -1)
    {
        errorHandler("Not able to create shared memory");
    }
    shm = (sharedMemory *)shmat(shmid, NULL, 0);
    if (shm == (void *)-1)
    {
        errorHandler("Not able to attach shared memory");
    }
    sem_wait(shmSem);
    shm->users = ((void *)shm + sizeof(stats) + sizeof(engines) + sizeof(user));
    shm->authEngines = ((void *)shm + sizeof(stats) + (mobile_users * sizeof(user)) + sizeof(engines));
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
        errorHandler("Not able to create message queue");
    }
}

int main(int argc, char *argv[])
{
    sem_destroy(&queuesEmpty);
    sem_unlink("LOG_SEM");
    sem_unlink("SHM_SEM");
    sem_unlink("AUTH_ENGINE_SEM");
    sem_unlink("SHM_CHANGES_SEM");
    pthread_mutex_destroy(&mutualExclusionVideo);
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
    max_others_wait = config[5];
    writeToLog("5G_AUTH_PLATFORM SIMULATOR STARTING");

    authEnginePipes = malloc(sizeof(*authEnginePipes) * auth_servers_max);
    if (authEnginePipes == NULL)
    {
        errorHandler("Not possible to create Authorization Engine pipes\n");
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
        errorHandler("Not able to create Authorization Requests Manager");
    if (pid == 0)
    {

        // signal(SIGINT, SIG_IGN);
        authRequestManagerPid = getpid();
        writeToLog("AUTHORIZATION REQUESTS MANAGER CREATED");
        authorizationRequestsManager();
    }
    // Create Monitor Engine

    pid = fork();
    if (pid == -1)
        errorHandler("Not able to create Monitor Engine");
    if (pid == 0) //&& getpid() != authManagerPid
    {
        monitorEnginePid = getpid();
        writeToLog("MONITOR ENGINE CREATED");
        monitorEngine();
    }

    pause();
    return 0;
}