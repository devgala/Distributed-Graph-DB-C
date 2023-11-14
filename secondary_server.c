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
sem_t *semaphore_write[21];
sem_t *semaphore_read[21];
key_t readerKey;
int reader_id = -1;
int *readerCount;
int msqid; // message qid
key_t key;
sem_t BFSsemaphore;

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
    long unique_id;
} typedef thread_arg;

struct TraverseArgs
{
    int currentNode;
    int *vis;
    int **adjList;
    char *str;
    int n;
    char *returnValue;
} typedef TraverseArgs;

void *DFShelper(void *arg)
{
    TraverseArgs *traverseArgs = (TraverseArgs *)arg;
    int n = traverseArgs->n;
    int currentNode = traverseArgs->currentNode;
    int **adjList = traverseArgs->adjList;
    int *vis = traverseArgs->vis;
    vis[currentNode] = 1;
    if (**(adjList + currentNode) == -1 || (*(*(adjList + currentNode) + 1) == -1 && vis[**(adjList + currentNode)] == 1))
    {
        printf("Deepest : %d\n", currentNode + 1);
        // add to string
        char *str = traverseArgs->str;
        int len = strlen(str);
        char node[10];
        sprintf(node, "%d_", currentNode + 1);
        for (int i = len; i < len + strlen(node); i++)
        {
            str[i] = node[i - len];
        }
        pthread_exit(NULL);
    }
    for (int i = 0; i < n; i++)
    {
        if (*(*(adjList + currentNode) + i) == -1)
            break;
        if (vis[*(*(adjList + currentNode) + i)] == 0)
        {
            traverseArgs->currentNode = *(*(adjList + currentNode) + i);
            pthread_t tid;
            pthread_create(&tid, NULL, DFShelper, (void *)traverseArgs);
            pthread_join(tid, NULL);
        }
    }
    pthread_exit(NULL);
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
        index = 0;
        char temp1;
        char temp2;
        temp1 = args->filename[1];
        temp2 = args->filename[2];
        index = (temp1 - '0') * 10 + (temp2 - '0');
    }

    // sync code
    sem_wait(semaphore_read[index]);
    readerCount[index]++;
    if (readerCount[index] == 1)
        sem_wait(semaphore_write[index]);
    sem_post(semaphore_read[index]);

    FILE *file = fopen(args->filename, "r");
    if (file == NULL)
    {
        // sem_post(semaphore_write[index]);
        return NULL;
    }

    sem_t *shmSemaphore;
    char sem_name[50];
    sprintf(sem_name, "___clientSemaphore%d___", args->sequence_number);
    shmSemaphore = sem_open(sem_name, 0, PERMS, 1);

    sem_wait(shmSemaphore);
    key_t shmkey;
    if ((shmkey = ftok("client.c", args->sequence_number)) == -1)
    {
        perror("SHM Key could not be created\n");
        return NULL;
    }

    int shmid = -1;
    int *shmptr;
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

    fscanf(file, "%d", &n);

    int adj[n][n];
    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < n; j++)
        {
            fscanf(file, "%d", &adj[i][j]);
        }
    }
    fclose(file);

    sem_post(shmSemaphore);

    printf("read complete\n");
    sem_wait(semaphore_read[index]);
    readerCount[index]--;
    if (readerCount[index] == 0){
        printf("\n\nzero reader\n\n");
        sem_post(semaphore_write[index]);
    }
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
    char response[200];
    int vis[MAX_GRAPH_NODES + 1];
    for (int i = 0; i < MAX_GRAPH_NODES + 1; i++)
    {
        vis[i] = 0;
    }
    TraverseArgs *traverseArgs = malloc(sizeof(TraverseArgs));
    traverseArgs->n = n;
    traverseArgs->currentNode = starting_node;
    traverseArgs->str = response;
    traverseArgs->vis = vis;
    traverseArgs->adjList = adjList;
    pthread_t tid;
    pthread_create(&tid, NULL, DFShelper, (void *)traverseArgs);
    pthread_join(tid, NULL);
    printf("%s %ld\n", response, strlen(response));
    //  str[len(str)] = '\0';
    // send list of vertices
    struct message buf;
    strcpy(buf.mtext, response);
    buf.sequence_number = args->unique_id;
    buf.operation_number = args->sequence_number;
    if (msgsnd(msqid, &buf, sizeof(buf.mtext), 0) == -1)
    {
        printf("Server has terminated or error in sending message");
    }

    pthread_exit(NULL);
}

