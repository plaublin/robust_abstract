#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/param.h>
#include <unistd.h>
#include <time.h>

#include "th_assert.h"
#include "Traces.h"
#include "libmodular_BFT.h"

#include "benchmarks.h"

extern "C" {
#include "db.h"
#include "client_interface.h"
#include "sqlite_common.h"
}

char db_name[1024] = "/tmp/dbt2.sqlite";

struct db_context_t dbc;
// initialize the database that we're gonna use
int init_db()
{
    db_init(db_name);
    if (connect_to_db(&dbc) != OK) {
	fprintf(stderr, "COuld not set up the database!\n");
	exit(1);
    }

    // fill in the rest...
}

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

int main(int argc, char **argv)
{
	short port_quorum;
	short port_pbft;
	short port_chain;
	char config_quorum[PATH_MAX];
	char config_pbft[PATH_MAX];
	char config_priv_pbft[PATH_MAX];
	char config_priv_tmp_pbft[PATH_MAX];
	char config_chain[PATH_MAX];
	char host_name[MAXHOSTNAMELEN+1];
	int argNb = 1;
	ssize_t init_history_size = 0;
	int percent_misses = 0;

	strcpy(host_name, argv[argNb++]);
	port_quorum = atoi(argv[argNb++]);
	port_pbft = atoi(argv[argNb++]);
	port_chain = atoi(argv[argNb++]);
	// reply_size = atoi(argv[argNb++]);
	argNb++; // skip reply_size
	// sleep_time_ns = atol(argv[argNb++]);
	argNb++; // skip sleep_time_ns
	strcpy(config_quorum, argv[argNb++]);
	strcpy(config_pbft, argv[argNb++]);
	strcpy(config_priv_tmp_pbft, argv[argNb++]);
	strcpy(config_chain, argv[argNb++]);
	init_history_size = atoi(argv[argNb++]);
	percent_misses= atoi(argv[argNb++]);

	// Priting parameters
	fprintf(stderr, "********************************************\n");
	fprintf(stderr, "*             Replica parameters           *\n");
	fprintf(stderr, "********************************************\n");
	fprintf(
	stderr,
	"Host name = %s\nPort_quorum = %d\nPort_pbft = %d\nPort_chain = %d\nReply size = %d \nConfiguration_quorum file = %s\nConfiguration_chain file = %s\nConfiguration_pbft file = %s\nConfig_private_pbft directory = %s\n",
	host_name, port_quorum, port_pbft, port_chain, -1,
	config_quorum, config_chain, config_pbft, config_priv_tmp_pbft);
	fprintf(stderr, "********************************************\n\n");

	char hname[MAXHOSTNAMELEN];
	gethostname(hname, MAXHOSTNAMELEN);
	// Try to open default file
	sprintf(config_priv_pbft, "%s/%s", config_priv_tmp_pbft, hname);

	int mem_size = 1024 * 8192;
	char *mem = (char*) valloc(mem_size);
	bzero(mem, mem_size);

	init_db();

	MBFT_init_replica(host_name, config_quorum, config_pbft,
			config_priv_pbft, config_chain, config_quorum, mem, mem_size, exec_command,
			port_quorum, port_pbft, port_chain, port_quorum);
}

