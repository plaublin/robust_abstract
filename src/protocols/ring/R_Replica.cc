#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <errno.h>

#include <sys/epoll.h>
#include <poll.h>

#include <queue>

#include "th_assert.h"
#include "R_Message_tags.h"
#include "R_Request.h"
#include "R_ACK.h"
#include "R_Checkpoint.h"
#include "R_Reply.h"
#include "R_Principal.h"
#include "R_Replica.h"
#include "types.h"
#include "MD5.h"

#define _MEASUREMENTS_ID_ (R_replica->id())
#include "measurements.h"

// #define TRACES
#define REQ_ACK_DEBUG

// Global replica object.
R_Replica *R_replica;

#define RANSWER (num_replicas - 1)
#define max(x,y) ((x)>(y)?(x):(y))

// statistics
long long total_reqs_batched = 0;
long long successful_batching = 0;
long long tried_batching = 0;
long long tried_piggyback = 0;
long long successful_piggyback = 0;
long long nonempty_queue_stat = 0;

#ifdef DO_TIMINGS
#include <signal.h>
static void kill_replica(int sig)
{
    REPORT_TIMINGS;
    if (R_replica != NULL)
    {
    	delete R_replica;
	R_replica=NULL;
    }
    exit(0);
}
#endif

#ifdef DO_STATISTICS
// client
#define STAT_C_PREQ_POS 0
#define STAT_C_POSTQ_POS 1
// replica: {predecessor, successor}
#define STAT_R_PREQ_POS 2
#define STAT_R_POSTQ_POS 3

// how many readings
#define STAT_SAMPLES 4
// offset where previous data starts
#define STAT_OFFSET 4
#define STAT_SIZE (4*sizeof(long long))

// x is {C,R}, p is {PRE,POST}, s is size
#define UPDATE_IN(x,p,s) do { reqs_in[STAT_##x##_##p##Q_POS]++; bytes_in[STAT_##x##_##p##Q_POS] += s; } while(0);
#define UPDATE_OUT(x,p,s) do { reqs_out[STAT_##x##_##p##Q_POS]++; bytes_out[STAT_##x##_##p##Q_POS] += s; } while(0);

#define STAT_DO_DIFF(name) \
    long long name##_diff[STAT_SAMPLES];\
    for (int ssi=0;ssi<STAT_SAMPLES;ssi++) {\
	name##_diff[ssi] = name[ssi] - name[ssi+STAT_OFFSET];\
    }

#define STAT_MOVE_TO_PREV(name) \
    memcpy(name+STAT_OFFSET, name, STAT_SIZE);

