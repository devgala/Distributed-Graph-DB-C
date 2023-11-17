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
#define QUEUE_MAX_SIZE 200
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

struct Queue
{
    int array[QUEUE_MAX_SIZE];
    int front, rear;
} typedef Queue;

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
    Queue *queue;
    pthread_mutex_t *lock;
} typedef TraverseArgs;

TraverseArgs *initializeTraverseArgs(int currentNode, int *vis, int **adjList, char *str, int n, Queue *queue, pthread_mutex_t *lock)
{
    TraverseArgs *traverseArgs = malloc(sizeof(TraverseArgs));
    if (!traverseArgs)
    {
        printf("Memory allocation error");
        return NULL;
    }
    traverseArgs->adjList = adjList;
    traverseArgs->currentNode = currentNode;
    traverseArgs->n = n;
    traverseArgs->vis = vis;
    traverseArgs->queue = queue;
    traverseArgs->lock = lock;
    traverseArgs->str = str;
    return traverseArgs;
}

struct Queue *getQueue()
{
    struct Queue *queue = (struct Queue *)malloc(sizeof(struct Queue));
    if (!queue)
    {
        perror("Memory allocation error");
        exit(EXIT_FAILURE);
    }
    queue->front = queue->rear = -1;
    return queue;
}

int queueIsEmpty(struct Queue *queue)
{
    return (queue->front == -1);
}

int queueIsFull(struct Queue *queue)
{
    return (queue->rear == QUEUE_MAX_SIZE - 1);
}

void push(struct Queue *queue, int data)
{
    // Check if the queue is full
    if (queueIsFull(queue))
    {
        return;
    }

    // If the queue is empty, set the front to 0
    if (queueIsEmpty(queue))
    {
        queue->front = 0;
    }

    // Increment the rear and add the new element
    queue->array[++queue->rear] = data;
}

int pop_front(struct Queue *queue)
{
    // Check if the queue is empty
    if (queueIsEmpty(queue))
    {
        return -1;
    }

    // Get the data from the front and increment the front pointer
    int data = queue->array[queue->front++];

    // If the front surpasses the rear, reset the front and rear to -1
    if (queue->front > queue->rear)
    {
        queue->front = queue->rear = -1;
    }

    return data;
}

int queueSize(struct Queue *queue)
{
    if (queueIsEmpty(queue))
    {
        return 0;
    }
    else
    {
        return queue->rear - queue->front + 1;
    }
}

void *DFShelper(void *arg)
{
    // extract argument to thread
    TraverseArgs *traverseArgs = (TraverseArgs *)arg;
    int n = traverseArgs->n;
    int currentNode = traverseArgs->currentNode;
    int **adjList = traverseArgs->adjList;
    int *vis = traverseArgs->vis;
    vis[currentNode] = 1;

    // base case condition
    if (**(adjList + currentNode) == -1 || (*(*(adjList + currentNode) + 1) == -1 && vis[**(adjList + currentNode)] == 1))
    {
        // add to string
        char *str = traverseArgs->str;
        int len = strlen(str);
        char node[10];
        sprintf(node, "%d ", currentNode + 1);
        pthread_mutex_lock(traverseArgs->lock);
        for (int i = len; i < len + strlen(node); i++)
        {
            str[i] = node[i - len];
        }
        pthread_mutex_unlock(traverseArgs->lock);
        pthread_exit(NULL);
    }
    pthread_t threads[MAX_GRAPH_NODES + 1];
    int itr = 0;
    for (int i = 0; i < n; i++)
    {
        if (*(*(adjList + currentNode) + i) == -1)
            break;
        if (vis[*(*(adjList + currentNode) + i)] == 0)
        {

            // start threads on unvisited neighbours
            TraverseArgs *temp = initializeTraverseArgs(*(*(adjList + currentNode) + i), vis, adjList, traverseArgs->str, n, NULL, traverseArgs->lock);
            int res = pthread_create(&threads[itr], NULL, DFShelper, (void *)temp);
            if (res != 0)
            {
                printf("Error occured in thread creation");
                return NULL;
            }
            itr++;
        }
    }
    for (int i = 0; i < itr; i++)
    {
        // wait for all neighbours to end
        pthread_join(threads[i], NULL);
    }
    pthread_exit(NULL);
}

