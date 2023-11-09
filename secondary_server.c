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
}typedef thread_arg;

void *DFS(void *arg){
    return NULL;
}

void *BFS(void *arg){
    return NULL;
}

int main(int argc, char const *argv[])
{
    struct message buf;
    int msqid; // message qid
    int len;
    key_t key;
    
    if ((key = ftok("load_balancer.c", 'W')) == -1)
    {
        perror("ftok");
        exit(1);
    }
    if ((msqid = msgget(key, PERMS | IPC_CREAT)) == -1)
    {
        /*Connect to queue*/
        printf("%d\n", msqid);
        perror("msgget");
        exit(1);
    }

    while (1)
    {
        if (msgrcv(msqid, &buf, sizeof(buf.mtext), 1001, 0) == -1)
        {
            perror("msgrcv error");
            exit(1);
        }
        if(buf.operation_number==-1) break;
        int operation = buf.operation_number%10;
        int sequence_number = buf.operation_number/10;
        thread_arg args;
        
        strcpy(args.filename,buf.mtext);
        args.sequence_number = sequence_number;

        pthread_attr_init(&attr[tidptr]);
        if(operation==3)
        pthread_create(&tid[tidptr],&attr[tidptr],DFS,(void *)args);
        else if(operation==4)
        pthread_create(&tid[tidptr],&attr[tidptr],BFS,(void *)args);
        tidptr++;
             
        



    }

    //cleanup
    
    return 0;
}