void *BFShelper(void *arg)
{
    TraverseArgs *traverseArgs = (TraverseArgs *)arg;
    int n = traverseArgs->n;
    int currentNode = traverseArgs->currentNode;
    int **adjList = traverseArgs->adjList;
    int *vis = traverseArgs->vis;
    char *res = (char *)malloc(10 * sizeof(char));
    vis[currentNode] = 1;
    if (**(adjList + currentNode) == -1 || (*(*(adjList + currentNode) + 1) == -1 && vis[**(adjList + currentNode)] == 1))
    {
        printf("this is end %d\n", currentNode + 1);
        pthread_exit(NULL);
    }
    TraverseArgs *traverseArray[MAX_GRAPH_NODES + 1];
    for (int i = 0; i < MAX_GRAPH_NODES + 1; i++)
    {
        traverseArray[i] = NULL;
    }
    pthread_t tid[MAX_GRAPH_NODES + 1];
    int itr = 0;
    for (int i = 0; i < n; i++)
    {

        if (*(*(adjList + currentNode) + i) == -1)
            break;
        if (vis[*(*(adjList + currentNode) + i)] == 0)
        {
            printf("yes\n");
            sprintf(res, "%d_", *(*(adjList + currentNode) + i) + 1);
            strcat(traverseArgs->str, res);
            printf("%d %s\n", currentNode + 1, (traverseArgs->str));
            TraverseArgs *temp = (TraverseArgs *)malloc(sizeof(TraverseArgs));
            temp->currentNode = *(*(adjList + currentNode) + i);
            temp->adjList = traverseArgs->adjList;
            temp->vis = traverseArgs->vis;
            temp->n = n;
            temp->str = traverseArgs->str;
            traverseArray[itr] = temp;
            itr++;
        }
    }
    int j = 0;
    while (j < itr && traverseArray[j] != NULL)
    {
        pthread_create(&tid[j], NULL, BFShelper, (void *)traverseArray[j]);
        j++;
    }
    for (int i = 0; i < itr; i++)
    {
        if (tid[i] != -1)
        {
            printf("join_%d\n", i + 1);
            pthread_join(tid[i], NULL);
        }
    }
    pthread_exit(NULL);
}
void *BFS(void *arg)
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

    FILE *file = fopen(args->filename, "r");
    if (file == NULL)
    {
        // sem_post(semaphore_write[index]);
        return NULL;
    }
    sem_t *shmSemaphore;
    char sem_name[50];
    sprintf(sem_name, "___clientSemaphore%d___", args->sequence_number);
    shmSemaphore = sem_open(sem_name, 0, PERMS, 1);
    printf("reader");
    sem_wait(shmSemaphore);
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
    starting_node--;
    int n;
    sem_post(shmSemaphore);
    printf("shm");
    fscanf(file, "%d", &n);

    int adj[n][n];
    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < n; j++)
        {
            fscanf(file, "%d", &adj[i][j]);
        }
    }
    fclose(file);
    printf("read complete\n");
    sem_wait(semaphore_read[index]);
    readerCount[index]--;
    if (readerCount[index] == 0)
        sem_post(semaphore_write[index]);
    sem_post(semaphore_read[index]);
    printf("read2\n");
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
    char str[200];
    int vis[MAX_GRAPH_NODES + 1];
    TraverseArgs *traverseArgs = malloc(sizeof(TraverseArgs));
    traverseArgs->n = n;
    traverseArgs->currentNode = starting_node;
    traverseArgs->str = str;
    traverseArgs->vis = vis;
    traverseArgs->adjList = adjList;
    sprintf(str, "%d_", starting_node + 1);
    pthread_t tid;
    pthread_create(&tid, NULL, BFShelper, (void *)traverseArgs);
    pthread_join(tid, NULL);
    printf("BFS: %s\n", str);

    // send list of vertices
    struct message buf;
    strcpy(buf.mtext, str);
    buf.sequence_number = args->unique_id;
    buf.operation_number = args->sequence_number;
    if (msgsnd(msqid, &buf, sizeof(buf.mtext), 0) == -1)
    {
        printf("Server has terminated or error in sending message");
    }
    // free(str);
    // free(vis);
    // for(int i=0;i<MAX_GRAPH_NODES+1;i++){
    //     free(adjList[i]);
    // }
    // free(adjList);
    // free(args);
    pthread_exit(NULL);
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
    for (int i = 0; i < 21; i++)
    {
        sprintf(sem_name, "__writerSemaphore__%d__", i);
        semaphore_write[i] = sem_open(sem_name, O_CREAT, PERMS, 1); // 0x0100 means create if doesnt exist already
    }

    for (int i = 0; i < 21; i++)
    {
        sprintf(sem_name, "__readerSemaphore__%d__", i);
        semaphore_read[i] = sem_open(sem_name, O_CREAT, PERMS, 1); // 0x0100 means create if doesnt exist already
    }

    struct message buf;
    int len;

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
        args->unique_id = 1000 * sequence_number;
        pthread_attr_init(&attr[tidptr]);
        if (operation == 3)
            pthread_create(&tid[tidptr], &attr[tidptr], DFS, (void *)args);
        else if (operation == 4)
            pthread_create(&tid[tidptr], &attr[tidptr], BFS, (void *)args);
        tidptr++;
    }

    // cleanup
    if (shmdt(readerCount) == -1)
    {
        perror("Detaching error");
        return 1;
    }

    if (shmctl(reader_id, IPC_RMID, 0) == -1)
    {
        perror("SHMCTL");
        return 1;
    }
    for (int i = 0; i < tidptr; i++)
    {
        pthread_join(tid[i], NULL);
    }
    for (int i = 0; i < 21; i++)
    {
        sem_close(semaphore_write[i]);
        sem_close(semaphore_read[i]);
        sprintf(sem_name, "__writerSemaphore__%d__", i);
        sem_unlink(sem_name);
        sprintf(sem_name, "__readerSemaphore__%d__", i);
        sem_unlink(sem_name);
    }

    return 0;
}
