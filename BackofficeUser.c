/*Processo que gere informação agregada dos plafonds dos utilizadores. Recebe estatísticas periódicas
(produzidas pelo Monitor Engine) através da Message Queue. Pode também, proactivamente,
solicitar estatísticas utilizando um comando específico. Neste caso, o comando é enviado para o
Authorization Request Manager através do named pipe BACK_PIPE. O Authorization Engine é
responsável por processar as estatísticas e enviar as mesmas ao BackOffice User através da Message
Queue.
Sintaxe do comando de inicialização do processo BackOffice User:
$ backoffice_user
Exemplo:
$ backoffice_user
Informação a enviar para o named pipe:
ID_backoffice_user#[data_stats | reset]
O identificador do BackOffice User a utilizar é 1.
Este processo recebe os seguintes comandos do utilizador:
● data_stats - apresenta estatísticas referentes aos consumos dos dados nos vários serviços:
total de dados reservados e número de pedidos de renovação de autorização;
● reset - limpa as estatísticas relacionadas calculadas até ao momento pelo sistema.
O processo termina ao receber um sinal SIGINT, ou em caso de erro. Um erro pode acontecer se algum
parâmetro estiver errado ou ao tentar escrever para o named pipe e a escrita falhar, casos em que
deverá escrever a mensagem de erro no ecrã. Sempre que termina, o processo deve limpar todos os
recursos*/
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>

void handleSigInt(int sig)
{
    printf("Received SIGINT\n");
    exit(0);
}
int main()
{

    struct sigaction ctrlc;
    ctrlc.sa_handler = handleSigInt;
    sigfillset(&ctrlc.sa_mask);
    ctrlc.sa_flags = 0;
    sigaction(SIGINT, &ctrlc, NULL);
    while (1)
    {
        char command[100];
        // Receber comandos do utilizador
        if (scanf("%s", command) == 1)
        {
            if (strcmp(command, "data_stats") == 0)
            {
                printf("printing data stats\n");
            }
            else if (strcmp(command, "reset") == 0)
            {
                printf("resetting\n");
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