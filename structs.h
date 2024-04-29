typedef struct user
{
    int userID;
    float currentPlafond;
    int musicUsed;
    int socialUsed;
    int videoUsed;

}user;

typedef struct sharedMemory
{ 
    //array de authorization engine available
    int *authEngineAvailable;
    //array de utilizadores
    user *users;

} sharedMemory;
// pedidos, megas e../

//Estrutura com estatisticas para monitor engine
typedef struct stats{
    int totalRequestsMusic;
    int totalRequestsSocial;
    int totalRequestsVideo;
    int totalRequests;//????

}stats;


// Estruturaa de mensagem para pôr nas mqueues
typedef struct userMessage
{
    int userID;
    char category[10];
    int dataToReserve;
    time_t timeOfRequest;
    int isMessageHere;

} userMessage;
