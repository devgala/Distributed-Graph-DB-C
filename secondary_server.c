#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>

#define PERMS 0644 // file permissions
#define MAX_GRAPH_NODES 30
#define PRIMARY_SEVER_CODE 1003
#define SECONDARY_SERVER_2_CODE 1002
#define SECONDARY_SERVER_1_CODE 1001
#define CLEANUP_CODE -1

int tidptr = 0;
pthread_t tid[10000];
pthread_attr_t attr[10000];
FILE *fileptr[20];
sem_t *semaphore_write[20];
sem_t *semaphore_read[20];
key_t readerKey;
int reader_id = -1;
int *readerCount;

struct message
{
    long sequence_number;
    int operation_number;
    char mtext[200];
};

struct thread_arg
{
    char filename[200];
    int sequence_number;
} typedef thread_arg;

struct DFSargs
{
    int currentNode;
    int **vis;
    int **adjList;
    char **str;
    int n;
} typedef DFSargs;

void *DFShelper(void *arg)
{
    DFSargs *dfsArgs = (DFSargs *)arg;
    int n = dfsArgs->n;
    int currentNode = dfsArgs->currentNode;
    int **adjList = dfsArgs->adjList;
    int **vis = dfsArgs->vis;
    *((*vis) + currentNode) = 1;
    if (**(adjList + currentNode) == -1 || (*(*(adjList + currentNode) + 1) == -1 && *(*vis + (**(adjList + currentNode))) == 1))
    {
        printf("Deepest : %d\n", currentNode + 1);
        // add to string

        pthread_exit(NULL);
    }
    for (int i = 0; i < n; i++)
    {
        if (*(*(adjList + currentNode) + i) == -1)
            break;
        if (*(*vis + *(*(adjList + currentNode) + i)) == 0)
        {
            dfsArgs->currentNode = *(*(adjList + currentNode) + i);
            pthread_t tid;
            pthread_create(&tid, NULL, DFShelper, (void *)dfsArgs);
            pthread_join(tid, NULL);
        }
    }
}

void *DFS(void *arg)
{
    thread_arg *args = (thread_arg *)arg;
    int index;
    if (args->filename[2] == '.')
    {
        index = args->filename[1] - '0';
    }
    else
    {
        char temp[2];
        temp[0] = args->filename[1];
        temp[1] = args->filename[2];
        index = atoi(temp);
    }

    // sync code
    sem_wait(semaphore_read[index]);
    readerCount[index]++;
    if (readerCount[index] == 1)
        sem_wait(semaphore_write[index]);
    sem_post(semaphore_read[index]);
    
    fileptr[index] = fopen(args->filename, "r");
    if (fileptr[index] == NULL)
    {
        // sem_post(semaphore_write[index]);
        return NULL;
    }

    key_t shmkey;
    if ((shmkey = ftok("client.c", args->sequence_number)) == -1)
    {
        perror("SHM Key could not be created\n");
        return NULL;
    }

    int shmid = -1;
    int *shmptr;
    int temp = 100000;
    shmid = shmget(shmkey, sizeof(int), PERMS);
    if (shmid == -1)
    {
        perror("Error occured in creating SHM segment");
        return NULL;
    }
    shmptr = shmat(shmid, 0, 0);

    if (shmptr == (void *)-1)
    {
        perror("shmat");
        return NULL;
    }
    int starting_node = shmptr[0];
    while (starting_node == 0)
    {
        starting_node = shmptr[0];
    }
    starting_node--;
    int n;

    fscanf(fileptr[index], "%d", &n);

    int adj[n][n];
    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < n; j++)
        {
            fscanf(fileptr[index], "%d", &adj[i][j]);
        }
    }
    fclose(fileptr[index]);
    printf("read complete\n");
    sleep(40);
    sem_wait(semaphore_read[index]);
    readerCount[index]--;
    if (readerCount[index] == 0)
        sem_post(semaphore_write[index]);
    sem_post(semaphore_read[index]);

    int **adjList = (int **)malloc((MAX_GRAPH_NODES + 1) * sizeof(int *));
    for (int i = 0; i < MAX_GRAPH_NODES + 1; i++)
    {
        adjList[i] = (int *)malloc((MAX_GRAPH_NODES + 1) * sizeof(int));
    }
    for (int i = 0; i < n; i++)
    {
        int itr = 0;
        for (int j = 0; j < n; j++)
        {
            if (adj[i][j] == 1)
            {
                adjList[i][itr] = j;
                itr++;
            }
        }
        for (int j = itr; j < MAX_GRAPH_NODES + 1; j++)
        {
            adjList[i][j] = -1;
        }
    }

    for (int i = 0; i < n; i++)
    {

        for (int j = 0; j < n; j++)
        {
            printf("%d ", adjList[i][j]);
        }
        printf("\n");
    }
    char *str = (char *)malloc(200 * sizeof(char));
    int *vis = (int *)malloc((MAX_GRAPH_NODES + 1) * sizeof(int));
    for (int i = 0; i < MAX_GRAPH_NODES + 1; i++)
    {
        vis[i] = 0;
    }
    DFSargs *dfsArgs = malloc(sizeof(DFSargs));
    dfsArgs->n = n;
    dfsArgs->currentNode = starting_node;
    dfsArgs->str = &str;
    dfsArgs->vis = &vis;
    dfsArgs->adjList = adjList;
    pthread_t tid;
    pthread_create(&tid, NULL, DFShelper, (void *)dfsArgs);
    pthread_join(tid, NULL);
    return NULL;
}

