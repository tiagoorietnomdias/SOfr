#define NUM_USERS 10
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
{
    float currentPlafond;
    music musicUsed;
    social socialUsed;
    video videoUsed;

} sharedMemory;
