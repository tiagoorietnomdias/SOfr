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
#define DEBUG 1

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
#include "structs.h"

FILE *configFile, *logFile;
int config[5];
int mobile_users, queue_pos, auth_servers_max, auth_proc_time, max_video_wait, max_others_wait;
sem_t *logSem, *shmSem;
pthread_t senderThread, receiverThread;
int shmid;
sharedMemory *shm;
int fdUserPipe, fdBackPipe;
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

    if (configFile != NULL)
    {
        fclose(configFile);
    }
    pthread_cancel(senderThread);
    pthread_cancel(receiverThread);
    pthread_join(senderThread, NULL);
    pthread_join(receiverThread, NULL);

    shmdt(shm);
    shmctl(shmid, IPC_RMID, NULL);

    writeToLog("5G_AUTH_PLATFORM SIMULATOR CLOSING");
    fclose(logFile);
    sem_close(logSem);
    sem_close(shmSem);
    sem_unlink("LOG_SEM");
    sem_unlink("SHM_SEM");
    close(fdUserPipe);
    unlink("USER_PIPE");
    close(fdBackPipe);
    unlink("BACK_PIPE");

    exit(1);
}
void handleSigInt(int sig)
{
    if (getpid() == 0)
    {
        return;
    }
    writeToLog("SIGINT received");
    if (configFile != NULL)
    {
        fclose(configFile);
    }
    // close(authorizationRequestsManager);
    // close(monitorEngine);

    pthread_cancel(senderThread);
    pthread_cancel(receiverThread);
    pthread_join(senderThread, NULL);
    pthread_join(receiverThread, NULL);
    writeToLog("5G_AUTH_PLATFORM SIMULATOR CLOSING");
    fclose(logFile);
    sem_close(logSem);
    sem_close(shmSem);
    sem_unlink("LOG_SEM");
    sem_unlink("SHM_SEM");
    shmdt(shm);
    shmctl(shmid, IPC_RMID, NULL);
    close(fdUserPipe);
    unlink("USER_PIPE");
    close(fdBackPipe);
    unlink("BACK_PIPE");

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
#ifdef DEBUG
    printf("USER_PIPE created and opened\n");
#endif
    if (mkfifo("BACK_PIPE", 0666) == -1)
    {
        errorHandler("ERROR: Not possible to create BACK_PIPE\n");
    }
    if ((fdBackPipe = open("BACK_PIPE", O_RDWR)) < 0)
    {
        errorHandler("ERROR: Not possible to open BACK_PIPE\n");
    }
    // Criação dos unnamed pipes
    /*for (int i = 0; i < auth_servers_max; i++)
    {
        if (pipe(shm->pipes[i]) == -1)
        {
            errorHandler("ERROR: Not possible to create unnamed pipes\n");
        }
    }*/
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
}

// authorizationRequestsManager: cria threads Sender e Receiver
void *senderFunction()
{
    writeToLog("SENDER THREAD SUCCESSFULLY CREATED");
    pthread_exit(NULL);
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
            if (strcmp(tokens[1], "data_stats") == 0)
            {
                printf("Data_stats: %s %s\n", tokens[0], tokens[1]);
            }
            else if (strcmp(tokens[1], "reset") == 0)
            {
                printf("Reset: %s %s\n", tokens[0], tokens[1]);
            }
            else
            {
                writeToLog("ERROR: Received invalid message format from backoffice user\n");
            }
        }
        else
        { // comes from mobile user
            if (count == 1)
            {
                printf("Registration message: %s %s\n", tokens[0], tokens[1]);
                // registration message
                // add user to shm

                // message format: idToAdd#initialPlafond
            }
            else if (count == 2)
            {
                printf("Data: %s %s %s\n", tokens[0], tokens[1], tokens[2]);
                // data request message
                // message format: idToRequest#category#dataToReserve
            }
            else
            {
                writeToLog("ERROR: Received invalid message format from mobile user\n");
            }
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
        errorHandler("Not able to create thread sender");
    }
    // Create receiver
    if (pthread_create(&receiverThread, NULL, receiverFunction, NULL) != 0)
    {
        errorHandler("Not able to create thread receiver");
    }
    pause();
}
void monitorEngine()
{
    pause();
    // exit(0);
}
void initializeSharedMemory()
{

    shmid = shmget(IPC_PRIVATE, sizeof(sharedMemory), IPC_CREAT | 0700);
    if (shmid == -1)
    {
        errorHandler("Not able to create shared memory");
    }
    shm = (sharedMemory *)shmat(shmid, NULL, 0);
    if (shm == (void *)-1)
    {
        errorHandler("Not able to attach shared memory");
    }
}
int main(int argc, char *argv[])
{
    sem_unlink("LOG_SEM");
    sem_unlink("SHM_SEM");

    // Initialize the signal handler
    struct sigaction ctrlc;
    ctrlc.sa_handler = handleSigInt;
    sigfillset(&ctrlc.sa_mask);
    ctrlc.sa_flags = 0;
    sigaction(SIGINT, &ctrlc, NULL);

    // pid_t originalPid = getpid();
    // pid_t authManagerPid, monitorEnginePid;
    // pid_t parentPid = getpid();
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
    // Semaphore creation function
    syncCreator();
    // Initialize shared memory
    initializeSharedMemory();
    // Create Authorization Requests Manager
    pid_t pid = fork();
    if (pid == -1)
        errorHandler("Not able to create Authorization Requests Manager");
    if (pid == 0)
    {
        signal(SIGINT, SIG_IGN);
        // authManagerPid = getpid();
        //   Authorization Requests Manager
        writeToLog("AUTHORIZATION REQUESTS MANAGER CREATED");
        authorizationRequestsManager();
        exit(0);
    }
    // Create Monitor Engine

    pid = fork();
    if (pid == -1)
        errorHandler("Not able to create Monitor Engine");
    if (pid == 0) //&& getpid() != authManagerPid
    {
        signal(SIGINT, SIG_IGN);
        // monitorEnginePid = getpid();
        //   Monitor Engine
        writeToLog("MONITOR ENGINE CREATED");
        monitorEngine();
        exit(0);
    }

    pause();
    return 0;
}