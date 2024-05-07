/* Guilherme Eufrásio Rodrigues - 2021218943
Tiago Monteiro Dias - 2021219480*/

typedef struct user
{
    int userID;
    float currentPlafond;
    int musicUsed;
    int socialUsed;
    int videoUsed;
    float originalPlafond;
    int wasNotified;

} user;
// Estrutura com estatisticas para monitor engine
typedef struct stats
{
    int totalRequestsMusic;
    int nRequestsMusic;
    int totalRequestsSocial;
    int nRequestsSocial;
    int totalRequestsVideo;
    int nRequestsVideo;

} stats;
typedef struct engines
{
    int available;
    pid_t pid;
} engines;

typedef struct sharedMemory
{
    int extraVerifier;
    // array de authorization engine available
    engines *authEngines;
    // array de utilizadores
    user *users;
    stats stats;

} sharedMemory;
// pedidos, megas e../

// Estruturaa de mensagem para pôr na video queue e other services queue
typedef struct userMessage
{
    int userID;
    char category[10];
    int dataToReserve;
    time_t timeOfRequest;
    int isMessageHere;
    int isRegistration;

} userMessage;
// Estrutura de mensagem para pôr na message queue
typedef struct mQMessageMobile
{
    long mtype;
    int typeOfAlert;
} mQMessageMobile;

typedef struct mQMessageBackOffice
{
    long mtype;
    stats stats;

} mQMessageBackOffice;
