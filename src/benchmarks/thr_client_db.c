#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <sys/param.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

#include "th_assert.h"
//#include "Timer.h"
#include "libmodular_BFT.h"

#include "benchmarks.h"

#include "db.h"
#include "driver.h"
#include "client_interface.h"
#include "input_data_generator.h"

/* Global Variables */
struct transaction_mix_t transaction_mix;
struct key_time_t key_time;
struct think_time_t think_time;
char hname[MAXHOSTNAMELEN];
int w_id_min = 1, w_id_max = 1;
int mode_altered = 0;
unsigned int seed = -1;
int spread = 1;
int threads_start_time= 0;
int node_id;

int terminal_state[3][TRANSACTION_MAX] = {
	{ 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0 }
};

void init_db()
{
	/* Each thread needs to seed in Linux. */
    int tid = node_id;
    pid_t pid = getpid();
    unsigned int local_seed;
	if (seed == -1) {
		struct timeval tv;
		unsigned long junk; /* Purposely used uninitialized */

		local_seed = pid;
		gettimeofday(&tv, NULL);
		local_seed ^=  tid ^ tv.tv_sec ^ tv.tv_usec ^ junk;
	} else {
		local_seed = seed;
	}
	printf("seed for %d:%x : %u\n", pid, (unsigned int)tid, local_seed);
	fflush(stdout);

	transaction_mix.delivery_actual = MIX_DELIVERY;
	transaction_mix.order_status_actual = MIX_ORDER_STATUS;
	transaction_mix.payment_actual = MIX_PAYMENT;
	transaction_mix.stock_level_actual = MIX_STOCK_LEVEL;

	key_time.delivery = KEY_TIME_DELIVERY;
	key_time.new_order = KEY_TIME_NEW_ORDER;
	key_time.order_status = KEY_TIME_ORDER_STATUS;
	key_time.payment = KEY_TIME_PAYMENT;
	key_time.stock_level = KEY_TIME_STOCK_LEVEL;

	think_time.delivery = THINK_TIME_DELIVERY;
	think_time.new_order = THINK_TIME_NEW_ORDER;
	think_time.order_status = THINK_TIME_ORDER_STATUS;
	think_time.payment = THINK_TIME_PAYMENT;
	think_time.stock_level = THINK_TIME_STOCK_LEVEL;

	srand(local_seed);
}

int recalculate_mix()
{
	/*
	 * Calculate the actual percentage that the New-Order transaction will
	 * be execute.
	 */
	transaction_mix.new_order_actual = 1.0 -
			(transaction_mix.delivery_actual +
			transaction_mix.order_status_actual +
			transaction_mix.payment_actual +
			transaction_mix.stock_level_actual);

	if (transaction_mix.new_order_actual < 0.0) {
		fprintf(stderr,
				"invalid transaction mix. d %0.1f. o %0.1f. p %0.1f. s %0.1f. n %0.1f.\n",
				transaction_mix.delivery_actual,
				transaction_mix.order_status_actual,
				transaction_mix.payment_actual,
				transaction_mix.stock_level_actual,
				transaction_mix.new_order_actual);
		return ERROR;
	}

	/* Calculate the thresholds of each transaction. */
	transaction_mix.new_order_threshold = transaction_mix.new_order_actual;
	transaction_mix.payment_threshold =
			transaction_mix.new_order_threshold +
			transaction_mix.payment_actual;
	transaction_mix.order_status_threshold =
			transaction_mix.payment_threshold
			+ transaction_mix.order_status_actual;
	transaction_mix.delivery_threshold =
			transaction_mix.order_status_threshold
			+ transaction_mix.delivery_actual;
	transaction_mix.stock_level_threshold =
			transaction_mix.delivery_threshold +
			transaction_mix.stock_level_actual;

	return OK;
}

static struct terminal_context_t main_tc;