// scripts should do the math and calculate total...
#define STAT_PRINT_STATS(name) \
	    fprintf(statfd, \
		    "R_Replica: [" #name "] client pre-in: %lld, post-in: %lld, pre-out: %lld; replica pre-in: %lld, post-in: %lld, pre-out: %lld;\n",\
		    name##_in_diff[STAT_C_PREQ_POS], name##_in_diff[STAT_C_POSTQ_POS], name##_out_diff[STAT_C_PREQ_POS],\
		    name##_in_diff[STAT_R_PREQ_POS], name##_in_diff[STAT_R_POSTQ_POS], name##_out_diff[STAT_R_PREQ_POS]);

#define STAT_PRINT_STATS_PER_TIME(name, time) \
	    fprintf(statfd, \
		    "R_Replica: [" #name "/s] client pre-in: %.3e, post-in: %.3e, pre-out: %.3e; replica pre-in: %.3e, post-in: %.3e, pre-out: %.3e;\n",\
		    name##_in_diff[STAT_C_PREQ_POS]/time, name##_in_diff[STAT_C_POSTQ_POS]/time, name##_out_diff[STAT_C_PREQ_POS]/time,\
		    name##_in_diff[STAT_R_PREQ_POS]/time, name##_in_diff[STAT_R_POSTQ_POS]/time, name##_out_diff[STAT_R_PREQ_POS]/time);

long long reqs_in[2*STAT_SAMPLES];
long long reqs_out[2*STAT_SAMPLES];
long long bytes_in[2*STAT_SAMPLES];
long long bytes_out[2*STAT_SAMPLES];

double avg_tot = 0.0; // average turn-over-time
double sum_tot = 0.0; // to count the average tot
long long tot_samples = 0;

#include <signal.h>
static void dump_stats(int sig)
{
	fprintf(stderr, "R_Replica: exiting\n");
	fprintf(stderr, "Statistics: batching %lld/%lld/%lld, piggybacking %lld/%lld, total_batched %lld\n", successful_batching, nonempty_queue_stat, tried_batching, successful_piggyback, tried_piggyback, total_reqs_batched);
	fprintf(stderr, "Statistics: avg batch size %g\n", total_reqs_batched*1.0/successful_batching);
	fprintf(stderr, "Statistics: total reqs in: %lld, out: %lld\n", reqs_in[STAT_C_POSTQ_POS]+reqs_in[STAT_R_POSTQ_POS], reqs_out[STAT_C_PREQ_POS]+reqs_out[STAT_R_POSTQ_POS]);
	fprintf(stderr, "Statistics: total bytes in: %lld, out: %lld\n", bytes_in[STAT_C_POSTQ_POS]+bytes_in[STAT_R_POSTQ_POS], bytes_out[STAT_C_PREQ_POS]+bytes_out[STAT_R_POSTQ_POS]);

	REPORT_TIMINGS;
        exit(0);
}
#else
#define UPDATE_IN(x,p,s)
#define UPDATE_OUT(x,p,s)
#endif

// Function for the thread receiving messages from the predecessor in the ring
//extern "C"
static void*requests_from_predecessor_handler_helper(void *o)
{
	void **o2 = (void **)o;
	R_Replica &r = (R_Replica&)(*o2);
	r.requests_from_predecessor_handler();
	return 0;
}

// Function for the thread receiving messages from clients
//extern "C"
void*R_client_requests_handler_helper(void *o)
{
	void **o2 = (void **)o;
	R_Replica &r = (R_Replica&)(*o2);
#ifdef USE_SELECT_EVENT_LOOP
	r.c_client_requests_handler_select();
#elif USE_POLL_EVENT_LOOP
	r.c_client_requests_handler_poll();
#else
	r.c_client_requests_handler_epoll();
#endif
	return 0;
}

inline void*R_message_queue_handler_helper(void *o)
{
	void **o2 = (void **)o;
	R_Replica &r = (R_Replica&) (*o2);
	//temp_replica_class = (Replica<class Request_T, class Reply_T>&)(*o);
	//  r.recv1();
	//temp_replica_class.do_recv_from_queue();
	r.do_recv_from_queue();
	return 0;
}

// Function for setting up the epoll listener -- thread which accepts clients connections
#if 0
void *R_Replica_listener_helper(void *o)
{
    void **o2 = (void**)o;
    R_Replica &r = (R_Replica&)(*o2);
    r.handle_new_connections();
    return 0;
}
#endif

#ifdef DO_STATISTICS
#define GET_DIFF(name) long long name##_diff[2] = { name##[0]-name##_prev[0], name##[1]-name##_prev[1] }
#define MOVE_TO_PREV(name) name##_prev[0] = name##[0]; name##_prev[1] = name##[1]

inline void* R_Replica_throughput_reporter(void *o)
{
	void **o2 = (void **)o;
	R_Replica &r = (R_Replica &)(*o2);

	char name[FILENAME_MAX];
	sprintf(name, "/tmp/throughput.replica%d.out", r.id());
	FILE *statfd = fopen(name, "w");
	if (statfd == NULL) {
	    fprintf(stderr, "R_Replica[%d]: couldn't open file for writing stats\n", r.id());
	    return NULL;
	}

	struct timeval t0, t1;
	gettimeofday(&t0, 0);
	while (reqs_in[STAT_C_PREQ_POS] == 0 && reqs_in[STAT_R_PREQ_POS] == 0)
	    sleep(1);
	while (1) {
	    gettimeofday(&t1, 0);
	    float diff_time = (t1.tv_sec-t0.tv_sec)+(t1.tv_usec-t0.tv_usec)/1e6;

	    STAT_DO_DIFF(reqs_in);
	    STAT_DO_DIFF(bytes_in);
	    STAT_DO_DIFF(reqs_out);
	    STAT_DO_DIFF(bytes_out);

	    STAT_MOVE_TO_PREV(reqs_in);
	    STAT_MOVE_TO_PREV(bytes_in);
	    STAT_MOVE_TO_PREV(reqs_out);
	    STAT_MOVE_TO_PREV(bytes_out);

	    STAT_PRINT_STATS(reqs);
	    STAT_PRINT_STATS(bytes);

	    STAT_PRINT_STATS_PER_TIME(reqs, diff_time);

	    t0 = t1;
	    sleep(1);
	}
	fclose(statfd);
}
#endif

R_Replica::R_Replica(FILE *config_file, FILE *config_priv, char* host_name, short port) :
	R_Node(config_file, config_priv, host_name, port), seqno(0), last_executed(0), replies(num_principals),
	incoming_queue_clients(),
	connectmap(MAX_CONNECTIONS),
	epoll_connectmap(MAX_CONNECTIONS),
#ifdef USE_POLL_EVENT_LOOP
	poll_connectmap(MAX_CONNECTIONS),
	num_of_pollfds(0),
#endif
#ifdef USE_SELECT_EVENT_LOOP
	select_connectmap(MAX_CONNECTIONS),
#endif
	vector_clock((Seqno)0, num_replicas),
	latest_seen_rid(num_replicas*MAX_CONNECTIONS),
	requests(),
	rh(this)
{
	// Fail if node is not a replica.
	if (!is_replica(id()))
	{
		th_fail("Node is not a replica");
	}

	// Read view change, status, and recovery timeouts from replica's portion
	// of "config_file"
	int vt, st, rt;
	fscanf(config_file, "%d\n", &vt);
	fscanf(config_file, "%d\n", &st);
	fscanf(config_file, "%d\n", &rt);

	// Create timers and randomize times to avoid collisions.
	srand48(getpid());

	exec_command = 0;

	R_replica = this;
#ifdef DO_STATISTICS
	// signal handler to dump profile information.
        struct sigaction act;
        act.sa_handler = dump_stats;
        sigemptyset (&act.sa_mask);
        act.sa_flags = 0;
        sigaction (SIGINT, &act, NULL);
        sigaction (SIGTERM, &act, NULL);
#elif defined(DO_TIMINGS)
	struct sigaction act;
        act.sa_handler = kill_replica;
        sigemptyset (&act.sa_mask);
        act.sa_flags = 0;
        sigaction (SIGINT, &act, NULL);
        sigaction (SIGTERM, &act, NULL);
#endif

	epoll_fd = epoll_create(MAX_CONNECTIONS);
	if (epoll_fd < 0) {
	    perror("epoll_create:");
	    exit(1);
	}

	connectmap.set_empty_key(-1);
	connectmap.set_deleted_key(-3);
	epoll_connectmap.set_empty_key(-1);
	epoll_connectmap.set_deleted_key(-3);
#ifdef USE_POLL_EVENT_LOOP
	poll_connectmap.set_empty_key(-1);
	poll_connectmap.set_deleted_key(-3);
#endif
#ifdef USE_SELECT_EVENT_LOOP
	select_connectmap.set_empty_key(-1);
	select_connectmap.set_deleted_key(-3);
#endif

	connectmap.clear();
	epoll_connectmap.clear();
#ifdef USE_POLL_EVENT_LOOP
	poll_connectmap.clear();
#endif
#ifdef USE_SELECT_EVENT_LOOP
	select_connectmap.clear();
#endif

	latest_seen_rid.set_empty_key(0);
	latest_seen_rid.set_deleted_key(-1);
	latest_seen_rid.clear();

	incoming_queue_signaler[0] = 0;
	incoming_queue_signaler[1] = 0;

	// Create server socket
	in_socket= createServerSocket(ntohs(principals[node_id]->TCP_addr.sin_port));

	// Create receiving thread
	if (pthread_create(&requests_from_predecessor_handler_thread, 0,
			&requests_from_predecessor_handler_helper, (void *)this) != 0)
	{
		fprintf(stderr, "Failed to create the thread for receiving messages from predecessor in the ring\n");
		exit(1);
	}

	// Connect to principals[(node_id + 1) % num_r]
	fprintf(stderr, "R_Replica[%d]: Trying to get out_socket\n", node_id);
	out_socket = createClientSocket(principals[(node_id + 1) % num_replicas]->TCP_addr);

	fprintf(stderr,"Creating client socket\n");
	in_socket_for_clients= createNonBlockingServerSocket(ntohs(principals[node_id]->TCP_addr_for_clients.sin_port));
	if (pthread_create(&R_client_requests_handler_thread, NULL,
		    &R_client_requests_handler_helper, (void *)this)!= 0)
	{
	    fprintf(stderr, "Failed to create the thread for receiving client requests\n");
	    exit(1);
	}
#if 0
	if (pthread_create(&R_Replica_listener_thread, NULL, &R_Replica_listener_helper, (void*)this)!=0)
	{
	    fprintf(stderr, "Failed to create the thread for accepting client requests (Listener)\n");
	    exit(1);
	}
#endif

#ifdef DO_STATISTICS
	if (pthread_create(&R_Replica_throughput_reporter_thread, NULL,
		    &R_Replica_throughput_reporter, (void*)this) != 0)
	{
	    fprintf(stderr, "Failed to start stat reported thread\n");
	}
#endif
}

R_Replica::~R_Replica()
{
	close(epoll_fd);
}

void R_Replica::do_recv_from_queue()
{
	R_Message *m;

	// deficit round robin:
	// for each queue, there is a quantum
	// also, there is a deficit counter, initialy set to quantum
	// for choosen queue, send packet until size(top(queue)) <= dc+quantum
	// if queue is empty, reset dc for queue
#define DRR_FOR_SIZE
#define DRR_PRIORITY_RATIO (2*num_replicas-1)

#ifdef DRR_FOR_SIZE
#define DRR_LOWER_PRIO 4096
#else
#define DRR_LOWER_PRIO 1
#endif

	static const ssize_t quantum[2] = {DRR_PRIORITY_RATIO*DRR_LOWER_PRIO, DRR_LOWER_PRIO}; // this is the initial quantum
	static ssize_t credit[2] = {DRR_PRIORITY_RATIO*DRR_LOWER_PRIO, DRR_LOWER_PRIO}; // here, we keep track of how many units we have sent...

	static int selector = 0;
	ssize_t qs;

	while (1)
	{
		m = NULL;
		pthread_mutex_lock(&incoming_queue_mutex);
		{
			while (incoming_queue.size() == 0 && incoming_queue_clients.size() == 0)
			{
				pthread_cond_wait(&not_empty_incoming_queue_cond, &incoming_queue_mutex);
			}
			// at this point, we know there is something in either queue
			// 0 is for predecessor,
			// 1 is for clients
			switch (selector) {
			    case 0: qs = incoming_queue.size(); break;
			    case 1: qs = incoming_queue_clients.size(); break;
			    default: selector = 0; goto loop_exit; break;
			}
			if (!incoming_queue_signaler[selector] && qs == 0) {
			    // selected queue is empty, reset counter
			    credit[selector] = 0;
			    selector = 1-selector;
			    goto loop_exit;
			}
			//fprintf(stderr, "R_Replica[%d]: selector %d, credit %d, qs %d\n", id(), selector, credit[selector], qs);
			if (selector == 0) {
			    if (incoming_queue.empty() || incoming_queue.first() == NULL) {
				credit[selector] = 0;
				selector = 1-selector;
				goto loop_exit;
			    }
#ifdef DRR_FOR_SIZE
			    if (incoming_queue.first()->size() > credit[selector] + quantum[selector]) {
				credit[selector] += quantum[selector];
#else
			    if (credit[selector] + quantum[selector] == 0) {
				credit[selector] = 0;
#endif
				selector = 1-selector;
				goto loop_exit;
			    }
			    m = incoming_queue.remove();
			    incoming_queue_signaler[selector] = 0;
			    UPDATE_IN(R,POST,m->size());
			    goto loop_exit;
			} else {
			    if (incoming_queue_clients.empty() || incoming_queue_clients.first() == NULL) {
				credit[selector] = 0;
				selector = 1-selector;
				goto loop_exit;
			    }
			    //fprintf(stderr, "R_Replica[%d]: size of request is %lld\n", id(), incoming_queue_clients.first()->size());
#ifdef DRR_FOR_SIZE
			    if (incoming_queue_clients.first()->size() > credit[selector] + quantum[selector]) {
				credit[selector] += quantum[selector];
#else
			    if (credit[selector] + quantum[selector] == 0) {
				credit[selector] = 0;
#endif
				selector = 1-selector;
				goto loop_exit;
			    }
			    m = incoming_queue_clients.remove();
			    incoming_queue_signaler[selector] = 0;
			    UPDATE_IN(C,POST,m->size());
			    goto loop_exit;
			}
		}
loop_exit:
		pthread_mutex_unlock(&incoming_queue_mutex);
		if (m != NULL)
		    handle(m);
	}
}

void R_Replica::register_exec(int(*e)(Byz_req *, Byz_rep *, Byz_buffer *, int, bool))
{
	exec_command = e;
}

// used for piggybacking mainly...
R_Request* R_Replica::get_new_client_request()
{
    R_Request *req = NULL;
    int i = 1;
    pthread_mutex_lock(&incoming_queue_mutex);
    while (i-->0 && incoming_queue_clients.size() > 0)
    {
		if (incoming_queue_clients.first()->tag() != R_Request_tag)
	    	break;
		if (incoming_queue_clients.first()->extra() & R_MESSAGE_READ_ONLY)
	    	break; // don't handle read-only requests
		R_Message *m = incoming_queue_clients.remove();
		// XXX: must not call convert, because it calls trim()
		if (!m->has_tag(R_Request_tag, sizeof(R_Request_rep)))
	    	continue;

		req = new R_Request(R_Max_message_size);
		memcpy(req->contents(), m->contents(), m->size());
		UPDATE_IN(C,POST,m->size());
		delete m;
    }
    pthread_mutex_unlock(&incoming_queue_mutex);
    return req;
}

#if 0
void R_Replica::get_and_batch_requests(R_Request **oreq)
{
    R_Request *req = *oreq;
    // fprintf(stderr, "R_Replica[%d]: msg_size is %d at beginnning\n", id(), req->size());
    int max_count = MAX_BATCH_SIZE;
    int cur_size = req->size();
    int orig_req_size = req->size();

    tried_batching++;

    std::queue<R_Request *> tobatch;
    std::map<int,bool> seen_cids;
    std::map<int,int> cid_req_pos;
    seen_cids[req->client_id()] = true;
    cid_req_pos[req->client_id()] = -1;

    pthread_mutex_lock(&incoming_queue_mutex);
    while (max_count > 0 && incoming_queue_clients.size() > 0)
    {
	//fprintf(stderr, "R_Request[%d]: will check one request\n", id());
	if (incoming_queue_clients.first()->tag() != R_Request_tag)
	    break;
	if (incoming_queue_clients.first()->size() + cur_size >= R_Max_message_size)
	    break;
	if (incoming_queue_clients.first()->extra() & R_MESSAGE_READ_ONLY)
	    break; // don't handle read-only requests
	R_Message *m = incoming_queue_clients.remove();
	R_Request *breq;
	if (!R_Request::convert(m, breq))
	{
	    delete m;
	    continue;
	}
	// check if request is a retransmit
	if (seen_cids.find(breq->client_id()) != seen_cids.end())
	{
#ifdef TRACES
	    fprintf(stderr, "R_Replica[%d]: duplicate request from %d\n", id(), breq->client_id());
#endif
	    // skip to the next one, don't update the counter
	    tobatch.push(breq);
	    if (tobatch.size() > MAX_IN_A_BATCH_CHECK)
		break;
	    else
		continue;
	}
	seen_cids[breq->client_id()] = true;
	cid_req_pos[breq->client_id()] = -1;
	max_count--;
	tobatch.push(breq);
	UPDATE_IN(C,POST,breq->size());
    }
    pthread_mutex_unlock(&incoming_queue_mutex);
    if (tobatch.size() > 0)
	nonempty_queue_stat++;

    if (tobatch.size() > 0) {
	R_Request *nreq = new R_Request(R_Max_message_size);
	memcpy(nreq->contents(), req->contents(), req->size());
	delete req;
	*oreq = req = nreq;
    }
    // prune duplicates and/or old requests
    std::vector<R_Request*> candidates;
    while (tobatch.size() > 0)
    {
	R_Request *breq;
	breq = tobatch.front();

	if (cid_req_pos[breq->client_id()] == -1) {
	    // if never seen from client, enqueue
	    cid_req_pos[breq->client_id()] = candidates.size();
	    candidates.push_back(breq);
	} else {
	    if (breq->request_id() < replies.reply(breq->client_id())->request_id()) {
		// this request is older than the reply
		delete breq;
		tobatch.pop();
		continue;
	    }
	    R_Request *creq = candidates[cid_req_pos[breq->client_id()]];
	    if (breq->request_id() > creq->request_id()) {
		// this request is newer than one in the queue, add to processing
		//fprintf(stderr, "R_Replica[%d]: discarding old request <%d,%lld>\n", id(), creq->client_id(), creq->request_id());
		delete creq;
		candidates[cid_req_pos[breq->client_id()]] = breq;
	    } else {
		// just a copy from already enqueue from the client
		delete breq;
	    }
	}
	tobatch.pop();
    }
    int batched_index = 1;
    int candidate_index = 0;
    int candidate_size = candidates.size();
    while (candidate_index < candidate_size)
    {
	R_Request *breq;
	breq = candidates[candidate_index];

	//fprintf(stderr, "\t\tsize %d, ptr %p, <%d,%lld>\n", breq->size(), breq->contents(), breq->client_id(), breq->request_id());
#ifdef USE_MACS
	if (!breq->verify())
	{
#ifdef TRACES
	    fprintf(stderr, "R_Replica[%d]: verify of request for batching returned false\n", id());
#endif
	    delete breq;
	    candidate_index++;
	    continue;
	}
#endif
#ifndef USE_CHECKPOINTS
	neuter_older_requests(breq);
#endif
	memcpy((char*)(req->contents()+orig_req_size), breq->contents(), breq->size());
	orig_req_size += ALIGNED_SIZE(breq->size());
	req->set_batched();
	candidate_index++;
	batched_index++;
	delete breq;

	total_reqs_batched++;
    }

    // fprintf(stderr, "Msg size is now %d, will set to %d\n",req->size(), orig_req_size);
    if (req->is_batched()) {
	req->set_pb_data(orig_req_size, batched_index);
	req->set_size(orig_req_size);
	successful_batching++;
    } else {
	req->set_pb_data(orig_req_size, 1);
    }
}
#endif

void R_Replica::requests_from_predecessor_handler()
{
	int socket = -1;

	listen(in_socket, 1);
	socket = accept(in_socket, NULL, NULL);
	if (socket < 0)
	{
		perror("Cannot accept connection \n");
		exit(-1);
	}

	fd_set socks;
	int readsocks;
	struct timeval timeout;
	// Loop to receive messages.
	while (1)
	{

		R_Message* m = R_Node::recv(socket);
		if (m == NULL) {
		    FD_ZERO(&socks);
		    FD_SET(socket, &socks);
		    timeout.tv_sec = 1;
		    timeout.tv_usec = 0;
		    readsocks = select(socket+1, &socks, (fd_set*)0, (fd_set*)0, &timeout);
		    if (readsocks < 0)
		    {
			perror("select in requests_from_predecessor_handler");
			exit(1);
		    }
		    continue;
		}
#ifdef TRACES
		fprintf(stderr, "Received message %d\n", socket);
#endif
			// Enqueue the message
			pthread_mutex_lock(&incoming_queue_mutex);
			{
				incoming_queue.append(m);
				UPDATE_IN(R,PRE,m->size());
				incoming_queue_signaler[0] = 1;
				pthread_cond_signal(&not_empty_incoming_queue_cond);
			}
			pthread_mutex_unlock(&incoming_queue_mutex);
	}
}

void R_Replica::handle(R_Message *m)
{
	// TODO: This should probably be a jump table.
	switch (m->tag())
	{
		case R_Request_tag:
			ENTRY_TIME_POS(0);
			gen_handle<R_Request>(m);
			EXIT_TIME_POS(0);
			break;

		case R_ACK_tag:
			ENTRY_TIME_POS(1);
			gen_handle<R_ACK>(m);
			EXIT_TIME_POS(1);
			break;

		case R_Checkpoint_tag:
			gen_handle<R_Checkpoint>(m);
			break;

		default:
			// Unknown message type.
			delete m;
	}
}

bool R_Replica::upper_handle(R_Request **oreq)
{
	R_Request *req = *oreq;

#ifdef TRACES
	int cid = req->client_id();
	//fprintf(stderr, "*********** R_Replica %d: Receiving request to handle <%d,%lld>, batched %d, piggybacked %d, dpb %d\n", id(), cid, req->request_id(), req->is_batched(), req->is_piggybacked(), req->is_double_piggybacked());
	fprintf(stderr, "*********** R_Replica %d: Receiving request to handle <%d,%lld>\n", id(), cid, req->request_id());
#endif

#if USE_MACS
	if (!req->verify())
	{
#ifdef TRACES
		fprintf(stderr, "R_Replica[%d]: verify returned FALSE\n", id());
#endif
		delete req;
		return false;
	}
#endif
#ifdef TRACES
	fprintf(stderr, "*********** R_Replica %d: message verified\n", id());
#endif

	return true;
}

Seqno R_Replica::get_seqno_for_request(R_Request *req)
{
	bool oldreq = replies.reply(req->client_id())->request_id() >= req->request_id();
	if (oldreq) {
		return replies.reply(req->client_id())->seqno();
	} else {
	    seqno++;
		return seqno;
	}
}

void R_Replica::assign_sequence(R_Request *req, bool iterate)
{
	if (0 == id()) {
	    // should set both seqno, and sequenced
	    //fprintf(stderr, "R_Replica[%d]: setting seqno to %lld\n", id(), seqno);
		Seqno myseqno = get_seqno_for_request(req);
		req->set_seqno(myseqno);
	}
}

// Updates the state for all batched requests...
void R_Replica::create_state_for_request(R_Request *req)
{
	int cid;
	int pos = 0;
	int index, offset;
	R_Message_Id main_rmid;

	cid = req->client_id();
	R_Message_Id rmid(cid, req->request_id());

	R_Message_State pending_req;
	pending_req.req = req; // always point to the first request...
	pending_req.state = (req->is_sequenced() && req->seqno() <= last_executed)?state_exec_not_acked:state_pending;
	pending_req.refcount = 1;// (pos)?1:index;
	pending_req.pos = pos;
	pending_req.rmid = main_rmid;
	//pending_req.my_rmid = rmid;

	if (pos != 0) {
		R_Message_State &main_rms = requests[main_rmid];
		main_rms.refcount++;
		requests[main_rmid] = main_rms;
	}

	// now, the trickery.
	// if the request is re-transmit, then we need to destroy the old one (if it isn't the first one in the batch)
	// and decrement the refcount. If the request is the carrier (ie, first in the batch), then sux! Split it into peaces,
	// and install each sub-requests into requests.
	if (has_request_state(req)) { // ie. requests exists, and it is new
		//fprintf(stderr, "R_Replica[%d]: found old request, splitting for client %d, post %d\n", id(), creq->client_id(), requests[rmid].pos);
		rms_remove(requests[rmid]);
		requests[rmid] = pending_req;
	}
	requests[rmid] = pending_req;

	// if we didn't execute the request,
	// store it in the list
	if (req->is_sequenced() && req->seqno() > last_executed) {
		R_Exec_Context ec(req->seqno(), rmid);
		execution_queue.push(ec);
	}
}

bool R_Replica::transform_handle(R_Request *req)
{
    int cid = req->client_id();

    R_ACK ack(cid, req->replica_id(), req->seqno(), req->request_id(), req->digest());
    ack.set_type(true);
    ack.just_created = true;
#ifdef USE_MACS
    ack.take_authenticators(req);
#endif

    // and send it...
#ifdef USE_MACS
    ack.authenticate(&replies);
#endif

#ifdef REQ_ACK_DEBUG
    fprintf(stderr, "R_Replica[%d]: send ack <%d,%lld, %lld, %d>\n", id(), ack.client_id(), ack.request_id(), ack.seqno(), ack.replica_id());
#endif

    int len = ack.size();
    send_all(out_socket, ack.contents(), &len);
    UPDATE_OUT(R,PRE,len);
#ifdef TRACES
    fprintf(stderr, "R_Replica %d: Sending ACK of size %d (client %d)\n", id(), len, ack.client_id());
#endif

    return true;
}

bool R_Replica::forward_handle(R_Request *req)
{
#if USE_MACS
    req->authenticate(&replies);
#endif

#ifdef REQ_ACK_DEBUG
    fprintf(stderr, "R_Replica[%d]: forward request <%d,%lld, %lld, %d>\n", id(), req->client_id(), req->request_id(), req->seqno(), req->replica_id());
#endif

    int len = req->size();
    send_all(out_socket, req->contents(), &len);
    UPDATE_OUT(R,PRE,len);
    //if (req->is_piggybacked())
    //fprintf(stderr, "R_Replica %d: Forwarding the request of size %d\n", id(), len);
    return true;
}

// removes the rms from the system...
void R_Replica::rms_remove(R_Message_State rms)
{
    rms.refcount--;
    // R_Request *areq = rms.req->at(rms.pos).data.contained.req;
    if (rms.refcount == 0) {
	// if it is a child of the main rms,
	// remove it first, and then proceed to the parent...
	requests.erase(rms.my_rmid);

	//fprintf(stderr, "R_Replica[%d]: removing rms <%d,%lld>\n", id(), rms.my_rmid.first, rms.my_rmid.second);
	if (rms.pos != 0) {
	    R_Message_State mrms = requests[rms.rmid];
	    rms_remove(mrms);
	} else {
	    if (rms.req) {
		delete rms.req;
		rms.req = NULL;
	    }
	}
	return;
    }
    requests[rms.my_rmid] = rms;
}


void R_Replica::neuter_older_requests(R_Request *breq)
{
#ifndef USE_CHECKPOINTS
    Request_id oldrid = replies.reply(breq->client_id())->request_id();
    bool oldreq = false;
    if (oldrid >= breq->request_id())
    {
	oldreq = true;
    }

    if (!oldreq) {
	R_Message_Id oldrmid(breq->client_id(), oldrid);
	if (requests.find(oldrmid) != requests.end()) {
	    R_Message_State oldrms = requests[oldrmid];
	    rms_remove(oldrms);
	}
	else {
	    //fprintf(stderr, "R_Replica[%d]: did not find old entry from <%d,%lld>\n", id(), req->client_id(), oldrid);
	}
    }
#endif
}

void R_Replica::handle(R_Request *req)
{
	if (!upper_handle(&req))
	    return;

#ifdef REQ_ACK_DEBUG
	fprintf(stderr, "R_Replica[%d]: handle request <%d,%lld, %lld, %d>\n", id(), req->client_id(), req->request_id(), req->seqno(), req->replica_id());
#endif

	// check whether the request is old
	Request_id oldrid = replies.reply(req->client_id())->request_id();
	bool oldreq = false;
	if (oldrid >= req->request_id())
	{
#ifdef TRACES
		fprintf(stderr, "R_Replica[%d]: request <%d,%lld> older than already seen <%d,%lld>\n", id(), req->client_id(), req->request_id(), req->client_id(), oldrid);
#endif
		if (oldrid > req->request_id()) {
			delete req;
			return;
		}
		oldreq = true;
	}

	// time to get other requests
	// and only if the request came directly from the client.
	Execution exr = execution_wait;

	//fprintf(stderr, "R_Replica[%d]: req is piggybacked == %d[%d], with size %d\n", id(), req->is_piggybacked(), req->is_double_piggybacked(),  req->size());
	// first, process double piggybacked data, then proceed with piggybacked
	 
	// skip assigning sequence if f=1 and first replica
    //if (f()!=1 || req->replica_id() != 0)
	    assign_sequence(req);

	exr = do_execute(req);

	create_state_for_request(req);

	if (distance(req->replica_id(), id()) == RANSWER) {
		transform_handle(req);
	} else {
		// Forwarding the request
		forward_handle(req);
	}

	return;
}

Execution R_Replica::do_execute(R_Request *areq)
{
    //fprintf(stderr, "R_Replica[%d]: do_execute called for %p, from %d, seqno %lld\n", id(), areq, areq->client_id(), areq->seqno());
    Execution exr = execution_wait;
    if (!areq->is_sequenced())
		return exr;

	execute_request(areq);
    return exr;
}

// Executes a single request
Execution R_Replica::execute_request(R_Request *req)
{
	// check if request has been executed
	if (req->seqno() <= last_executed)
	    return execution_old;

	if (req->seqno() != last_executed + 1) {
	    if (!execution_queue.empty() && execution_queue.top().first != last_executed + 1)
		    return execution_wait;

	    process_execution_queue();

	    if (req->seqno() <= last_executed)
		return execution_ok;
	    else if (req->seqno() != last_executed + 1)
		return execution_wait;
	}

	execute_request_main(req);

	last_executed = req->seqno();

	while (!execution_queue.empty() && execution_queue.top().first < last_executed) {
	    execution_queue.pop();
	}

	process_execution_queue();

	return execution_ok;
}

void R_Replica::process_execution_queue()
{
    while (!execution_queue.empty() && execution_queue.top().first == last_executed + 1) {
		// XXX: change...
    }
}

// executes a single request a'la execute_request, without recursion
Execution R_Replica::process_execution_queue_helper(R_Request *req)
{
	// check if request has been executed
	if (req->seqno() <= last_executed)
	    return execution_old;

	if (req->seqno() != last_executed + 1) {
	    return execution_wait;
	}

	execute_request_main(req);

	last_executed = req->seqno();

	while (!execution_queue.empty() && execution_queue.top().first < last_executed) {
	    execution_queue.pop();
	}

	return execution_ok;
}


// Actual execution of the request...
// Prepares the reply, calls exec_command
void R_Replica::execute_request_main(R_Request *req)
{
	int cid = req->client_id();
	// Execute the request
	Byz_req inb;
	Byz_rep outb;
	inb.contents = req->command(inb.size);
	outb.contents = replies.new_reply(cid, outb.size);

	// Execute command in a regular request.
	exec_command(&inb, &outb, (Byz_buffer*)&last_executed, cid, req->is_read_only());

#ifdef TRACES
	fprintf(stderr, "*********** R_Replica %d: commmand executed\n", id());
#endif
	if (outb.size % ALIGNMENT_BYTES)
	{
		for (int i=0; i < ALIGNMENT_BYTES - (outb.size % ALIGNMENT_BYTES); i++)
		{
			outb.contents[outb.size+i] = 0;
		}
	}

	// Finish constructing the reply.
	replies.end_reply(cid, req->replica_id(), req->request_id(), req->seqno(), outb.contents, outb.size);
}


void R_Replica::send_reply_to_client(R_BaseRequest *req)
{
	int cid = req->client_id();
	// R_Replying to the client
#ifdef TRACES
	fprintf(stderr,
			"R_Replica[%d]: will reply to %d on socket %d for seqno %lld, rid %lld\n",
			id(), cid, connectmap[cid], req->seqno(), req->request_id());
#endif
#ifdef DO_STATISTICS
	int sent =
#endif
	    replies.send_reply(cid, connectmap[cid], req->toauth_pos());
	UPDATE_OUT(C,PRE,sent);
}

bool R_Replica::has_request_state(R_BaseRequest *req) const
{
	R_Message_Id rmid(req->client_id(), req->request_id());

	R_Message_Map::const_iterator it = requests.find(rmid);
	if (it == requests.end()) {
	    return false;
	}

	return true;
}

bool R_Replica::get_request_state(R_Message_Id rmid, R_Message_State &rms) const
{
	R_Message_Map::const_iterator it = requests.find(rmid);
	if (it == requests.end()) {
	    return false;
	}

	rms = it->second;
	return true;
}

bool R_Replica::get_request_state(R_BaseRequest *req, R_Message_State &rms) const
{
	R_Message_Id rmid(req->client_id(), req->request_id());

	R_Message_Map::const_iterator it = requests.find(rmid);
	if (it == requests.end()) {
	    return false;
	}

	rms = it->second;
	return true;
}

bool R_Replica::transform_handle(R_ACK *ack)
{
#ifdef REQ_ACK_DEBUG
  fprintf(stderr, "R_Replica[%d]: time to reply for ack <%d,%lld, %lld, %d>\n", id(), ack->client_id(), ack->request_id(), ack->seqno(), ack->replica_id());
#endif

#ifdef USE_MACS
	//ack->authenticate(&replies);
	gen_auth_for_client(ack, &replies);
#endif
	R_Message_State rms;
	if (unlikely(!get_request_state(ack, rms))) {
		fprintf(stderr,
				"R_Replica[%d]: could not get state for request from %d[%d]\n",
				id(), ack->client_id(), ack->client_id());
		return false;
	} else {
        if (unlikely(rms.state < state_acked)) {
		    fprintf(stderr,
		    		"R_Replica[%d]: received ack which is in state %d < acked from client %d[%d]\n",
		    		id(), rms.state, ack->client_id(), ack->client_id());
		    //exit(1);
		} else {
		    send_reply_to_client(ack);
		    rms.state = state_stable;
		    requests[rms.my_rmid] = rms;

		    add_request_to_history(rms.my_rmid, ack);
		}
	}
	return true;
}

bool R_Replica::forward_handle(R_ACK *ack)
{

#ifdef REQ_ACK_DEBUG
  fprintf(stderr, "R_Replica[%d]: forward ack <%d,%lld, %lld, %d>\n", id(), ack->client_id(), ack->request_id(), ack->seqno(), ack->replica_id());
#endif

  // now, forward ack, if needed
  int len = ack->size();
  //fprintf(stderr, "R_Replica[%d]: forward_handle, ack of size %d\n", id(), len);
  send_all(out_socket, ack->contents(), &len);
  UPDATE_OUT(R,PRE,len);

    // do the cleanup
	R_Message_State rms;
	if (unlikely(!get_request_state(ack, rms))) {
	    fprintf(stderr,
	    		"R_Replica[%d]: could not get state for request from %d[%d]\n",
	    		id(), ack->client_id(), ack->client_id());
	    return false;
	} else {
	    if (unlikely(rms.state < state_acked)) {
			fprintf(stderr,
					"R_Replica[%d]: received ack which is in state %d < acked from client %d[%d]\n",
					id(), rms.state, ack->client_id(), ack->client_id());
			//exit(1);
	    } else {
			rms.state = state_stable;
			requests[rms.my_rmid] = rms;

			add_request_to_history(rms.my_rmid, ack);
	    }
	}

	return true;
}

void R_Replica::handle(R_ACK *ack)
{
	int cid = ack->client_id();
	Digest dig;

#ifdef REQ_ACK_DEBUG
  fprintf(stderr, "R_Replica[%d]: recv ack <%d,%lld, %lld, %d>\n", id(), ack->client_id(), ack->request_id(), ack->seqno(), ack->replica_id());
#endif

#ifdef TRACES
	//fprintf(stderr, "*********** R_Replica %d: Receiving ACK<%d,%lld> to handle batched %d piggybacked %d, dpb %d\n", id(), cid, ack->request_id(), ack->is_batched(), ack->is_piggybacked(), ack->is_double_piggybacked());
	fprintf(stderr, "*********** R_Replica %d: Receiving ACK<%d,%lld> to handle\n", id(), cid, ack->request_id());
#endif

	Request_id oldrid = max(replies.reply(ack->client_id())->request_id(), latest_seen_rid[ack->client_id()]);
	bool oldreq = false;
	if (oldrid > ack->request_id())
	{
		//fprintf(stderr, "R_Replica[%d]: ack <%d,%lld> older than already seen <%d,%lld>\n", id(), ack->client_id(), ack->request_id(), ack->client_id(), oldrid);
		oldreq = true;
		delete ack;
		return;
	}
	R_Request *areq = NULL;
	R_Message_State rms;
	if (!get_request_state(ack, rms)) {
		//fprintf(stderr, "R_Replica[%d]: request not found for <%d,%lld>\n", id(), cid, ack->request_id());
	    delete ack;
	    return;
	}
	areq = rms.req;

#if USE_MACS
	int authentication_offset;
	if (!ack->verify(areq)) {
#ifdef TRACES
		fprintf(stderr, "R_Replica %d: verify of ACK returned FALSE\n", id());
#endif
		R_Message_Id rmid(ack->client_id(), ack->request_id());
		rms_remove(rms);
		delete ack;
		return;
	}
#endif

    bool update_rms = false;
    R_Message_State& arms = requests[rms.my_rmid];
    if (arms.state == state_pending) {
        arms.state = state_acked_not_exec;
        fprintf(stderr, "R_Replica[%d]: (1) upgrading request for ack <%d,%lld, %lld, %d>: %d\n", id(), ack->client_id(), ack->request_id(), ack->seqno(), ack->replica_id(), arms.state);
        update_rms = true;
    }

    if (arms.state == state_exec_not_acked) {
        arms.state = state_acked;
        update_rms = true;
    }

    if (arms.state == state_acked_not_exec) {
        Execution exr = execute_request(areq);
        if (exr == execution_ok || exr == execution_old) {
            arms.state = state_acked;
            fprintf(stderr, "R_Replica[%d]: (2) upgrading request for ack <%d,%lld, %lld, %d>: %d\n", id(), ack->client_id(), ack->request_id(), ack->seqno(), ack->replica_id(), arms.state);
        } else if (exr == execution_wait) {
            arms.state = state_acked_not_exec;
            fprintf(stderr, "R_Replica[%d]: (3) request cannot be executed for ack <%d,%lld, %lld, %d>: %d\n", id(), ack->client_id(), ack->request_id(), ack->seqno(), ack->replica_id(), arms.state);
            R_Exec_Context ec(areq->seqno(), arms.my_rmid);
            execution_queue.push(ec);
        }
        update_rms = true;
    }

    if (update_rms) {
        requests[rms.my_rmid] = arms;

        //TODO: DEBUG, print the rms
        R_Message_State arms2;
        if (!get_request_state(ack, arms2)) {
            fprintf(stderr, "STRANGE BUG oO\n");
            return;
        }

        fprintf(stderr, "We have updated rms. The new state is %d\n", arms2.state);
    }


#if 0
	    R_Message_State arms;
	    if (unlikely(!get_request_state(ack, arms))) {
			//fprintf(stderr, "R_Replica[%d]: could not find the RMS for request from client %d\n", id(), ack->client_id());
			return;
	    }
	    if (ack->seqno()==0 && f()==1 && areq->replica_id() == 0 && id() == 0) {
			if (areq->seqno() == 0) {
		    	assign_sequence(areq, false);
				ack->set_seqno(areq->seqno());
	    	} else {
				areq->set_seqno(ack->seqno());
	    	}
		}

        if (arms.state <= state_acked_not_exec) {
            Execution exr = execute_request(areq);
            if (exr == execution_ok || exr == execution_old)
                arms.state = state_acked;
            else if (exr == execution_wait) {
                arms.state = state_acked_not_exec;
                //fprintf(stderr, "R_Replica[%d]: There is a problem, request could not be executed (last_executed = %lld, seqno = %lld)\n", id(), last_executed, areq->seqno());
                R_Exec_Context ec(areq->seqno(), arms.my_rmid);
                execution_queue.push(ec);
            }
        } else if (arms.state < state_stable)
            arms.state = state_acked;

	    requests[arms.my_rmid] = arms;
	//}
#endif

#ifdef USE_MACS
	R_Reply* rep = replies.reply(rms.req->client_id());
	ack->authenticate(&replies);
#endif

	if (distance(rms.req->replica_id(), id()) == RANSWER) {
	    transform_handle(ack);
	} else {
	    forward_handle(ack);
	}

	delete ack;

#ifdef USE_CHECKPOINTS
	// send the checkpoint if necessary
	if (rh.should_checkpoint()) {
	    R_Checkpoint *chkp = new R_Checkpoint();
	    Seqno chkp_seqno;
	    Digest chkp_digest;
	    if (!rh.get_next_checkpoint_data(chkp_seqno, chkp_digest)) {
		fprintf(stderr, "Could not get checkpoint data\n");
		return;
	    }
	    // fill in the checkpoint message
	    chkp->set_seqno(chkp_seqno);
	    chkp->set_digest(chkp_digest);
	    // sign
	    R_node->gen_signature(chkp->contents(), sizeof(R_Checkpoint_rep),
		    chkp->contents()+sizeof(R_Checkpoint_rep));
	    // add it to the store
	    R_CheckpointSet *checkpoints = NULL;
	    if (!checkpoint_store.find(chkp->get_seqno(), &checkpoints)) {
		checkpoints = new R_CheckpointSet(n());
		checkpoints->store(chkp);
		checkpoint_store.add(chkp->get_seqno(), checkpoints);
#ifdef TRACES
		fprintf(stderr, "R_Replica[%d]: checkpoint seqno %lld added to the list\n", id(), chkp->get_seqno());
#endif
	    } else {
#ifdef TRACES
		fprintf(stderr, "R_Replica[%d]: checkpoint set already exists for seqno %lld\n", id(), chkp->get_seqno());
#endif
		checkpoints->store(chkp);
	    }
	    // send it
	    int len = chkp->size();
	    send_all(out_socket, chkp->contents(), &len);
	}
#endif
	return;
}

void R_Replica::handle(R_Checkpoint *c)
{
#ifdef USE_CHECKPOINTS
    // drop the checkpoint we created
    if (c->id() == id()) {
	delete c;
	return;
    }

    // verify signature
    if (!c->verify()) {
	fprintf(stderr, "Couldn't verify the signature of R_Checkpoint\n");
	delete c;
	return;
    }

    fprintf(stderr, "R_Replica[%d]: got checkpoint from %d [%lld]\n", id(), c->id(), c->get_seqno());
    // optimization: if less than last removed, discard
    if (checkpoint_store.last() != 0 && c->get_seqno() < checkpoint_store.last())
    {
#ifdef TRACES
	fprintf(stderr, "Checkpoint is for older than last removed, discarding\n");
#endif
	delete c;
	return;
    }

    // store
    R_CheckpointSet *checkpoints;
    if (!checkpoint_store.find(c->get_seqno(), &checkpoints)) {
#ifdef TRACES
    	fprintf(stderr, "Creating new entry in the store, for seqno %lld\n", c->get_seqno());
#endif
	checkpoints = new R_CheckpointSet(n());
	checkpoints->store(c);
	checkpoint_store.add(c->get_seqno(), checkpoints);
    } else {
	checkpoints->store(c);
    }

    // forward the message...
    int len = c->size();
    send_all(out_socket, c->contents(), &len);

    // check whether full
    // if so, clear, and truncate history
    if (checkpoints->size() == n()) {
	bool same = false;
	for (int i=0; i<n(); i++) {
	    R_Checkpoint *cc = checkpoints->fetch(i);
	    same = c->match(cc);
	    if (!same)
		break;
	}
	// we should now truncate the history
	if (same) {
	    checkpoint_store.remove(c->get_seqno());
	    rh.truncate_history(c->get_seqno());
	}
    }
#else /* USE_CHECKPOINTS */
    delete c;
#endif
}

const int MAX_EPOLL_EVENTS_PER_RUN=MAX_CONNECTIONS;
const int EPOLL_RUN_TIMEOUT=10;
const int POLL_RUN_TIMEOUT=500;
const int MAX_LISTEN_CONNECTIONS=2048;

void R_Replica::c_client_requests_handler_epoll()
{
	int res = listen(in_socket_for_clients, MAX_LISTEN_CONNECTIONS);
	if (res < 0) {
	    perror("listen failed:\n");
	    exit(1);
	}

	struct epoll_event *ev = (struct epoll_event *)malloc(sizeof(struct epoll_event));
	ev->events = EPOLLIN | EPOLLPRI | EPOLLERR | EPOLLHUP;
	ev->data.fd = in_socket_for_clients;
	res = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, in_socket_for_clients, ev);
	if (res < 0) {
	    	fprintf(stderr, "R_Replica[%d]: problem setting up the epoll event for in_socket_for_clients %d\n", id(),in_socket_for_clients);
		perror("epoll_ctl");
	    }


	struct epoll_event *events;
	events = (struct epoll_event*)malloc(sizeof(struct epoll_event)*MAX_EPOLL_EVENTS_PER_RUN);
	if (events == NULL) {
	    fprintf(stderr, "Problem allocating memory for epoll events\n");
	    exit(1);
	}
	bzero(events, sizeof(struct epoll_event)*MAX_EPOLL_EVENTS_PER_RUN);
	const int MAX_QUEUE_AT_TIME=10;
	R_Message *msgs[MAX_QUEUE_AT_TIME];

	while (1)
	{
	    // check which connections have data available
	    int nfds = epoll_wait(epoll_fd, events,
		    MAX_EPOLL_EVENTS_PER_RUN,
		    EPOLL_RUN_TIMEOUT);
	    if (nfds < 0) {
		if (errno == EINTR)
		    continue;

		fprintf(stderr, "R_Replica[%d]: error in epoll_wait\n", id());
		perror("epoll_wait:");
		exit(1);
	    } else if (nfds == 0) {
		continue;
	    }

	    // Run through the sockets and check to see if anything
	    // happened with them, if so 'service' them
	    int msgs_index = 0;
	    for (int i = 0; i<nfds; i++) {
		int err;
		int fd = events[i].data.fd;

		uint32_t revents = events[i].events;
		bool poll_have_data = ((revents&EPOLLIN) || (revents&EPOLLPRI));
		bool poll_err = ((revents&EPOLLERR) || (revents&EPOLLHUP));

		if (unlikely(fd == in_socket_for_clients)) {
		    if(!poll_err && poll_have_data)
			handle_new_connection();
		    continue;
		}

		if (poll_err) {
		    res = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
		    if (res < 0) {
			fprintf(stderr, "R_Replica[%d]: problem with removing epoll event", id());
			perror("epoll_ctl_del");
		    }
		    continue;
		}

		if (!poll_have_data) {
		    continue;
		}

		R_Message* m = R_Node::recv(fd, &err);
		if (m == NULL) {
		    //fprintf(stderr, "Miss from %d, %d\n", it->first, it->second);
			if (err == 0) {
				// signal to remove the other party from list of fds
				res = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
				if (res < 0) {
					fprintf(stderr, "R_Replica[%d]: problem with removing epoll event", id());
					perror("epoll_ctl_del");
				}
			}
		    continue;
		}
		msgs[msgs_index++] = m;
		if (msgs_index == MAX_QUEUE_AT_TIME
			|| (msgs_index!=0 && i==(nfds-1))) {
		    pthread_mutex_lock(&incoming_queue_mutex);
		    {
			for (int j=0; j<msgs_index; j++) {
			    incoming_queue_clients.append(msgs[j]);
			    UPDATE_IN(C,PRE,(msgs[j])->size());
			}
			incoming_queue_signaler[1] = 1;
			pthread_cond_signal(&not_empty_incoming_queue_cond);
		    }
		    pthread_mutex_unlock(&incoming_queue_mutex);
		    msgs_index=0;
		}
	    }
	    if (msgs_index != 0) {
		    pthread_mutex_lock(&incoming_queue_mutex);
		    {
			for (int j=0; j<msgs_index; j++) {
			    incoming_queue_clients.append(msgs[j]);
			    UPDATE_IN(C,PRE,(msgs[j])->size());
			}
			incoming_queue_signaler[1] = 1;
			pthread_cond_signal(&not_empty_incoming_queue_cond);
		    }
		    pthread_mutex_unlock(&incoming_queue_mutex);
		    msgs_index=0;
	    }
	}
	pthread_exit(NULL);
}

#ifdef USE_SELECT_EVENT_LOOP
void R_Replica::c_client_requests_handler_select()
{
    struct timeval timeout;
    int readsocks;
    fd_set socks;

    int res = listen(in_socket_for_clients, MAX_LISTEN_CONNECTIONS);
    if (res < 0) {
	perror("listen failed:\n");
	exit(1);
    }

    const int MAX_QUEUE_AT_TIME=10;
    R_Message *msgs[MAX_QUEUE_AT_TIME];

    int highsock = in_socket_for_clients;
    while (1)
    {
	FD_ZERO(&socks);
	FD_SET(in_socket_for_clients, &socks);

	highsock = in_socket_for_clients;
	// Loop through all the possible connections and add
	// those sockets to the fd_set
	google::dense_hash_map<int,int>::iterator it;
	for (it = select_connectmap.begin(); it != select_connectmap.end(); it++) {
	    FD_SET(it->second, &socks);
	    if (it->second > highsock)
		highsock = it->second;
	}

	timeout.tv_sec = 1;
	timeout.tv_usec = 0;
	readsocks = select(highsock + 1, &socks, (fd_set *) 0, (fd_set *) 0, &timeout);
	if (readsocks < 0)
	{
	    fprintf(stderr, "select returned <0");
	    exit(1);
	}
	if (readsocks == 0)
	{
	    continue;
	}

	if (unlikely(FD_ISSET(in_socket_for_clients, &socks)))
	{
	    handle_new_connection();
	}
	// Run through the sockets and check to see if anything
	// happened with them, if so 'service' them
	int msgs_index = 0;
	for (it = connectmap.begin(); it != connectmap.end(); it++) {
	    if (FD_ISSET(it->second, &socks))
	    {
		int err;
		R_Message* m = R_Node::recv(it->second, &err);
		if (m == NULL) {
		    //fprintf(stderr, "Miss from %d, %d\n", it->first, it->second);
		    if (err == 0)
			select_connectmap.erase(it);
		    continue;
		}
#if 0
		pthread_mutex_lock(&incoming_queue_mutex);
		{
		    //fprintf(stderr, "Got the mutex, appending to queue from %d, on %d\n", it->first, it->second);
		    incoming_queue_clients.append(m);
		    UPDATE_IN(C,PRE,m->size());
		    incoming_queue_signaler[1] = 1;
		    pthread_cond_signal(&not_empty_incoming_queue_cond);
		}
		pthread_mutex_unlock(&incoming_queue_mutex);
#endif
		msgs[msgs_index++] = m;
		if (msgs_index == MAX_QUEUE_AT_TIME) {
		    pthread_mutex_lock(&incoming_queue_mutex);
		    {
			for (int j=0; j<msgs_index; j++) {
			    incoming_queue_clients.append(msgs[j]);
			    UPDATE_IN(C,PRE,(msgs[j])->size());
			}
			incoming_queue_signaler[1] = 1;
			pthread_cond_signal(&not_empty_incoming_queue_cond);
		    }
		    pthread_mutex_unlock(&incoming_queue_mutex);
		    msgs_index=0;
		}
	    }
	}
	if (msgs_index != 0) {
	    pthread_mutex_lock(&incoming_queue_mutex);
	    {
		for (int j=0; j<msgs_index; j++) {
		    incoming_queue_clients.append(msgs[j]);
		    UPDATE_IN(C,PRE,(msgs[j])->size());
		}
		incoming_queue_signaler[1] = 1;
		pthread_cond_signal(&not_empty_incoming_queue_cond);
	    }
	    pthread_mutex_unlock(&incoming_queue_mutex);
	    msgs_index=0;
	}
    }
    pthread_exit(NULL);
}
#endif

#ifdef USE_POLL_EVENT_LOOP
void R_Replica::c_client_requests_handler_poll()
{
	int res = listen(in_socket_for_clients, MAX_LISTEN_CONNECTIONS);
	if (res < 0) {
	    perror("listen failed:\n");
	    exit(1);
	}

	pollfds[num_of_pollfds].fd = in_socket_for_clients;
	pollfds[num_of_pollfds].events = POLLIN | POLLPRI | POLLERR | POLLRDHUP | POLLHUP;
	pollfds[num_of_pollfds].revents = 0;
	num_of_pollfds++;

	const int MAX_QUEUE_AT_TIME=10;
	R_Message *msgs[MAX_QUEUE_AT_TIME];

	while (1)
	{
	    // check which connections have data available
	    int nfds = poll(pollfds, num_of_pollfds, POLL_RUN_TIMEOUT);
	    if (nfds < 0) {
		if (errno == EINTR)
		    continue;

		fprintf(stderr, "R_Replica[%d]: error in poll_wait\n", id());
		perror("poll_wait:");
		exit(1);
	    } else if (nfds == 0) {
		continue;
	    }

	    // Run through the sockets and check to see if anything
	    // happened with them, if so 'service' them
	    int msgs_index = 0;
	    for (int i = 0; i<num_of_pollfds; i++) {
		int err;
		int fd = pollfds[i].fd;
		short revents = pollfds[i].revents;

		bool poll_have_data = ((revents&POLLIN) || (revents&POLLPRI));
		bool poll_err = ((revents&POLLERR) || (revents&POLLHUP) || (revents&POLLRDHUP));

		if (unlikely(fd == in_socket_for_clients)) {
		    if (!poll_err && poll_have_data)
			handle_new_connection();
		    continue;
		}

		if (poll_err) {
		    pollfds[i] = pollfds[num_of_pollfds-1];
		    num_of_pollfds--;

		    i--;
		    res = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
		    if (res < 0) {
			fprintf(stderr, "R_Replica[%d]: problem with removing epoll event", id());
			perror("epoll_ctl_del");
		    }
		    continue;
		}

		if (!poll_have_data) {
		    continue;
		}

		R_Message* m = R_Node::recv(fd, &err);
		if (m == NULL) {
		    //fprintf(stderr, "Miss from %d, %d\n", it->first, it->second);
			if (err == 0) {
				// signal to remove the other party from list of fds
				pollfds[i] = pollfds[num_of_pollfds-1];
				num_of_pollfds--;
				i--;

				res = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
				if (res < 0) {
					fprintf(stderr, "R_Replica[%d]: problem with removing epoll event", id());
					perror("epoll_ctl_del");
				}
			}
		    continue;
		}
		msgs[msgs_index++] = m;
		if (msgs_index == MAX_QUEUE_AT_TIME
			|| (msgs_index!=0 && i==(nfds-1))) {
		    pthread_mutex_lock(&incoming_queue_mutex);
		    {
			for (int j=0; j<msgs_index; j++) {
			    incoming_queue_clients.append(msgs[j]);
			    UPDATE_IN(C,PRE,(msgs[j])->size());
			}
			incoming_queue_signaler[1] = 1;
			pthread_cond_signal(&not_empty_incoming_queue_cond);
		    }
		    pthread_mutex_unlock(&incoming_queue_mutex);
		    msgs_index=0;
		}
	    }
	    if (msgs_index != 0) {
		    pthread_mutex_lock(&incoming_queue_mutex);
		    {
			for (int j=0; j<msgs_index; j++) {
			    incoming_queue_clients.append(msgs[j]);
			    UPDATE_IN(C,PRE,(msgs[j])->size());
			}
			incoming_queue_signaler[1] = 1;
			pthread_cond_signal(&not_empty_incoming_queue_cond);
		    }
		    pthread_mutex_unlock(&incoming_queue_mutex);
		    msgs_index=0;
	    }
	}
	pthread_exit(NULL);
}
#endif

void R_Replica::handle_new_connection()
{
	int connection;

	connection = accept(in_socket_for_clients, NULL, NULL);
	if (connection < 0)
	{
	    perror("problem accept");
	    exit(EXIT_FAILURE);
	}
	fprintf(stderr, "ACCEPTED\n");

	int flag = 1;
	int result = setsockopt(connection, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int));
	result = setsockopt(connection, IPPROTO_TCP, SO_REUSEADDR, (char *) &flag, sizeof(int));
	int client_id = -1;
	ssize_t res = 0;
	res = ::recv(connection, &client_id, sizeof(client_id), 0);
	if (res < sizeof(client_id)) {
	    fprintf(stderr, "problem fetching client id (%d < %d)\n", res, sizeof(client_id));
		close(connection);
		return;
	}
	fprintf(stderr, "Client is %d\n", client_id);

	if (connectmap.find(client_id) != connectmap.end()) {
	    fprintf(stderr, "handle_new_connection: problem, client asks again %d\n", client_id);
	    res = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, connectmap[client_id], NULL);
	    if (res < 0) {
		fprintf(stderr, "R_Replica[%d]: problem with removing epoll event", id());
		perror("epoll_ctl_del");
	    }
	    shutdown(connectmap[client_id], SHUT_RDWR);
	    close(connectmap[client_id]);
	    connectmap.erase(client_id);
	}
	char direction = 'x';
	res = ::recv(connection, &direction, sizeof(direction), 0);
	if (res < sizeof(direction)) {
	    fprintf(stderr, "problem fetching client direction (%d < %d)\n", res, sizeof(direction));
		close(connection);
		return;
	}
	res = ::send(connection, &direction, sizeof(direction), 0);
	if (res < sizeof(direction))
	    fprintf(stderr, "problem responding back to the client about direction\n");

	setnonblocking(connection);
	if (connectmap.size() <  MAX_CONNECTIONS) {
	    fprintf(stderr, "\nR_Replica[%d]: Connection accepted:   FD=%d; client_id=%d, direction=%c\n", id(), connection, client_id, direction);

	    // setup epoll events, only if direction is 'o'
	    if (direction != 'o') {
		connectmap[client_id] = connection;
		return;
	    }
	    struct epoll_event *ev = (struct epoll_event *)malloc(sizeof(struct epoll_event));
	    ev->events = EPOLLIN | EPOLLPRI | EPOLLERR | EPOLLHUP | EPOLLET;
	    ev->data.fd = connection;

#ifdef USE_POLL_EVENT_LOOP
	    pollfds[num_of_pollfds].fd = connection;
	    pollfds[num_of_pollfds].events = POLLIN | POLLPRI | POLLERR | POLLRDHUP | POLLHUP;
	    pollfds[num_of_pollfds].revents = 0;
	    num_of_pollfds++;
#endif
#ifdef USE_SELECT_EVENT_LOOP
	    select_connectmap[client_id] = connection;
#endif

	    int res = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, connection, ev);
	    if (res == 0) {
		// XXX: should check and close if there is a connection for the client
		connectmap[client_id] = connection;
		epoll_connectmap[client_id] = ev;
	    } else {
	    	fprintf(stderr, "R_Replica[%d]: problem setting up the epoll event for client %d\n", id(), client_id);
		perror("epoll_ctl");
	    }
	} else {
	    fprintf(stderr, "\nERROR: (problem) No room left for new client.\n");
	    close(connection);
	}
}

void R_Replica::add_request_to_history(R_Message_Id rmid, R_BaseRequest *req)
{
#ifdef USE_CHECKPOINTS
	Digest d;
	rh.add_request(req, rmid, req->seqno(), d);
#endif
}


