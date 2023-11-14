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

#define PERMS 0644 // file permissions
#define MAX_GRAPH_NODES 30
#define PRIMARY_SEVER_CODE 1003
#define SECONDARY_SERVER_2_CODE 1002
#define SECONDARY_SERVER_1_CODE 1001
#define CLEANUP_CODE -1

struct message
{
    long sequence_number;
    int operation_number;
    char mtext[200];
};
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
        //  break;
        if (msgrcv(msqid, &buf, sizeof(buf.mtext), -101, 0) == -1)
        {
            perror("msgrcv error");
            exit(1);
        }
        printf("%ld\n",buf.sequence_number);
        printf("%d\n",buf.operation_number);
        printf("%s\n",buf.mtext);
        int operation = buf.operation_number;
        int sequence_number = buf.sequence_number;
        if (sequence_number == PRIMARY_SEVER_CODE || sequence_number == SECONDARY_SERVER_1_CODE || sequence_number == SECONDARY_SERVER_2_CODE)
        {
            // send back to queue

            if (msgsnd(msqid, &buf, sizeof(buf.mtext), 0) == -1)
            {
                printf("Server has terminated or error in sending message");
                exit(1);
            }

            // int temp = 10000;
            // while(temp--); //can you do this?
        }
        else if (operation == 1 || operation == 2)
        {
            // primary server
            buf.operation_number = operation + 10 * sequence_number;
            buf.sequence_number = PRIMARY_SEVER_CODE;
            if (msgsnd(msqid, &buf, sizeof(buf.mtext), 0) == -1)
            {
                printf("Server has terminated or error in sending message");
                exit(1);
            }
        }
        else if (operation == 4 || operation == 3)
        {
            // secondary servers
            buf.operation_number = operation + 10 * sequence_number;
            if (sequence_number % 2 == 0)
                buf.sequence_number = SECONDARY_SERVER_2_CODE;
            else
                buf.sequence_number = SECONDARY_SERVER_1_CODE;
            if (msgsnd(msqid, &buf, sizeof(buf.mtext), 0) == -1)
            {
                printf("Server has terminated or error in sending message");
                exit(1);
            }
        }
        else if (operation == 9)
        {
            // cleanup
            printf("CLEANUP");
            buf.operation_number = CLEANUP_CODE;
            sprintf(buf.mtext,"CLEANUP");

            buf.sequence_number = SECONDARY_SERVER_1_CODE;
            if (msgsnd(msqid, &buf, sizeof(buf.mtext), 0) == -1)
            {
                printf("Server has terminated or error in sending message");
                exit(1);
            }

            buf.sequence_number = SECONDARY_SERVER_2_CODE;
            if (msgsnd(msqid, &buf, sizeof(buf.mtext), 0) == -1)
            {
                printf("Server has terminated or error in sending message");
                exit(1);
            }

            buf.sequence_number = PRIMARY_SEVER_CODE;
            if (msgsnd(msqid, &buf, sizeof(buf.mtext), 0) == -1)
            {
                printf("Server has terminated or error in sending message");
                exit(1);
            }
            sleep(5);
            break;
        }
    }

    // delete queue
    if (msgctl(msqid, IPC_RMID, NULL) == -1)
    {
        perror("mdgctl");
        exit(1);
    }

    return 0;
}

//  if (msgrcv(msqid, &buf, sizeof(buf.mtext), 0, 0) == -1)
//         {
//             perror("msgrcv");
//             exit(1);
//         }

//         printf("%s",buf.mtext);
//         if(msgctl(msqid,IPC_RMID,NULL)==-1){
//        perror("mdgctl");
//        exit(1);
//    }