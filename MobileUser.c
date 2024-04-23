/*Mobile User
Processo que gera pedidos de autorização para cada um dos 3 serviços do simulador (streaming de
vídeo, streaming de música e redes sociais). O Mobile User gera duas mensagens:
1. Registo inicial: mensagem inicial para simular o registo do Mobile User na plataforma de
autorizações de serviço. Neste pedido terá que ser indicado o plafond inicial do Mobile User.
Este valor é registado na Shared Memory.
2. Pedido de autorização: mensagem para simular os pedidos de autorização de serviço do
Mobile User. Estas mensagens são enviadas em intervalos periódicos (Δt), específicos para
cada tipo de serviço. Para cada pedido de autorização é indicada a quantidade de dados a
reservar do plafond disponível. Este passo repete-se até o número máximo de pedidos de
autorização estar concluído ou o plafond esgotado
O plafond inicial, o número de pedidos de autorização a enviar, os intervalos periódicos de
renovação (Δt) por serviço e a quantidade de dados a reservar em cada pedido de renovação é
fornecido através da linha de comandos no arranque do Mobile User.
Cada um dos processos Mobile User envia as mensagens através do named pipe USER_PIPE. Podemos
ter um ou mais processos destes a correr em simultâneo, cada um com os seus parâmetros.
Sintaxe do comando de inicialização do processo Mobile User:
$ mobile_user /
{plafond inicial} /
{número de pedidos de autorização} /
{intervalo VIDEO} {intervalo MUSIC} {intervalo SOCIAL} /
{dados a reservar}
mobile_user 800 50 10 20 5 40
O identificador do Mobile_User, correspondente ao PID, será utilizado para agrupar a informação do
utilizador na memória partilhada.
O Mobile_Userrecebe alertas sobre o plafond de dados (80%, 90%, 100%) através da Message Queue.
O processo Mobile User termina quando uma das seguintes condições se verificar:
1. Receção de um sinal SIGINT;
2. Receção de um alerta de 100% relativo ao plafond de dados;
3. No caso de o número máximo de pedidos de autorização ser atingido;
4. Em caso de erro - um erro pode acontecer se algum parâmetro estiver errado ou ao tentar
escrever para o named pipe e a escrita falhar. Nestes casos deve escrever a mensagem de erro
no ecrã.
Sempre que o Mobile User termina, o processo deve limpar todos os recursos*/

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
#define _XOPEN_SOURCE 700
int fdUserPipe;
char messageToSend[128];
int currentRequests = 0;
int initialPlafond, n_reqs, intervalVideo, intervalMusic, intervalSocial, dataToReserve;
sem_t *mobile_sem;
void handleSigInt(int sig)
{
    sem_unlink("MOBILE_SEM");
    printf("Received SIGINT\n");

    exit(0);
}
void writeToPipe(char *category)
{
    sprintf(messageToSend, "%d#%s#%d", getpid(), category, dataToReserve);
    write(fdUserPipe, messageToSend, strlen(messageToSend) + 1);
    currentRequests++;
}
void *socialFunction()
{
    while (currentRequests < n_reqs) // plafond esgotado
    {
        sleep(intervalSocial);
        sem_wait(mobile_sem);
        writeToPipe("SOCIAL");
        sem_post(mobile_sem);
    }
    return NULL;
}
void *musicFunction()
{
    while (currentRequests < n_reqs) // plafond esgotado
    {
        sleep(intervalMusic);
        sem_wait(mobile_sem);
        writeToPipe("MUSIC");
        sem_post(mobile_sem);
    }
    return NULL;
}
void *videoFunction()
{
    while (currentRequests < n_reqs) // plafond esgotado
    {
        sleep(intervalVideo);
        sem_wait(mobile_sem);
        writeToPipe("VIDEO");
        sem_post(mobile_sem);
    }
    return NULL;
}
int main(int argc, char *argv[])
{
    sem_unlink("MOBILE_SEM");
    pthread_t musicThread, socialThread, videoThread;
    // Semaforo para escrita no pipe

    struct sigaction ctrlc;
    ctrlc.sa_handler = handleSigInt;
    sigfillset(&ctrlc.sa_mask);
    ctrlc.sa_flags = 0;
    sigaction(SIGINT, &ctrlc, NULL);
    // Receive initial arguments:plafond inicial,número de pedidos de autorização,intervalo VIDEO,intervalo MUSIC,intervalo SOCIAL,dados a reservar
    if (argc != 7)
    {
        printf("Usage: ./mobile_user <initial plafond>, <n_reqs>, <intervalVideo>, <intervalMusic>, <intervalSocial>, <data to reserve> \n");
        exit(1);
    }

    // Parse arguments
    initialPlafond = atoi(argv[1]);
    n_reqs = atoi(argv[2]); // SE CARATERES NÃO FOREM NUMEROS
    intervalVideo = atoi(argv[3]);
    intervalMusic = atoi(argv[4]);
    intervalSocial = atoi(argv[5]);
    dataToReserve = atoi(argv[6]);
    if (initialPlafond < 0 || n_reqs < 0 || intervalVideo < 0 || intervalMusic < 0 || intervalSocial < 0 || dataToReserve < 0)
    {
        printf("Usage: all arguments must be >0\n");
        exit(1);
    }

    printf("Initial plafond: %d\n", initialPlafond);
    printf("Number of requests: %d\n", n_reqs);
    printf("Interval Video: %d\n", intervalVideo);
    printf("Interval Music: %d\n", intervalMusic);
    printf("Interval Social: %d\n", intervalSocial);
    printf("Data to reserve: %d\n", dataToReserve);

    // Open named pipe
    if ((fdUserPipe = open("USER_PIPE", O_WRONLY)) < 0)
    {
        printf("Error opening USER_PIPE\n");
    }

    mobile_sem = sem_open("MOBILE_SEM", O_CREAT | O_EXCL, 0700, 1);
    if (mobile_sem == SEM_FAILED)
    {
        printf("ERROR: Not possible to create mobile_sem semaphore\n");
        exit(1);
    }

    // write to pipe
    // Register message
    sprintf(messageToSend, "%d#%d", getpid(), initialPlafond);
    write(fdUserPipe, messageToSend, strlen(messageToSend) + 1);
    // Thread creation, one for each service
    // Social
    if (pthread_create(&socialThread, NULL, socialFunction, NULL) != 0)
    {
        printf("Not able to create thread social");
        exit(1);
    }
    // Music
    if (pthread_create(&musicThread, NULL, musicFunction, NULL) != 0)
    {
        printf("Not able to create thread music");
        exit(1);
    }
    // Video
    if (pthread_create(&videoThread, NULL, videoFunction, NULL) != 0)
    {
        printf("Not able to create thread video");
        exit(1);
    }
    pause();

    return 0;
}
