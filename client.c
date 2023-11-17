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
#include <semaphore.h>
#include <fcntl.h>

#define PERMS 0644 // file permissions
#define MAX_GRAPH_NODES 30
struct message
{
    long sequence_number;
    int operation_number;
    char mtext[200];
};

void displayMenu()
{
    printf("1.Add a new graph to the database\n");
    printf("2. Modify an existing graph of the database\n");
    printf("3. Perform DFS on an existing graph of the database\n");
    printf("4. Perform BFS on an existing graph of the database\n");
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
    if ((msqid = msgget(key, PERMS)) == -1)
    {
        /*Connect to queue*/
        printf("%d\n", msqid);
        perror("msgget");
        exit(1);
    }
    while (1)
    {
        displayMenu();
        int sequence_number, operaton_number;
        printf("Enter Sequence Number: \n");
        scanf("%d", &sequence_number);
        printf("Enter Operation Number: \n");
        scanf("%d", &operaton_number);
        printf("Enter filename\n");
        getchar();
        if (fgets(buf.mtext, sizeof(buf.mtext), stdin) == NULL)
        {
            printf("Error Occured in reading file name.\n");
            continue;
        }
        len = strlen(buf.mtext);
        // len = sprintf(buf.mtext,"this is an issue");
        printf("%s\n", buf.mtext);
        if (buf.mtext[len - 1] == '\n')
            buf.mtext[len - 1] = '\0';
        // send request to Load balancer
        buf.sequence_number = sequence_number;
        buf.operation_number = operaton_number;
        if (msgsnd(msqid, &buf, sizeof(buf.mtext), 0) == -1)
        {
            printf("Server has terminated or error in sending message");
            exit(1);
        }

        // request specific tasks
        if (operaton_number == 1)
        {
            // new graph
            sem_t *shmSemaphore;
            char sem_name[50];
            sprintf(sem_name, "___clientSemaphore%d___", sequence_number);
            shmSemaphore = sem_open(sem_name, O_CREAT, PERMS, 1);
            // take input for graph
            sem_wait(shmSemaphore);
            key_t shmkey;
            if ((shmkey = ftok("client.c", sequence_number)) == -1)
            {
                perror("SHM Key could not be created\n");
                continue;
            }

            int shmid;
            int *shmptr;
            shmid = shmget(shmkey, sizeof(int[MAX_GRAPH_NODES + 1][MAX_GRAPH_NODES + 1]), IPC_CREAT | PERMS);
            if (shmid == -1)
            {
                perror("Error occured in creating SHM segment");
                continue;
            }

            shmptr = (int *)shmat(shmid, NULL, 0);

            if (shmptr == (void *)-1)
            {
                perror("shmat");
                continue;
            }
            int n;
            printf("Enter number of nodes of the graph\n");
            scanf("%d", &n);
            printf("Enter adjacency matrix, each row on a separate line and elements of a single row separated by whitespace characters\n");
            int adj[n][n];
            for (int i = 0; i < n; i++)
            {
                for (int j = 0; j < n; j++)
                {
                    scanf("%d", &adj[i][j]);
                }
            }
            shmptr[0] = n;
            for (int i = 1; i <= n; i++)
            {
                if (shmptr == (void *)-1)
                {
                    perror("shmat");
                    continue;
                }

                for (int j = 1; j <= n; j++)
                {
                    shmptr[i * n + j] = adj[i - 1][j - 1];
                }
            }

            for (int i = 1; i <= n; i++)
            {
                for (int j = 1; j <= n; j++)
                {
                    printf("%d ", shmptr[i * n + j]);
                }
            }
            sem_post(shmSemaphore);
            // process response
            if (msgrcv(msqid, &buf, sizeof(buf.mtext), 1000 * sequence_number, 0) == -1)
            {
                perror("msgrcv error");
                exit(1);
            }
            printf("Response from server: \n%s\n\n", buf.mtext);
            // delete shared memory segment after recieving response

            if (shmdt(shmptr) == -1)
            {
                perror("Detaching error");
                break;
            }

            if (shmctl(shmid, IPC_RMID, 0) == -1)
            {
                perror("SHMCTL");
                return 1;
            }
            sem_close(shmSemaphore);
            sprintf(sem_name, "___clientSemaphore%d___", sequence_number);
            sem_unlink(sem_name);
        }
        else if (operaton_number == 2)
        {
            // update graph
            sem_t *shmSemaphore;
            char sem_name[50];
            sprintf(sem_name, "___clientSemaphore%d___", sequence_number);
            shmSemaphore = sem_open(sem_name, O_CREAT, PERMS, 1);
            sem_wait(shmSemaphore);
            key_t shmkey;
            if ((shmkey = ftok("client.c", sequence_number)) == -1)
            {
                perror("SHM Key could not be created\n");
                continue;
            }

            int shmid;
            int *shmptr;
            shmid = shmget(shmkey, sizeof(int[MAX_GRAPH_NODES + 1][MAX_GRAPH_NODES + 1]), IPC_CREAT | PERMS);
            if (shmid == -1)
            {
                perror("Error occured in creating SHM segment");
                continue;
            }

            shmptr = (int *)shmat(shmid, NULL, 0);

            if (shmptr == (void *)-1)
            {
                perror("shmat");
                continue;
            }
            int n;
            printf("Enter number of nodes of the graph\n");
            scanf("%d", &n);
            printf("Enter adjacency matrix, each row on a separate line and elements of a single row separated by whitespace characters\n");
            int adj[n][n];
            for (int i = 0; i < n; i++)
            {
                for (int j = 0; j < n; j++)
                {
                    scanf("%d", &adj[i][j]);
                }
            }

            shmptr[0] = n;
            for (int i = 1; i <= n; i++)
            {
                if (shmptr == (void *)-1)
                {
                    perror("shmat");
                    continue;
                }

                for (int j = 1; j <= n; j++)
                {
                    shmptr[i * n + j] = adj[i - 1][j - 1];
                }
            }

            for (int i = 1; i <= n; i++)
            {
                for (int j = 1; j <= n; j++)
                {
                    printf("%d ", shmptr[i * n + j]);
                }
            }
            sem_post(shmSemaphore);
            // process response

            if (msgrcv(msqid, &buf, sizeof(buf.mtext), 1000 * sequence_number, 0) == -1)
            {
                perror("msgrcv error");
                exit(1);
            }
            printf("Response from server: \n%s\n\n", buf.mtext);
            // delete shared memory segment after recieving response

            if (shmdt(shmptr) == -1)
            {
                perror("Detaching error");
                break;
            }

            if (shmctl(shmid, IPC_RMID, 0) == -1)
            {
                perror("SHMCTL");
                continue;
            }
            sem_close(shmSemaphore);
            sprintf(sem_name, "___clientSemaphore%d___", sequence_number);
            sem_unlink(sem_name);
        }
        else if (operaton_number == 3)
        {
            // DFS
            sem_t *shmSemaphore;
            char sem_name[50];
            sprintf(sem_name, "___clientSemaphore%d___", sequence_number);
            shmSemaphore = sem_open(sem_name, O_CREAT, PERMS, 1);
            sem_wait(shmSemaphore);
            key_t shmkey;
            if ((shmkey = ftok("client.c", sequence_number)) == -1)
            {
                perror("SHM Key could not be created\n");
                continue;
            }

            int shmid;
            int *shmptr;
            shmid = shmget(shmkey, sizeof(int), IPC_CREAT | PERMS);
            if (shmid == -1)
            {
                perror("Error occured in creating SHM segment");
                continue;
            }

            shmptr = (int *)shmat(shmid, NULL, 0);

            if (shmptr == (void *)-1)
            {
                perror("shmat");
                continue;
            }
            int starting_node;
            printf("Enter starting node\n");
            scanf("%d", &starting_node);
            shmptr[0] = starting_node;

            sem_post(shmSemaphore);
            // response from server
            if (msgrcv(msqid, &buf, sizeof(buf.mtext), 1000 * sequence_number, 0) == -1)
            {
                perror("msgrcv error");
                exit(1);
            }

            printf("Response from server: \n%s\n\n", buf.mtext);

            if (shmdt(shmptr) == -1)
            {
                perror("Detaching error");
                break;
            }

            if (shmctl(shmid, IPC_RMID, 0) == -1)
            {
                perror("SHMCTL");
                continue;
            }

            sem_close(shmSemaphore);
            sprintf(sem_name, "___clientSemaphore%d___", sequence_number);
            sem_unlink(sem_name);
        }
        else if (operaton_number == 4)
        {
            // BFS
            sem_t *shmSemaphore;
            char sem_name[50];
            sprintf(sem_name, "___clientSemaphore%d___", sequence_number);
            shmSemaphore = sem_open(sem_name, O_CREAT, PERMS, 1);
            sem_wait(shmSemaphore);
            key_t shmkey;
            if ((shmkey = ftok("client.c", sequence_number)) == -1)
            {
                perror("SHM Key could not be created\n");
                continue;
            }

            int shmid;
            int *shmptr;
            shmid = shmget(shmkey, sizeof(int), IPC_CREAT | PERMS);
            if (shmid == -1)
            {
                perror("Error occured in creating SHM segment");
                continue;
            }

            shmptr = (int *)shmat(shmid, NULL, 0);

            if (shmptr == (void *)-1)
            {
                perror("shmat");
                continue;
            }
            int starting_node;
            printf("Enter starting node\n");
            scanf("%d", &starting_node);
            shmptr[0] = starting_node;

            sem_post(shmSemaphore);
            if (msgrcv(msqid, &buf, sizeof(buf.mtext), 1000 * sequence_number, 0) == -1)
            {
                perror("msgrcv error");
                exit(1);
            }
            printf("Response from server: \n%s\n\n", buf.mtext);

            if (shmdt(shmptr) == -1)
            {
                perror("Detaching error");
                break;
            }

            if (shmctl(shmid, IPC_RMID, 0) == -1)
            {
                perror("SHMCTL");
                continue;
            }
            sem_close(shmSemaphore);
            sprintf(sem_name, "___clientSemaphore%d___", sequence_number);
            sem_unlink(sem_name);
        }
        else
            continue;
    }

    return 0;
}