void fill_in_request(Byz_req *req, int *req_size)
{
	int length;

	struct terminal_context_t *tc = &main_tc;
	struct client_transaction_t client_data;
	double threshold;
	int keying_time;
	struct timespec thinking_time, rem;
	int mean_think_time; /* In milliseconds. */
	struct timeval rt0, rt1;
	double response_time;
	extern int errno;
	int rc;
	int local_seed;
	pid_t pid;

    if (1) {
	/*
	 * Determine w_id and d_id for the client per
	 * transaction.
	 */
	tc->w_id = w_id_min + get_random(w_id_max - w_id_min + 1);
	tc->d_id = get_random(table_cardinality.districts) + 1;
    }

    /*
     * Determine which transaction to execute, minimum keying time,
     * and mean think time.
     */
    threshold = get_percentage();
    if (threshold < transaction_mix.new_order_threshold) {
	client_data.transaction = NEW_ORDER;
	keying_time = key_time.new_order;
	mean_think_time = think_time.new_order;
    } else if (transaction_mix.payment_actual != 0 &&
	    threshold < transaction_mix.payment_threshold) {
	client_data.transaction = PAYMENT;
	keying_time = key_time.payment;
	mean_think_time = think_time.payment;
    } else if (transaction_mix.order_status_actual != 0 &&
	    threshold < transaction_mix.order_status_threshold) {
	client_data.transaction = ORDER_STATUS;
	keying_time = key_time.order_status;
	mean_think_time = think_time.order_status;
    } else if (transaction_mix.delivery_actual != 0 &&
	    threshold < transaction_mix.delivery_threshold) {
	client_data.transaction = DELIVERY;
	keying_time = key_time.delivery;
	mean_think_time = think_time.delivery;
    } else {
	client_data.transaction = STOCK_LEVEL;
	keying_time = key_time.stock_level;
	mean_think_time = think_time.stock_level;
    }

    /*printf("executing transaction %s\n",*/
	    /*transaction_name[client_data.transaction]);*/
    /*fflush(stdout);*/

    /* Generate the input data for the transaction. */
    if (client_data.transaction != STOCK_LEVEL) {
	generate_input_data(client_data.transaction,
		&client_data.transaction_data, tc->w_id);
    } else {
	generate_input_data2(client_data.transaction,
		&client_data.transaction_data, tc->w_id, tc->d_id);
    }

    /*sleep(keying_time);*/
    /*if (gettimeofday(&rt0, NULL) == -1) {*/
	/*perror("gettimeofday");*/
    /*}*/
switch (client_data.transaction) {
	    case INTEGRITY:
	    	length = sizeof(struct integrity_t);
		break;
	    case DELIVERY:
	    	length = sizeof(struct delivery_t);
		break;
	    case NEW_ORDER:
	    	length = sizeof(struct new_order_t);
		break;
	    case ORDER_STATUS:
	    	length = sizeof(struct order_status_t);
		break;
	    case PAYMENT:
	    	length = sizeof(struct payment_t);
		break;
	    case STOCK_LEVEL:
	    	length = sizeof(struct stock_level_t);
		break;
	    default:
	    	length = 0;
	}
	length += 2*sizeof(int);

    	if (req->size < length) {
	    fprintf(stderr, "Can't store data in the request %d %d\n", req->size, length);
	    exit(1);
	}
    memcpy(req->contents, &client_data, length);
    req->size = length;
    *req_size = length;

#if 0
    if (0) {
	thinking_time.tv_nsec = (long) get_think_time(mean_think_time);
	thinking_time.tv_sec = (time_t) (thinking_time.tv_nsec / 1000);
	thinking_time.tv_nsec = (thinking_time.tv_nsec % 1000) * 1000000;
	while (nanosleep(&thinking_time, &rem) == -1) {
	    if (errno == EINTR) {
		memcpy(&thinking_time, &rem, sizeof(struct timespec));
	    } else {
		LOG_ERROR_MESSAGE(
			"sleep time invalid %d s %ls ns",
			thinking_time.tv_sec,
			thinking_time.tv_nsec);
		break;
	    }
	}
    }
#endif

    return; /* keep the compiler quiet */
}