void *DFS(void *arg)
{

    // extract file name and get index
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

    // synchrnization using named semaphore
    sem_wait(semaphore_read[index]);
    readerCount[index]++;
    if (readerCount[index] == 1)
        sem_wait(semaphore_write[index]);
    sem_post(semaphore_read[index]);

    FILE *file = fopen(args->filename, "r");
    if (file == NULL)
    {
        sem_post(semaphore_write[index]);
        return NULL;
    }

    // get starting node from Shared Memory
    sem_t *shmSemaphore;
    char sem_name[50];
    sprintf(sem_name, "___clientSemaphore%d___", args->sequence_number);
    shmSemaphore = sem_open(sem_name, 0, PERMS, 1);
    if (shmSemaphore == SEM_FAILED)
    {
        printf("Error occured in semaphore creation");
        return NULL;
    }
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

    sem_wait(semaphore_read[index]);
    readerCount[index]--;
    if (readerCount[index] == 0)
    {
        sem_post(semaphore_write[index]);
    }
    sem_post(semaphore_read[index]);

    // create adjecency list
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

    char *response = (char *)malloc(200 * sizeof(char));
    int *vis = (int *)calloc((MAX_GRAPH_NODES + 1), sizeof(int));
    for (int i = 0; i < MAX_GRAPH_NODES + 1; i++)
    {
        vis[i] = 0;
    }
    pthread_mutex_t lock;
    pthread_mutex_init(&lock,NULL);
    TraverseArgs *traverseArgs = initializeTraverseArgs(starting_node, vis, adjList, response, n, NULL, &lock);

    // start DFS
    pthread_t tid;
    int res = pthread_create(&tid, NULL, DFShelper, (void *)traverseArgs);
    if (res != 0)
    {
        printf("Error occured in thread creation");
        return NULL;
    }
    pthread_join(tid, NULL);

    // send result of DFS to client at 1000*sequence_number
    struct message buf;
    strcpy(buf.mtext, response);
    buf.sequence_number = args->unique_id;
    buf.operation_number = args->sequence_number;
    if (msgsnd(msqid, &buf, sizeof(buf.mtext), 0) == -1)
    {
        printf("Server has terminated or error in sending message");
        return NULL;
    }
    printf("Response sent to %d\n\n", args->sequence_number);
    pthread_mutex_destroy(&lock);
    pthread_exit(NULL);
}

void *BFShelper(void *arg)
{
    // extract argument to thread
    TraverseArgs *traverseArgs = (TraverseArgs *)arg;
    int n = traverseArgs->n;
    int currentNode = traverseArgs->currentNode;
    int **adjList = traverseArgs->adjList;
    int *vis = traverseArgs->vis;
    Queue *queue = traverseArgs->queue;
    int i = 0;

    // add neighbours to queue
    while (*(*(adjList + currentNode) + i) != -1)
    {
        if (vis[*(*(adjList + currentNode) + i)] == 0)
        {
            // using mutex to synchronize queue operation
            pthread_mutex_lock(traverseArgs->lock);
            push(queue, *(*(adjList + currentNode) + i));
            pthread_mutex_unlock(traverseArgs->lock);
        }
        i++;
    }
    pthread_exit(NULL);
}

void *BFS(void *arg)
{

    // get arguments and index
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

    // synchronization using named semaphore
    sem_wait(semaphore_read[index]);
    readerCount[index]++;
    if (readerCount[index] == 1)
        sem_wait(semaphore_write[index]);
    sem_post(semaphore_read[index]);

    FILE *file = fopen(args->filename, "r");
    if (file == NULL)
    {
        sem_post(semaphore_write[index]);
        return NULL;
    }

    // getting starting node from shared memory
    sem_t *shmSemaphore;
    char sem_name[50];
    sprintf(sem_name, "___clientSemaphore%d___", args->sequence_number);
    shmSemaphore = sem_open(sem_name, 0, PERMS, 1);
    if (shmSemaphore == SEM_FAILED)
    {
        printf("Error occured in semaphore creation");
        return NULL;
    }
    sem_wait(shmSemaphore);
    key_t shmkey;
    if ((shmkey = ftok("client.c", args->sequence_number)) == -1)
    {
        perror("SHM Key could not be created\n");
        sem_post(semaphore_write[index]);
        return NULL;
    }

    int shmid = -1;
    int *shmptr;
    int temp = 100000;
    shmid = shmget(shmkey, sizeof(int), PERMS);
    if (shmid == -1)
    {
        perror("Error occured in creating SHM segment");
        sem_post(semaphore_write[index]);
        return NULL;
    }
    shmptr = shmat(shmid, 0, 0);

    if (shmptr == (void *)-1)
    {
        perror("shmat");
        sem_post(semaphore_write[index]);
        return NULL;
    }
    int starting_node = shmptr[0];
    starting_node--;
    int n;
    sem_post(shmSemaphore);
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
    sem_wait(semaphore_read[index]);
    readerCount[index]--;
    if (readerCount[index] == 0)
        sem_post(semaphore_write[index]);
    sem_post(semaphore_read[index]);

    // making adjecency list
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

    //  BFS
    char *breadth_first_traversal = (char *)calloc(200, sizeof(char));
    int *vis = (int *)calloc(MAX_GRAPH_NODES + 1, sizeof(int));
    char res[10];
    Queue *queue = getQueue();
    pthread_mutex_t lock;
    pthread_mutex_init(&lock, NULL);
    pthread_t threads[MAX_GRAPH_NODES + 1];
    int itr = 0;
    push(queue, starting_node);
    while (!queueIsEmpty(queue))
    {
        itr = 0;
        int size = queueSize(queue);
        for (int i = 0; i < size; i++)
        {

            int node = pop_front(queue);
            vis[node] = 1;
            sprintf(res, "%d ", node + 1);
            strcat(breadth_first_traversal, res);
            TraverseArgs *temp = initializeTraverseArgs(node, vis, adjList, NULL, n, queue, &lock);
            int res = pthread_create(&threads[itr], NULL, BFShelper, (void *)temp);
            if (res != 0)
            {
                printf("Error occured in thread creation");
                return NULL;
            }
            itr++;
        }
        for (int i = 0; i < itr; i++)
        {
            pthread_join(threads[i], NULL);
        }
    }
    pthread_mutex_destroy(&lock);
    // send list of vertices
    struct message buf;
    strcpy(buf.mtext, breadth_first_traversal);
    buf.sequence_number = args->unique_id;
    buf.operation_number = args->sequence_number;
    if (msgsnd(msqid, &buf, sizeof(buf.mtext), 0) == -1)
    {
        printf("Server has terminated or error in sending message");
    }
    printf("Response sent to %d\n\n", args->sequence_number);
    pthread_exit(NULL);
}

