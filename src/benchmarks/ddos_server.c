#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <sys/param.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <pthread.h>

#include "th_assert.h"
//#include "Timer.h"
#include "libmodular_BFT.h"

#include "benchmarks.h"

typedef struct {
    int type;
    int id;
    int node_id;
    int size;
    int payload; //size of the payload
    // rest is the payload;
    } ddos_req;

static long received_total = 0;
static int do_run = 1;

int sock = -1;
int request_size = -1;
int num_bursts;
int num_messages_per_burst;
int num_participants;
char host_name[MAXHOSTNAMELEN+1];

ddos_req* ddos_recv(int *status);
struct timeval tv_diff(const struct timeval tv1, const struct timeval tv2);

#include <signal.h>
void kill_server(int sig) {
    fprintf(stderr, "[Node %s] received a total of %ld/%ld requests\n", host_name, received_total, (long)num_bursts*num_messages_per_burst*num_participants);
    fprintf(stderr, "Client exiting\n");
    fflush(stderr);
    exit(0);
}

static void*requests_from_others(void *o)
{
	int status;
	char *host_name = (char*)o;
	while (do_run)
	{
	    ddos_req *m = ddos_recv(&status);
	    if (status != -1)
		received_total++;
	    else
		fprintf(stderr, "[Node %s]: had problem receiving\n", host_name);
	    free(m);
	}
	return 0;
}

int main(int argc, char **argv)
{
	char host_addr[MAXHOSTNAMELEN+1];

	short port;

	int argNb=1;

	strcpy(host_name, argv[argNb++]);
	strcpy(host_addr, argv[argNb++]);
	num_participants = atoi(argv[argNb++]);
	port = atoi(argv[argNb++]);
	num_bursts = atoi(argv[argNb++]);
	num_messages_per_burst = atoi(argv[argNb++]);
	request_size = atoi(argv[argNb++]);

	fprintf(stderr, "******************************\n* multicast test\n****************************************\n");
	fprintf(stderr, "Data is:\n* hostname = %s\n* port = %d\n* num_bursts = %d\n* msgs per burst = %d\n* request size = %d\n", host_name, port, num_bursts, num_messages_per_burst, request_size);

	char hname[MAXHOSTNAMELEN];
	gethostname(hname, MAXHOSTNAMELEN);

	// Initialize socket.
	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	int flag_on = 1;
	/* set reuse port to on to allow multiple binds per host */
	if ((setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &flag_on,
           sizeof(flag_on))) < 0) {
	       perror("setsockopt() failed");
	           exit(1);
	}

	struct sockaddr_in laddr;
	bzero(&laddr, sizeof(laddr));
	laddr.sin_family = AF_INET;
	laddr.sin_addr.s_addr = inet_addr(host_addr);
	laddr.sin_port = htons(port);

	int error = bind(sock, (struct sockaddr*)&laddr, sizeof(laddr));
	if (error < 0)
	{
		perror("Unable to name socket");
		exit(1);
	}

	struct sigaction act;
        act.sa_handler = kill_server;
        sigemptyset (&act.sa_mask);
        act.sa_flags = 0;
        sigaction (SIGTERM, &act, NULL);
        sigaction (SIGINT, &act, NULL);

	pthread_t requests_from_others_thread;

	if (pthread_create(&requests_from_others_thread, 0,
			&requests_from_others, host_name) != 0)
	{
		fprintf(stderr, "Failed to create the thread for receiving messages from other replicas\n");
		exit(1);
	}

	/*pthread_kill(requests_from_others_thread, SIGINT);*/
	pthread_join(requests_from_others_thread, NULL);

	return 0;
}

ddos_req* ddos_recv(int *status)
{
	ddos_req *m = (ddos_req*)malloc(request_size);

	int err = recvfrom(sock, (char*)m, request_size, 0, NULL, NULL);

	if (err == -1) {
	    free(m);
	    fprintf(stderr, "ERROR %d %d\n", err, errno);
	    perror("Got this!");
	    if (status != NULL)
		*status = err;
	    return NULL;
	}
	if (err == 0) {
	    // this means recv returned 0, which is a signal for closed connection
	    free(m);
	    if (status != NULL)
		*status = err;
	    return NULL;
	}
	if (status != NULL)
	    *status = err;
	return m;
}
