// Funcionalidades deste ficheiro, system manager:
// Lê e valida as informações no ficheiro de configurações
// ● Cria os processos Authorization Requests Manager e Monitor Engine
// ● Escreve no log file;
//  Captura o sinal SIGINT para terminar o programa, libertando antes todos os recursos.
/*O ficheiro de configurações deverá seguir a seguinte estrutura:
QUEUE_POS(>=0) - número de slots nas filas que são utilizadas para armazenar os pedidos de autorização
e os comandos dos utilizadores
AUTH_SERVERS_MAX  (>=1)- número máximo de Authorization Engines que podem ser lançados
AUTH_PROC_TIME -(>=0) período (em ms) que o Authorization Engine demora para processar os pedidos
MAX_VIDEO_WAIT -(>=1) tempo máximo (em ms) que os pedidos de autorização do serviço de vídeo podem
aguardar para serem executados (>=1)
MAX_OTHERS_WAIT (>=1)- tempo máximo (em ms) que os pedidos de autorização dos serviços de música e
de redes sociais, bem como os comandos podem aguardar para serem executados (>=1)*/
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
FILE *configFile, *logFile;
int config[5];
int queue_pos, auth_servers_max, auth_proc_time, max_video_wait, max_others_wait;
sem_t *logSem;
pthread_t senderThread, receiverThread;
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

    writeToLog("5G_AUTH_PLATFORM SIMULATOR CLOSING");
    fclose(logFile);
    sem_close(logSem);
    sem_unlink("LOG_SEM");
    exit(1);
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
    for (i = 0; i < 5; i++)
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
        else if ((i == 1 || i == 3 || i == 4) && config[i] < 1)
        {
            errorHandler("AUTH_SERVERS_MAX, MAX_VIDEO_WAIT and MAX_OTHERS_WAIT >=1");
        }
    }
    fclose(configFile);
    configFile = NULL;
}
// authorizationRequestsManager: cria threads Sender e Receiver
void *senderFunction()
{
    pthread_exit(NULL);
}
void *receiverFunction()
{
    pthread_exit(NULL);
}
void authorizationRequestsManager()
{
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
}
void monitorEngine()
{
    //exit(0);
}
int main(int argc, char *argv[])
{
    sem_unlink("LOG_SEM");

    // pid_t originalPid = getpid();
    pid_t authManagerPid, monitorEnginePid;
    pid_t parentPid = getpid();
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
    queue_pos = config[0];
    auth_servers_max = config[1];
    auth_proc_time = config[2];
    max_video_wait = config[3];
    max_others_wait = config[4];
    writeToLog("5G_AUTH_PLATFORM SIMULATOR STARTING");
    // Create Authorization Requests Manager
    pid_t pid = fork();
    if (pid == -1)
        errorHandler("Not able to create Authorization Requests Manager");
    if (pid == 0)
    {
        authManagerPid = getpid();
        //  Authorization Requests Manager
        writeToLog("AUTHORIZATION REQUESTS MANAGER CREATED");
        authorizationRequestsManager();
    }
    // Create Monitor Engine
    if (getpid() == parentPid)
    {
        pid = fork();
        if (pid == -1)
            errorHandler("Not able to create Monitor Engine");
        if (pid == 0 && getpid() != parentPid && getpid() != authManagerPid)
        {
            monitorEnginePid = getpid();
            //  Monitor Engine
            writeToLog("MONITOR ENGINE CREATED");
            monitorEngine();
        }
    }
    
    writeToLog("5G_AUTH_PLATFORM SIMULATOR CLOSING");
    // wait for auth manager
    waitpid(authManagerPid, NULL, 0);
    // wait for monitor engine
    waitpid(monitorEnginePid, NULL, 0);
    return 0;
}