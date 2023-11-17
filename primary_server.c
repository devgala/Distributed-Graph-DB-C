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
#include <string.h>
#include <fcntl.h>

#define PERMS 0644 // file permissions
#define MAX_GRAPH_NODES 30
#define PRIMARY_SEVER_CODE 1003
#define SECONDARY_SERVER_2_CODE 1002
#define SECONDARY_SERVER_1_CODE 1001
#define CLEANUP_CODE -1
#define MAX_GRAPH_FILES 20

int tidptr = 0;
pthread_t tid[10000];
pthread_attr_t attr[10000];
sem_t *semaphore_write[21];
int msqid;
key_t key;

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
    int option;
} typedef thread_arg;

void *writeToGraphDB(void *arg)
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
    sem_wait(semaphore_write[index]);

    /*open for reading and writing.
   If file exists deletes content and overwrites the file,
    otherwise creates an empty new file*/
    FILE *file = fopen(args->filename, "w+");

    if (file == NULL)
    {
        sem_post(semaphore_write[index]);
        return NULL;
    }
    // sleep(20);

    // connct to shared memory of client
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

    int shmid;
    int *shmptr;
    int temp = 100000;
    shmid = shmget(shmkey, sizeof(int[MAX_GRAPH_NODES + 1][MAX_GRAPH_NODES + 1]), PERMS);
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
    

    int nodes = shmptr[0];

    int adj[nodes][nodes];
    for (int i = 1; i <= nodes; i++)
    {

        for (int j = 1; j <= nodes; j++)
        {
            adj[i - 1][j - 1] = shmptr[i * nodes + j];
        }
    }

    sem_post(shmSemaphore);
    fprintf(file, "%d", nodes);
    fprintf(file, "\n");
    for (int i = 0; i < nodes; i++)
    {
        for (int j = 0; j < nodes; j++)
        {

            fprintf(file, "%d ", adj[i][j]);
        }
        fprintf(file, "\n");
    }
    fclose(file);

    if (shmdt(shmptr) == -1)
    {
        perror("Detaching error");
        return NULL;
    }

    // response
    struct message buf;
    buf.sequence_number = 1000 * (args->sequence_number);
    if (args->option == 1)
        sprintf(buf.mtext, "File Successfully added");
    else
        sprintf(buf.mtext, "File Successfully modified");
    if (msgsnd(msqid, &buf, sizeof(buf.mtext), 0) == -1)
    {
        printf("Server has terminated or error in sending message");
        exit(1);
    }

    sem_post(semaphore_write[index]);
    sem_close(shmSemaphore);
    sprintf(sem_name, "___clientSemaphore%d___", args->sequence_number);
    sem_unlink(sem_name);
    
    printf("Response sent to %d\n",args->sequence_number);
    return NULL;
}
int main(int argc, char const *argv[])
{
    char sem_name[50];
    struct message buf;
    for (int i = 0; i < 21; i++)
    {
        sprintf(sem_name, "__writerSemaphore__%d__", i);
        sem_unlink(sem_name); // 0x0100 means create if doesnt exist already
    }
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

    while (1)
    {
        if (msgrcv(msqid, &buf, sizeof(buf.mtext), PRIMARY_SEVER_CODE, 0) == -1)
        {
            perror("msgrcv error");
            exit(1);
        }
        

        if (buf.operation_number == -1) break;

        int operation = buf.operation_number % 10;
        int sequence_number = buf.operation_number / 10;

        printf("(Sequence Number, Operation) : (%d %d)\n",sequence_number,operation);
        thread_arg *args = malloc(sizeof(thread_arg));
        thread_arg temp;
        strcpy(args->filename, buf.mtext);
        args->sequence_number = sequence_number;
        args->option = operation;
        pthread_attr_init(&attr[tidptr]);
        int res = pthread_create(&tid[tidptr], &attr[tidptr], writeToGraphDB, (void *)args);
        if(res!=0){
            printf("Error occured in thread creation");
            continue;
        }
        tidptr++;
    }

    // cleanup
    printf("CLEANUP\n\n");
    for(int i=0;i<tidptr;i++){
        pthread_join(tid[i],NULL);
    }
    for (int i = 0; i < 21; i++)
    {
         if (sem_close(semaphore_write[i]) == -1)
        {
            printf("Error in closing semaphore_write %d", i);
        }
        sprintf(sem_name, "__writerSemaphore__%d__", i);
         if (sem_unlink(sem_name) == -1)
        {
            printf("Error occured in unlinking %s", sem_name);
        }
    }
    
    return 0;
}