void *BFS(void *arg)
{
    return NULL;
}

int main(int argc, char const *argv[])
{
    if ((readerKey = ftok("secondary_server.c", 12345)) == -1)
    {
        perror("SHM Key could not be created\n");
        return 1;
    }

    reader_id = shmget(readerKey, sizeof(int[20]), PERMS | IPC_CREAT);
    if (reader_id == -1)
    {
        perror("Error occured in creating SHM segment");
        return 1;
    }

    readerCount = shmat(reader_id, 0, 0);
    if (readerCount == (void *)-1)
    {
        perror("shmat");
        return 1;
    }

    char sem_name[50];
    for (int i = 0; i < 20; i++)
    {
        sprintf(sem_name, "__writerSemaphore__%d__", i);
        semaphore_write[i] = sem_open(sem_name, O_CREAT, PERMS, 1); // 0x0100 means create if doesnt exist already
    }

    for (int i = 0; i < 20; i++)
    {
        sprintf(sem_name, "__readerSemaphore__%d__", i);
        semaphore_read[i] = sem_open(sem_name, O_CREAT, PERMS, 1); // 0x0100 means create if doesnt exist already
    }

    struct message buf;
    int msqid; // message qid
    int len;
    key_t key;
    // decide how to make it odd even
    int recieveChannel = SECONDARY_SERVER_1_CODE;
    if ((key = ftok("load_balancer.c", 'W')) == -1)
    {
        perror("ftok");
        exit(1);
    }
    if ((msqid = msgget(key, PERMS)) == -1)
    {
        /*Connect to queue*/
        printf("%d\n", msqid);
        perror("msgget");
        exit(1);
    }

    while (1)
    {
        if (msgrcv(msqid, &buf, sizeof(buf.mtext), recieveChannel, 0) == -1)
        {
            perror("msgrcv error");
            exit(1);
        }
        if (buf.operation_number == -1)
            break;
        int operation = buf.operation_number % 10;
        int sequence_number = buf.operation_number / 10;
        thread_arg *args = malloc(sizeof(thread_arg));
        strcpy(args->filename, buf.mtext);
        args->sequence_number = sequence_number;

        pthread_attr_init(&attr[tidptr]);
        if (operation == 3)
            pthread_create(&tid[tidptr], &attr[tidptr], DFS, (void *)args);
        else if (operation == 4)
            pthread_create(&tid[tidptr], &attr[tidptr], BFS, (void *)args);
        tidptr++;
    }

    // cleanup

    return 0;
}
