#include "fmacros.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/param.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

extern "C" {
#include "ae.h"
#include "anet.h"
#include "sds.h"
#include "adlist.h"
#include "zmalloc.h"
}

#define REPLY_INT 0
#define REPLY_RETCODE 1
#define REPLY_BULK 2
#define REPLY_MBULK 3

#define CLIENT_CONNECTING 0
#define CLIENT_SENDQUERY 1
#define CLIENT_READREPLY 2

#define MAX_LATENCY 5000

#define REDIS_NOTUSED(V) ((void) V)

static struct config {
    int debug;
    int requests;
    int donerequests;
    int datasize;
    char *hostip;
    int hostport;
    int keepalive;
    long long start;
    int quiet;
    int loop;
    int idlemode;
} config;

typedef struct _client {
    int state;
    int fd;
    sds obuf;
    sds ibuf;
    int mbulk;          /* Number of elements in an mbulk reply */
    int readlen;        /* readlen == -1 means read a single line */
    int totreceived;
    unsigned int written;        /* bytes of 'obuf' already written */
    int replytype;
} *client;

client the_redis_client;

#include "th_assert.h"
#include "Traces.h"
#include "libmodular_BFT.h"

#include "benchmarks.h"

/* Prototypes */
static void writeHandler();
static void readHandler();

// Service specific functions.
static client createClient(void) {
    client c = (client)zmalloc(sizeof(struct _client));
    char err[ANET_ERR_LEN];

    c->fd = anetTcpNonBlockConnect(err,config.hostip,config.hostport);
    if (c->fd == ANET_ERR) {
        zfree(c);
        fprintf(stderr,"Connect: %s\n",err);
        return NULL;
    }
    anetTcpNoDelay(NULL,c->fd);
    c->obuf = sdsempty();
    c->ibuf = sdsempty();
    c->mbulk = -1;
    c->readlen = 0;
    c->written = 0;
    c->totreceived = 0;
    c->state = CLIENT_CONNECTING;
    return c;
}

static void prepareClientForReply(client c, int type) {
    if (type == REPLY_BULK) {
        c->replytype = REPLY_BULK;
        c->readlen = -1;
    } else if (type == REPLY_MBULK) {
        c->replytype = REPLY_MBULK;
        c->readlen = -1;
        c->mbulk = -1;
    } else {
        c->replytype = type;
        c->readlen = 0;
    }
}

static void writeHandler()
{
    client c = the_redis_client;

    if (c->state == CLIENT_CONNECTING) {
        c->state = CLIENT_SENDQUERY;
    }
    if (sdslen(c->obuf) > c->written) {
        void *ptr = c->obuf+c->written;
        int len = sdslen(c->obuf) - c->written;
        while (1) {
	    int nwritten = write(c->fd, ptr, len);
	    if (nwritten == -1 && errno == EAGAIN)
		continue;
            if (nwritten == -1) {
		if (errno != EPIPE)
                    fprintf(stderr, "Writing to socket: %s\n", strerror(errno));
		exit(2);
		return;
            }
            c->written += nwritten;
	    break;
	}
        if (sdslen(c->obuf) == c->written) {
            c->state = CLIENT_READREPLY;
        }
    }
}

static void readHandler()
{
    char buf[1024];
    int nread;

    while (1) {
    	nread = read(the_redis_client->fd, buf, 1024);
    	if (nread == -1 && errno == EAGAIN)
    	    continue;
    	break;
    }
    if (nread == -1) {
        fprintf(stderr, "Reading from socket: %s\n", strerror(errno));
	exit(3);
        return;
    }
    if (nread == 0) {
        fprintf(stderr, "EOF from client\n");
	exit(3);
        return;
    }
    the_redis_client->totreceived += nread;
    the_redis_client->ibuf = sdscatlen(the_redis_client->ibuf,buf,nread);

processdata:
    /* Are we waiting for the first line of the command of for  sdf 
     * count in bulk or multi bulk operations? */
    if (the_redis_client->replytype == REPLY_INT ||
        the_redis_client->replytype == REPLY_RETCODE ||
        (the_redis_client->replytype == REPLY_BULK && the_redis_client->readlen == -1) ||
        (the_redis_client->replytype == REPLY_MBULK && the_redis_client->readlen == -1) ||
        (the_redis_client->replytype == REPLY_MBULK && the_redis_client->mbulk == -1)) {
        char *p;

        /* Check if the first line is complete. This is only true if
         * there is a newline inside the buffer. */
        if ((p = strchr(the_redis_client->ibuf,'\n')) != NULL) {
            if (the_redis_client->replytype == REPLY_BULK ||
                (the_redis_client->replytype == REPLY_MBULK && the_redis_client->mbulk != -1))
            {
                /* Read the count of a bulk reply (being it a single bulk or
                 * a multi bulk reply). "$<count>" for the protocol spec. */
                *p = '\0';
                *(p-1) = '\0';
                the_redis_client->readlen = atoi(the_redis_client->ibuf+1)+2;
                // printf("BULK ATOI: %s\n", the_redis_client->ibuf+1);
                /* Handle null bulk reply "$-1" */
                if (the_redis_client->readlen-2 == -1) {
                    return;
                }
                /* Leave all the rest in the input buffer */
                the_redis_client->ibuf = sdsrange(the_redis_client->ibuf,(p-the_redis_client->ibuf)+1,-1);
                /* fall through to reach the point where the code will try
                 * to check if the bulk reply is complete. */
            } else if (the_redis_client->replytype == REPLY_MBULK && the_redis_client->mbulk == -1) {
                /* Read the count of a multi bulk reply. That is, how many
                 * bulk replies we have to read next. "*<count>" protocol. */
                *p = '\0';
                *(p-1) = '\0';
                the_redis_client->mbulk = atoi(the_redis_client->ibuf+1);
                /* Handle null bulk reply "*-1" */
                if (the_redis_client->mbulk == -1) {
                    return;
                }
                // printf("%p) %d elements list\n", c, the_redis_client->mbulk);
                /* Leave all the rest in the input buffer */
                the_redis_client->ibuf = sdsrange(the_redis_client->ibuf,(p-the_redis_client->ibuf)+1,-1);
                goto processdata;
            } else {
                the_redis_client->ibuf = sdstrim(the_redis_client->ibuf,"\r\n");
                return;
            }
        }
    }
    /* bulk read, did we read everything? */
    if (((the_redis_client->replytype == REPLY_MBULK && the_redis_client->mbulk != -1) || 
         (the_redis_client->replytype == REPLY_BULK)) && the_redis_client->readlen != -1 &&
          (unsigned)the_redis_client->readlen <= sdslen(the_redis_client->ibuf))
    {
        // printf("BULKSTATUS mbulk:%d readlen:%d sdslen:%d\n",
        //    the_redis_client->mbulk,the_redis_client->readlen,sdslen(the_redis_client->ibuf));
        if (the_redis_client->replytype == REPLY_BULK) {
        } else if (the_redis_client->replytype == REPLY_MBULK) {
            // printf("%p) %d (%d)) ",c, the_redis_client->mbulk, the_redis_client->readlen);
            // fwrite(the_redis_client->ibuf,the_redis_client->readlen,1,stdout);
            // printf("\n");
            if (--the_redis_client->mbulk == 0) {
            } else {
                the_redis_client->ibuf = sdsrange(the_redis_client->ibuf,the_redis_client->readlen,-1);
                the_redis_client->readlen = -1;
                goto processdata;
            }
        }
    }
}

