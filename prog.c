#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <mqueue.h>

#define LIFE_SPAN 10
#define MAX_NUM 10
#define MAX_PID_LENGTH 10
#define MAXMSG 10
#define MAX_MSG_LENGTH 20
#define NEIGHBOURS_LIMIT 5


#define ERR(source) (fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
                     perror(source),kill(0,SIGKILL),\
		     		     exit(EXIT_FAILURE))

char **neighbours;
int neighboursSize = 0;

void prepend(char* s, const char* t)
{
    size_t len = strlen(t);
    size_t i;

    memmove(s + len, s, strlen(s) + 1);

    for (i = 0; i < len; ++i)
    {
        s[i] = t[i];
    }
}

void closeQueue(mqd_t *queue, char *queueId) {
	if (mq_close(*queue)) {
		ERR("Error closing queue");
	}

	char *tempName = (char*) malloc(sizeof(char) * strlen(queueId));
	strcpy(tempName, queueId);
	prepend(tempName, "/");

	if (mq_unlink(tempName))
		ERR("Error unlinkin queue");
}

void initializeQueue(mqd_t *queue, char *queueId) {
	struct mq_attr attr;
	attr.mq_maxmsg = 10;
	attr.mq_msgsize = sizeof(pid_t) + sizeof(char) * MAX_MSG_LENGTH;

	char *tempName = (char*) malloc(sizeof(char) * strlen(queueId));
	strcpy(tempName, queueId);
	prepend(tempName, "/");

	*queue = TEMP_FAILURE_RETRY(mq_open(
		tempName,
		O_RDWR | O_NONBLOCK | O_CREAT,
		0600, &attr
		));

	free(tempName);
	if (*queue == (mqd_t)-1) {
		ERR("Error opening queue");
	}
}

void intHandler(int dummy) {
	int i;
	for (i = 0; i < neighboursSize; i++) {
		mqd_t queue;
		initializeQueue(&queue, neighbours[i]);

		printf("\nSending terminate singal to [%s]", neighbours[i]);
		long pid = strtol(neighbours[i], NULL, 10);
		if (kill(pid, SIGINT)) {
			ERR("Error sending kill to [%s] process\n", neighbours[i]);
		}

		closeQueue(&queue, neighbours[i]);
	}
	printf("\nTerminating...\n");
	exit(EXIT_SUCCESS);
}


void sethandler( void (*f)(int, siginfo_t*, void*), int sigNo) {
	struct sigaction act;
	memset(&act, 0, sizeof(struct sigaction));
	act.sa_sigaction = f;
	act.sa_flags=SA_SIGINFO;
	if (-1==sigaction(sigNo, &act, NULL)) ERR("sigaction");
}

int countDigits(long number) {
	int count = 0;

	while(number != 0)
	{
		number /= 10;
		++count;
	}

    return count;
}

void parseNeighbourPidFromArgs(char **neighbourPid, char **argv) {
	if (argv[1] != NULL && strlen(argv[1]) > 0) {
		*neighbourPid = (char*) malloc(sizeof(char) * strlen(argv[1]));
		strcpy(*neighbourPid, argv[1]);
	}
}

void createCurrentProcessPidString(char **myPid) {
	long pid = (long) getpid();
	*myPid = (char*) malloc(sizeof(char) * countDigits(pid));
	sprintf(*myPid, "%ld", pid);
}

void addNeighbour(char *neighbourPid) {
	neighboursSize++;
	if (neighbours == NULL) {
		neighbours = (char**) malloc(sizeof(char*) * neighboursSize);
	} else {	
		neighbours = (char**) realloc(neighbours, sizeof(char*) * neighboursSize);
	}
	
	neighbours[neighboursSize - 1] = (char*) malloc(sizeof(char) * strlen(neighbourPid));
	strcpy(neighbours[neighboursSize-1], neighbourPid);
}

void printNeighbours() {
	printf("My neighbours:\n");
	int i;
	for (i = 0; i < neighboursSize; i++) {
		printf("%d. [%s]\n",i + 1, neighbours[i]);
	}
	printf("\n");
}

void subscribeToMessageQueue(mqd_t *queue) {
	static struct sigevent notification;
	notification.sigev_notify = SIGEV_SIGNAL;
	notification.sigev_signo = SIGRTMIN;
	notification.sigev_value.sival_ptr=queue;
	if(mq_notify(*queue, &notification) < 0) {
		ERR("mq_notify");
	}
}

void mq_handler(int sig, siginfo_t *info, void *p) {
	mqd_t *pin;
	char clientPid[MAX_MSG_LENGTH];
	unsigned msg_prio;

	pin = (mqd_t *)info->si_value.sival_ptr;
	subscribeToMessageQueue(pin);

	for(;;){
		int bytesRead;
		if((bytesRead = mq_receive(*pin, clientPid, sizeof(pid_t) + sizeof(char) * MAX_MSG_LENGTH, NULL))<1) {
			if(errno==EAGAIN) break;
			else ERR("mq_receive");
		}
		else {
			if (neighboursSize >= NEIGHBOURS_LIMIT) {
				printf("Maximal number of clients reached!\n");
				return;
			}
			clientPid[bytesRead] = '\0';
			addNeighbour(clientPid);
			printNeighbours();			
		}
	}
}

int main(int argc, char** argv) {
	
	char *neighbourPid = NULL;
	char *myPid;
	
	parseNeighbourPidFromArgs(&neighbourPid, argv);
	createCurrentProcessPidString(&myPid);

	printf("PID: [%s]\n\n", myPid);

	mqd_t queue;
	initializeQueue(&queue, myPid);

	signal(SIGINT, intHandler);
	sethandler(mq_handler,SIGRTMIN);
	subscribeToMessageQueue(&queue);

	mqd_t neighbourQueue;
	if (neighbourPid != NULL) {
		initializeQueue(&neighbourQueue, neighbourPid);

		printf("Sending message from [%s] to [%s]\n", myPid, neighbourPid);		
		if(TEMP_FAILURE_RETRY(mq_send(neighbourQueue,(const char*)myPid,strlen(myPid),0))) {
			ERR("mq_send");
		}
	}
	
	while(1);

	if (neighbourPid != NULL) {
		closeQueue(&neighbourQueue, neighbourPid);
	}
	closeQueue(&queue, myPid);
	return EXIT_SUCCESS;
}
