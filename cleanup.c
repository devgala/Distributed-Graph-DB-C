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

struct message
{
    long sequence_number;
    int operation_number;
    char mtext[200];
};

int main(int argc, char const *argv[])
{
    char c = 'N';
    while (c == 'N')
    {
        printf("Want to terminate the application? Press Y (Yes) or N (No)\n");
        scanf("%c", &c);
    }

    struct message buf;
    int msqid; // message qid
    int len;
    key_t key;
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
    sprintf(buf.mtext, "CLEANUP");
    buf.sequence_number = 101;
    buf.operation_number = 9;
    if (msgsnd(msqid, &buf, sizeof(buf.mtext), 0) == -1)
    {
        printf("Server has terminated or error in sending message");
        exit(1);
    }

    return 0;
}
