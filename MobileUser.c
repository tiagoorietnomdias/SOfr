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
#include <unistd.h>
#include <signal.h>
void handleSigInt(int sig)
{
    printf("Received SIGINT\n");
    exit(0);
}
int main(int argc, char *argv[])
{

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
    int initialPlafond = atoi(argv[1]);
    int n_reqs = atoi(argv[2]);
    int intervalVideo = atoi(argv[3]);
    int intervalMusic = atoi(argv[4]);
    int intervalSocial = atoi(argv[5]);
    int dataToReserve = atoi(argv[6]);
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

    // info to send to named pipe
    // char info[100]=ID+initialPLafond;
    pause();
}