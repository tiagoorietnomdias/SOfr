typedef struct music
{
    float usedPlafond;
    int authReqNumber;
} music;
typedef struct social
{
    float usedPlafond;
    int authReqNumber;
} social;
typedef struct video
{
    float usedPlafond;
    int authReqNumber;
} video;
typedef struct sharedMemory
{ /// adicionar estrutura com estatisticas para monitor engine
    int userID;
    float currentPlafond;
    music musicUsed;
    social socialUsed;
    video videoUsed;

} sharedMemory;
// pedidos, megas e..

typedef struct userMessage
{
    int userID;
    char category[10];
    int dataToReserve;
    time_t timeOfRequest;
    int isMessageHere;

} userMessage;
//Estruturaa de mensagem para pôr nas mqueues