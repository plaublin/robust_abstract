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
#include "libbyz.h"
//#include "Timer.h"

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
char db_name[1024] = "/tmp/dbt2.sqlite";
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

struct db_context_t dbc;

// initialize the database that we're gonna use
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
	db_init(db_name);
	connect_to_db(&dbc);
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
//
// Service specific functions.
// executes tpc-c requests...
int exec_command(Byz_req *inb, Byz_rep *outb, Byz_buffer *non_det, int client,
		bool ro)
{
	//fprintf(stderr, "replica_main.cc: We are executing a request. The client is %d, size %d\n", client, reply_size);
	struct client_transaction_t *in_trans = (struct client_transaction_t *)inb->contents;

	ssize_t length = 0;
	switch (in_trans->transaction) {
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

	memcpy(outb->contents, inb->contents, length);
	struct client_transaction_t *trans = (struct client_transaction_t *)outb->contents;
	int retval = process_transaction(trans->transaction, &dbc, &(trans->transaction_data));

	//fprintf(stderr, "Replica exec_command: size %d, trans %s(%d), retval %d\n", inb->size, transaction_name[in_trans->transaction], in_trans->transaction, retval);

	//if (reply_size >= sizeof(Seqno))
	    //memcpy(outb->contents, (char*)non_det, sizeof(Seqno));

	outb->size = length;

	return retval != OK;
}

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
	int num_bursts;
	int num_messages_per_burst;
	int argNb = 1;
	num_bursts = atoi(argv[argNb++]);
	num_messages_per_burst = atoi(argv[argNb++]);

	fprintf(stderr, "********************************************\n\n");

	gethostname(hname, MAXHOSTNAMELEN);

	srand(0);
	node_id = rand();
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

	fprintf(stderr, "Starting the bursts (num_bursts = %d)\n", num_bursts);

	int i;
	int successful = 0;
	struct timeval totalrt;
	struct timeval begint;
	struct timeval endt;

	bool exit_immediatelly = false;
	Byz_req req;
	Byz_rep rep;
	req.contents = malloc(8192);
	req.size = 8192;
	rep.contents = malloc(8192);
	rep.size = 8192;
	int req_size;
	double avg_rt = 0;
	for (i = 0; i < num_bursts && !exit_immediatelly; i++)
	{
		fprintf(stderr, "Starting burst #%d\n", i);
		int s = 0;
		int j;
		successful = 0;
		avg_rt = 0;
		totalrt.tv_sec = 0;
		totalrt.tv_usec = 0;
		for (j = s; j < num_messages_per_burst; j++)
		{
//			fprintf(stderr, "Send a new message\n");
			int retval = 0;

			bool ro = false;
			req.size = 8192;
			rep.size = 8192;
			fill_in_request(&req, &req_size);
			gettimeofday(&begint, NULL);
			// execute request...
			exec_command(&req, &rep, NULL, 0, false);
			gettimeofday(&endt, NULL);
			{
			    //			fprintf(stderr, "Get a reply\n");
			    // after receiving one message, we should switch back to quorum
			    if (j%1000 == 0)
			    	fprintf(stderr, ".");
			    successful++;
			    totalrt.tv_sec += endt.tv_sec - begint.tv_sec;
			    totalrt.tv_usec += endt.tv_usec - begint.tv_usec;
			    while (totalrt.tv_usec < 0) {
				totalrt.tv_usec += 1000000;
				totalrt.tv_sec -= 1;
			    }
			    while (totalrt.tv_usec > 1000000) {
				totalrt.tv_usec -= 1000000;
				totalrt.tv_sec += 1;
			    }
			}
		}
		fprintf(stderr,"\n");
		//
		avg_rt = (totalrt.tv_sec + totalrt.tv_usec/1e9)/successful;
		fprintf(stderr, "Average time spent in requests is %12.10g [%d]\n", avg_rt, successful);
	}
	fprintf(stderr, "Client exiting\n");
	kill(getpid(), SIGHUP);
}