static int my_cmd_counter = 0;
int exec_command(Byz_req *inb, Byz_rep *outb, Byz_buffer *non_det, int client,
		bool ro)
{
	//fprintf(stderr, "replica_main.cc: We are executing a request. The client is %d, size %d\n", client, reply_size);

	//outb->size = length;
	char *data = inb->contents;
	int type = *(int*)data;
	data = data + sizeof(int);

#ifdef TRACES
	fprintf(stderr, "Got request to process, size %ld\n", inb->size);
	fprintf(stderr, "[");
	for (int pos=0; pos < inb->size-sizeof(int); pos++)
	    fprintf(stderr, "%hhx ", data[pos]);
	fprintf(stderr, "]\n");
#endif
	//the_redis_client->obuf = sdscatlen(the_redis_client->obuf, data, inb->size-sizeof(int));
	the_redis_client->obuf = sdsnewlen(data, inb->size-sizeof(int));
	//memcpy(the_redis_client->obuf, data, inb->size-sizeof(int));
#if 0
	fprintf(stderr, "obuf [");
	for (int pos=0; pos < inb->size-sizeof(int); pos++)
	    fprintf(stderr, "%d:%02hhx ", pos, the_redis_client->obuf[pos]);
	fprintf(stderr, "]\n");
#endif

	writeHandler();
	prepareClientForReply(the_redis_client, type);
	readHandler();

	memcpy(outb->contents, the_redis_client->ibuf, the_redis_client->totreceived);
#ifdef TRACES
	fprintf(stderr, "%d Totreceived %d\n", my_cmd_counter++, the_redis_client->totreceived);
	fprintf(stderr, "[");
	for (int pos=0; pos < the_redis_client->totreceived; pos++)
	    fprintf(stderr, "%hhx ",outb->contents[pos]);
	fprintf(stderr, "]\n");
	fprintf(stderr, "outbsize %d\n", outb->size);
#endif
	outb->size = the_redis_client->totreceived;

#if 0
	if (outb->size > 5) {
	    fprintf(stderr, "<%s>\n", the_redis_client->ibuf);
	fprintf(stderr, "[");
	data = the_redis_client->obuf;
	for (int pos=0; pos < inb->size-sizeof(int); pos++)
	    fprintf(stderr, "%02hhx(%c) ", data[pos], isgraph(data[pos])?data[pos]:'*');
	fprintf(stderr, "]\n");
	}
#endif
	the_redis_client->totreceived = 0;
	the_redis_client->readlen = (the_redis_client->replytype == REPLY_BULK ||
		the_redis_client->replytype == REPLY_MBULK) ? -1 : 0;
	the_redis_client->mbulk = -1;
	the_redis_client->written = 0;
	sdsfree(the_redis_client->obuf);
	sdsfree(the_redis_client->ibuf);
	//the_redis_client->obuf = sdsempty();
	the_redis_client->ibuf = sdsempty();
	the_redis_client->state = CLIENT_SENDQUERY;

	return 0;
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

	config.debug = 0;
	config.keepalive = 1;
	config.donerequests = 0;
	config.quiet = 0;
	config.loop = 0;

	config.hostip = "127.0.0.1";
	config.hostport = 6379;

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

	//init_db();
	the_redis_client = createClient();
	if (!the_redis_client) {
	    fprintf(stderr, "Could not create redis client\n");
	    exit(1);
	}

	MBFT_init_replica(host_name, config_quorum, config_pbft,
			config_priv_pbft, config_chain, config_quorum, mem, mem_size, exec_command,
			port_quorum, port_pbft, port_chain, port_quorum);
}