int main(int argc, char **argv)
{
	char config_abstract[PATH_MAX];
	char config_backup_BFT[PATH_MAX];
	char config_chain[PATH_MAX];
	char config_priv[PATH_MAX];
	char config_priv_tmp[PATH_MAX];
	char host_name[MAXHOSTNAMELEN+1];
	char master_host_name[MAXHOSTNAMELEN+1];

	short port_quorum;
	short port_pbft;
	short port_chain;
	int num_bursts;
	int num_messages_per_burst;
	int request_size = -1;
	// XXX: if requests_sizes_str is -1, that means requests will be random in the range [0,4096], congruent with 8
	char request_sizes_str[PATH_MAX];

	ssize_t init_hist_size = 0;

	int argNb=1;

	strcpy(host_name, argv[argNb++]);
	strcpy(master_host_name, argv[argNb++]);
	port_quorum = atoi(argv[argNb++]);
	port_pbft = atoi(argv[argNb++]);
	port_chain = atoi(argv[argNb++]);
	num_bursts = atoi(argv[argNb++]);
	num_messages_per_burst = atoi(argv[argNb++]);
	node_id = atoi(argv[argNb++]);
	strcpy(config_abstract, argv[argNb++]);
	strcpy(config_backup_BFT, argv[argNb++]);
	strcpy(config_chain, argv[argNb++]);
	strcpy(config_priv_tmp, argv[argNb++]);

	if (argNb < argc && strcmp(argv[argNb], "-m")) {
	    fprintf(stderr, "Will read last one\n");
	    init_hist_size = atol(argv[argNb++]);
	}

	bool be_malicious = false;
	for (int i=0; i < argc; i++) {
	    if (!strcmp(argv[i], "-m")) {
		be_malicious = true;
		break;
	    }
	}

	// Priting parameters
	fprintf(stderr, "********************************************\n");
	fprintf(stderr, "*             Client parameters            *\n");
	fprintf(stderr, "********************************************\n");
	fprintf(stderr,
	"Host name = %s\nPort_quorum = %d\nPort_pbft = %d\nPort_chain = %d\nNb bursts = %d \nNb messages per burst = %d\nInit history size = %d\nRequest sizes = '%s'\nConfiguration_quorum file = %s\nConfiguration_pbft file = %s\nConfig_private_pbft directory = %s\n",
	host_name, port_quorum, port_pbft, port_chain, num_bursts, num_messages_per_burst, init_hist_size, request_sizes_str, config_abstract, config_backup_BFT, config_priv_tmp);
	fprintf(stderr, "********************************************\n\n");

	gethostname(hname, MAXHOSTNAMELEN);

	// Try to open default file
	sprintf(config_priv, "%s/%s", config_priv_tmp, hname);

	srand(0);
	init_common();
	init_db();
	if (recalculate_mix() != OK) {
		printf("invalid transaction mix: -e %0.2f. -r %0.2f. -q %0.2f. -t %0.2f. causes new-order mix of %0.2f.\n",
				transaction_mix.delivery_actual,
				transaction_mix.order_status_actual,
				transaction_mix.payment_actual,
				transaction_mix.stock_level_actual,
				transaction_mix.new_order_actual);
		return 1;
	}
	// Initialize client
	MBFT_init_client(host_name, config_abstract, config_backup_BFT,
			config_chain, config_abstract, config_priv, port_quorum, port_pbft, port_chain, port_quorum);

	// TODO let the time to the client to send its key
	sleep(2);

	// for varying sizes, in this order
	int sizes_counter = 0;
	int sizes[64];
	char *tok = strtok(request_sizes_str, " ");
	while (tok != NULL) {
	    int nsize = atoi(tok);
	    sizes[sizes_counter++] = nsize;
	    tok = strtok(NULL, " ");
	}

	// Allocate request
	int size = 8192;
	Byz_req req;
	MBFT_alloc_request(&req);

	th_assert(size <= req.size, "Request too big");

	//req.size = request_size;
	Byz_rep rep;

	// Create socket to communicate with manager
	int manager;
	if ((manager = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		th_fail("Could not create socket");
	}
	Address a;
	bzero((char *)&a, sizeof(a));
	a.sin_addr.s_addr = INADDR_ANY;
	a.sin_family = AF_INET;
	a.sin_port = htons(port_quorum+500);

	// Fill-in manager address
	Address desta;
	bzero((char *)&desta, sizeof(desta));

	struct hostent *hent = gethostbyname(master_host_name);
	if (hent == 0)
	{
		th_fail("Could not get hostent");
	}
	// desta.sin_addr.s_addr = inet_addr("192.168.20.6"); // sci6
	desta.sin_addr.s_addr = ((struct in_addr*)hent->h_addr_list[0])->s_addr;
	desta.sin_family = AF_INET;
	desta.sin_port = htons(MANAGER_PORT);
	if (connect(manager, (struct sockaddr *) &desta, sizeof(desta)) == -1)
	{
		th_fail("Could not connect name to socket");
	}

	thr_command out, in;

	// Tell manager we are up
	if (send(manager, &out, sizeof(out), 0) < sizeof(out))
	{
		fprintf(stderr, "Problem with send to manager\n");
		exit(-1);
	}

	fprintf(stderr, "Starting the bursts (num_bursts = %d)\n", num_bursts);

	fprintf(stderr, "Initializing INIT history %zu\n", init_hist_size);

	int req_size;
	for (int j = 0; j < init_hist_size; j++)
	{
	    fprintf(stderr, " %d:", j);
	    fill_in_request(&req, &req_size);
	    MBFT_invoke(&req, &rep, req_size, false);
	    MBFT_free_reply(&rep);
	    MBFT_free_request(&req);
	    MBFT_alloc_request(&req);
	}
	fprintf(stderr,"\n");
	int i;
	int successful = 0;
	struct timeval totalrt;
	struct timeval begint;
	struct timeval endt;

	bool exit_immediatelly = false;
	for (i = 0; i < num_bursts && !exit_immediatelly; i++)
	{
		char *data = (char*)&in;
		int ssize = sizeof(in);
		int ret = 0;
		while (ssize) {
		    ret = recv(manager, data, ssize, MSG_WAITALL);
		    if (ret == -1 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
			continue;
		    } else if (ret < 0) {
			fprintf(stderr, "Error receiving msg from manager\n");
			perror(NULL);
			exit(1);
		    }
		    ssize -= ret;
		    data += ret;
		}
		be_malicious = in.malicious!=0;
		MBFT_set_malicious_client(be_malicious);

		fprintf(stderr, "Starting burst #%d\n", i);
		//char a=getchar();
		// Loop invoking requests
		// int j=0;
		// while(true){
		//  j++
		int s = 0;
		s = in.num_iter;
		int j;
		successful = 0;
		totalrt.tv_sec = 0;
		totalrt.tv_usec = 0;
		for (j = s; j < num_messages_per_burst; j++)
		{
//			fprintf(stderr, "Send a new message\n");
			int retval = 0;

			bool ro = false;
			fill_in_request(&req, &req_size);
			gettimeofday(&begint, NULL);
			retval = MBFT_invoke(&req, &rep, req_size, ro);
			gettimeofday(&endt, NULL);
			if (retval == -1)
			{
			    fprintf(stderr, "thr_client: problem invoking request\n");
			    exit_immediatelly = true;
			    break;
			}
			else if (retval == -127)
			{
			    // fprintf(stderr, "thr_client: will switch to new protocol at message %d\n", j);
			    j--;
			} else {
//			fprintf(stderr, "Get a reply\n");
			    // after receiving one message, we should switch back to quorum
			    if (j%1000 == 0)
			    	fprintf(stderr, ".");
			    MBFT_free_reply(&rep);
			    successful++;
			    totalrt.tv_sec += endt.tv_sec - begint.tv_sec;
			    totalrt.tv_usec += endt.tv_usec - begint.tv_usec;
			    while (totalrt.tv_usec > 1000000) {
				totalrt.tv_usec -= 1000000;
				totalrt.tv_sec += 1;
			    }
			}
			MBFT_free_request(&req);
			MBFT_alloc_request(&req);
		}
		fprintf(stderr,"\n");
		//
		out.malicious = be_malicious;
		out.num_iter = successful;
		out.avg_rt = (totalrt.tv_sec*1e6 + totalrt.tv_usec)/successful;
		out.avg_rt = out.avg_rt/1e6;
		if (send(manager, &out, sizeof(out), 0) <= 0)
		{
			fprintf(stderr, "Sendto failed");
			exit(-1);
		}
	}
	MBFT_free_request(&req);
	shutdown(manager, SHUT_RDWR);
	close(manager);
	fprintf(stderr, "Client exiting\n");
	kill(getpid(), SIGHUP);
}