int main(int argc, char const *argv[])
{
    char sem_name[50];
    for (int i = 0; i < 21; i++)
    {
        sprintf(sem_name, "__writerSemaphore__%d__", i);
        sem_unlink(sem_name);
        sprintf(sem_name, "__readerSemaphore__%d__", i);
        sem_unlink(sem_name);
    }
    // connect to SHM for reader count between processes
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

    for (int i = 0; i < 21; i++)
    {
        sprintf(sem_name, "__writerSemaphore__%d__", i);
        semaphore_write[i] = sem_open(sem_name, O_CREAT, PERMS, 1); // 0x0100 means create if doesnt exist already
        if (semaphore_write[i] == SEM_FAILED)
        {
            printf("Error occured in semaphore creation");
            return 1;
        }
    }

    for (int i = 0; i < 21; i++)
    {
        sprintf(sem_name, "__readerSemaphore__%d__", i);
        semaphore_read[i] = sem_open(sem_name, O_CREAT, PERMS, 1); // 0x0100 means create if doesnt exist already
        if (semaphore_read[i] == SEM_FAILED)
        {
            printf("Error occured in semaphore creation");
            return 1;
        }
    }

    struct message buf;
    int len;

    // decide how to make it odd even
    int parity;
    printf("If server is ODD then enter 1\nIf server is EVEN enter 2:\n");
    scanf("%d", &parity);
    int recieveChannel = (parity == 1) ? SECONDARY_SERVER_1_CODE : SECONDARY_SERVER_2_CODE;

    if (parity == 1)
    {
        printf("This is ODD server\n\n");
    }
    else
        printf("This is EVEN server\n\n");
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
        // recieve messages from queue
        if (msgrcv(msqid, &buf, sizeof(buf.mtext), recieveChannel, 0) == -1)
        {
            perror("msgrcv error");
            exit(1);
        }
        // CLEANUP condition
        if (buf.operation_number == -1)
            break;

        int operation = buf.operation_number % 10;
        int sequence_number = buf.operation_number / 10;
        thread_arg *args = malloc(sizeof(thread_arg));
        strcpy(args->filename, buf.mtext);
        args->sequence_number = sequence_number;
        args->unique_id = 1000 * sequence_number;
        pthread_attr_init(&attr[tidptr]);
        int res;
        if (operation == 3)
            res = pthread_create(&tid[tidptr], &attr[tidptr], DFS, (void *)args);
        else if (operation == 4)
            res = pthread_create(&tid[tidptr], &attr[tidptr], BFS, (void *)args);
        if (res != 0)
        {
            printf("Error occured in thread creation");
            continue;
        }
        tidptr++;
    }

    // cleanup
    printf("CLEANUP\n\n");
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
        if (sem_close(semaphore_write[i]) == -1)
        {
            printf("Error in closing semaphore_write %d", i);
        }
        if (sem_close(semaphore_read[i]) == -1)
        {
            printf("Error in closing semaphore_read %d", i);
        }
        sprintf(sem_name, "__writerSemaphore__%d__", i);
        if (sem_unlink(sem_name) == -1)
        {
            printf("Error occured in unlinking %s", sem_name);
        }
        sprintf(sem_name, "__readerSemaphore__%d__", i);
        if (sem_unlink(sem_name) == -1)
        {
            printf("Error occured in unlinking %s", sem_name);
        }
    }

    return 0;
}
