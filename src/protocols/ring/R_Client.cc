#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>

#include "th_assert.h"
#include "R_Client.h"
#include "R_Message.h"
#include "R_Reply.h"
#include "R_Request.h"

//#define TRACES

static void*replies_handler_helper(void *o)
{
	void **o2 = (void **)o;
	R_Client &r = (R_Client&)(*o2);
	r.replies_handler();
	return 0;
}

static void* itimer_helper(void *o)
{
	struct timespec st;
	st.tv_sec = 0;
	st.tv_nsec = 100000000;

	while (1) {
	    R_ITimer::handle_timeouts();
	    nanosleep(&st, NULL);
	}
	return NULL;
}

R_Client::R_Client(FILE *config_file, FILE *config_priv, char* host_name, short port) :
	R_Node(config_file, config_priv, host_name, port)
{
	// Fail if R_node is is a replica.
	if (is_replica(id()))
	{
		th_fail("R_Node is a replica");
	}
	R_node=this;
	node=this;
	//  out_rid = new_rid();
	//out_req = 0;

	malicious = false;

	pthread_mutex_init(&outgoing_req_mutex, NULL);

	out_rid = new_rid();
	out_req = 0;
	servicing_read_only = false;
	nb_received_requests = 0;

	n_retrans = 0;
#ifdef ADAPTIVE_RTIMEOUT
	rtimeout = Max_rtimeout;
#else
	rtimeout = 500;
#endif
	rtimer = new R_ITimer(rtimeout, R_rtime_handler);

	entry_replica = (id())%num_replicas;

	// Connect to primary (principals[0])
#ifdef REPLY_BY_ENTRY
	fprintf(stderr, "R_Client[%d]: trying to connect to %lx:%d as head and tail\n", id(), principals[entry_replica]->TCP_addr_for_clients.sin_addr.s_addr, principals[entry_replica]->TCP_addr_for_clients.sin_port);
#else
	fprintf(stderr, "R_Client[%d]: trying to connect to %lx:%d as head\n", id(), principals[entry_replica]->TCP_addr_for_clients.sin_addr.s_addr, principals[entry_replica]->TCP_addr_for_clients.sin_port);
#endif
	socket_to_my_primary = createClientSocket(principals[entry_replica]->TCP_addr_for_clients);
	int cid = id();
	int res = ::send(socket_to_my_primary, (const void *)&cid, sizeof(cid), 0);
	if (res < sizeof(cid)) {
	    fprintf(stderr, "R_Client[%d]: problem sending id to the replica at head\n", id());
	    perror("sendto:");
	}
	char direction = 'o';
#ifdef REPLY_BY_ENTRY
	direction = 'x'; // this is the signal that connection is both way
#endif
	res = ::send(socket_to_my_primary, (const void *)&direction, sizeof(direction), 0);
	if (res < sizeof(direction)) {
	    fprintf(stderr, "R_Client[%d]: problem sending direction to the replica at head\n", id());
	    perror("sendto:");
	}
	res = ::recv(socket_to_my_primary, (void *)&direction, sizeof(direction), 0);
	if (res < sizeof(direction)) {
	    fprintf(stderr, "R_Client[%d]: problem receiving direction from the head\n", id());
	    perror("recvfrom:");
	    exit(1);
	}

	/*   int flag = 1;
	 int result = setsockopt(socket_to_primary,IPPROTO_TCP,TCP_NODELAY,(char *)&flag,sizeof(int));
	 if(result<0){

	 fprintf(stderr, "Failed to disable Naggle for socket to primary in the ring\n");
	 exit(1);

	 }
	 */

#ifndef REPLY_BY_ENTRY
	// Connect to last (principals[num_replicas - 1])
	fprintf(stderr, "R_Client[%d]: trying to connect to %lx:%d as tail\n", id(), principals[((entry_replica+num_replicas-1)%num_replicas)]->TCP_addr_for_clients.sin_addr.s_addr, principals[((entry_replica+num_replicas-1)%num_replicas)]->TCP_addr_for_clients.sin_port);

    bool redo = false;
    do {
	redo = false;
	socket_to_my_last
			= createClientSocket(principals[((entry_replica+num_replicas-1)%num_replicas)]->TCP_addr_for_clients);
	res = ::send(socket_to_my_last, (const void *)&cid, sizeof(cid), 0);
	if (res < sizeof(cid)) {
	    fprintf(stderr, "R_Client[%d]: problem sending id to the replica at tail\n", id());
	    perror("sendto:");
	}

	fprintf(stderr, "Sent cid\n");
	direction = 'i';
	res = ::send(socket_to_my_last, (const void *)&direction, sizeof(direction), 0);
	if (res < sizeof(direction)) {
	    fprintf(stderr, "R_Client[%d]: problem sending direction to the replica at tail\n", id());
	    perror("sendto:");
	}

	fprintf(stderr, "Sentdirection\n");

redo_place:
	fd_set socks;
	FD_ZERO(&socks);
	FD_SET(socket_to_my_last, &socks);

	struct timeval timeout;
	timeout.tv_sec = 10;
	timeout.tv_usec = 0;
	int readsocks = 0;
	readsocks=select(socket_to_my_last+1, &socks, (fd_set*)0, (fd_set*)0, &timeout);
	if (readsocks <= 0)
	{
	    fprintf(stderr, "problem with connecting (%d)\n", readsocks);
	    perror("select");
	    goto redo_place;
	    redo = true;
	    shutdown(socket_to_my_last, SHUT_RDWR);
	    close(socket_to_my_last);
	    socket_to_my_last = -1;
	} else {
	    res = ::recv(socket_to_my_last, (void *)&direction, sizeof(direction), 0);
	    if (res < sizeof(direction)) {
	    	fprintf(stderr, "R_Client[%d]: problem receiving direction from the tail\n", id());
	    	perror("recv:");
	    	redo = true;
		close(socket_to_my_last);
		exit(1);
	    }
	}
    } while (redo);

	    fprintf(stderr, "R_Client[%d]: created the connection\n", id());
	/* result = setsockopt(socket_to_last,IPPROTO_TCP,TCP_NODELAY,(char *)&flag,sizeof(int));
	 if(result<0){

	 fprintf(stderr, "Failed to disable Naggle for socket to last in the ring\n");
	 exit(1);

	 }
	 */
#endif

	// now, create connections to random f+1 replica, for read only requests.
	if (pthread_create(&replies_handler_thread, 0, &replies_handler_helper,
			(void *)this) != 0)
	{
		fprintf(stderr, "Failed to create the thread for receiving messages from predecessor in the ring\n");
		exit(1);
	}

	if (pthread_create(&itimer_thread, 0, &itimer_helper, (void*)this) != 0) {
	    fprintf(stderr, "Failed to create the thread for handling itimers\n");
	    exit(1);
	}
}

R_Client::~R_Client()
{
    delete rtimer;
}

void R_Client::close_connections()
{
    close(socket_to_my_primary);
    close(socket_to_my_last);
}

void R_Client::replies_handler()
{
	fd_set socks;
	int readsocks;
	struct timeval timeout;
#ifdef REPLY_BY_ENTRY
	int socket_to_a_replica = socket_to_my_primary;
#else
	int socket_to_a_replica = socket_to_my_last;
#endif

	while (1)
	{
                R_Message* m = R_node->recv(socket_to_a_replica);
		if (m == NULL) {
		    FD_ZERO(&socks);
		    FD_SET(socket_to_a_replica, &socks);

		    timeout.tv_sec = 1;
		    timeout.tv_usec = 0;
		    readsocks = select(socket_to_a_replica+1, &socks, (fd_set*)0, (fd_set*)0, &timeout);
		    if (readsocks < 0)
		    {
			fprintf(stderr, "select in requests_from_predecessor_handler");
			exit(1);
		    }
		    continue;
		}

		// R_Reply* n;
		if (m->tag()==R_Reply_tag)
		{
			// Enqueue the message
			pthread_mutex_lock(&incoming_queue_mutex);
			{
				incoming_queue.append(m);
				pthread_cond_signal(&not_empty_incoming_queue_cond);
			}
			pthread_mutex_unlock(&incoming_queue_mutex);
		} else
		{
			delete m;
		}
	}

}

bool R_Client::send_request(R_Request *req, int size, bool ro)
{
	if (0 && ro)
	{
	    req->set_read_only();
	    servicing_read_only = true;
	}

	pthread_mutex_lock(&outgoing_req_mutex);
	if (out_req == 0)
	{
		// Send request to service
		// read-write requests are sent to the primary only.
#ifdef TRACES
		fprintf(stderr, "Client::send_request (size = %d)\n", req->size());
#endif

#ifdef TRACES
		fprintf(
				stderr,
				"************ INVOKING A NEW REQUEST ************ (out_rid = %llu) (req->rid = %llu) (req->size = %d)\n",
				out_rid, req->request_id(), req->size());
		fprintf(stderr,
				"Client::send_request (out_rid = %llu) (req->rid = %llu)\n",
				out_rid, req->request_id());
#endif

		out_req = req;
		n_retrans = 0;
		req->request_id() = get_rid();
		int len = req->size();

#ifdef USE_MACS
		req->authenticate(NULL);

		if (malicious) {
		    memset(req->toauth_pos(), 0, R_UNonce_size+R_UMAC_size);
		}
#endif

		int err = send_all(socket_to_my_primary, (char *) req->contents(), &len);
		if (malicious) {
		    out_req = 0;
		    out_rid = new_rid();
		    pthread_mutex_unlock(&outgoing_req_mutex);
		    return err != -1;
		}
		pthread_mutex_unlock(&outgoing_req_mutex);
#ifdef ADAPTIVE_RTIMEOUT
		gettimeofday(&ad_start, NULL);
		rtimer->restop();
		if (rtimeout_avg < Max_rtimeout)
		    rtimer->adjust((int)rtimeout_avg);
#else
		rtimer->restop();
#endif
		rtimer->start();
		return  err != -1;
	} else
	{
		// Another request is being processed.
	    pthread_mutex_unlock(&outgoing_req_mutex);
		return false;
	}
}

R_Reply *R_Client::recv_reply()
{
	Digest rod;
	int ro_how_many = 0;

	if (out_req == 0)
	{
		// Nothing to wait for.
		fprintf(stderr, "Client::recv_reply: no outgoing request\n");
		return 0;
	}

	//
	// Wait for reply
	//
	while (1)
	{
		//   R_Message* m = recv();
		R_Message *m;
		//  fprintf(stderr,".");
		pthread_mutex_lock(&incoming_queue_mutex);
		{
			while (incoming_queue.size() == 0)
			{

				pthread_cond_wait(&not_empty_incoming_queue_cond,
						&incoming_queue_mutex);
			}
			m = incoming_queue.remove();
		}
		pthread_mutex_unlock(&incoming_queue_mutex);

		R_Reply* rep;
		if (!R_Reply::convert(m, rep))
		{
			delete m;
			continue;
		}

		pthread_mutex_lock(&outgoing_req_mutex);
		if (rep->request_id() != out_rid)
		{
#ifdef TRACES
			fprintf(stderr, "Client::recv_reply: rid is %llu different from out rid %llu \n",rep->request_id(), out_rid);
#endif
			delete rep;
			pthread_mutex_unlock(&outgoing_req_mutex);
			continue;
		}
		pthread_mutex_unlock(&outgoing_req_mutex);
#ifdef USE_MACS
		if (!rep->verify())
		{
			fprintf(stderr, "Client::recv_reply: verify returns FALSE\n");
			exit(1);
		}
#endif
		nb_received_requests++;

out:
		rtimer->stop();
#ifdef ADAPTIVE_RTIMEOUT
		gettimeofday(&ad_end, NULL);
		if (n_retrans != 0) {
		    double elapsed = (ad_end.tv_sec-ad_start.tv_sec)*1000+(ad_end.tv_usec-ad_start.tv_usec)/1000;
		    if (elapsed >= Min_rtimeout) {
		    	rtimeout_avg = rtimeout_avg*seen_reqs + elapsed;
			seen_reqs++;
		    }
		}
#endif
		n_retrans = 0;
		servicing_read_only = false;
		pthread_mutex_lock(&outgoing_req_mutex);
		out_rid = new_rid();
		out_req = 0;
		pthread_mutex_unlock(&outgoing_req_mutex);
		return rep;
	}
}

void R_Client::retransmit()
{
	// Retransmit any outstanding request.
	static const int nk_thresh = 1000;
	static const int nk_thresh_p = 5;

	pthread_mutex_lock(&outgoing_req_mutex);
	if (out_req != 0)
	{
		//    fprintf(stderr, ".");
		n_retrans++;
		if (n_retrans == nk_thresh)
		{
		    fprintf(stderr, "R_Client[%d]: They are not responding!\n", id());
		    n_retrans = 0;

		    out_req = 0;
		    out_rid = new_rid();
		    pthread_mutex_unlock(&outgoing_req_mutex);
		    return;
		}

		int len = out_req->size();
		int err = send_all(socket_to_my_primary, (char *) out_req->contents(), &len);
	}

#ifdef ADJUST_RTIMEOUT
	// exponential back off
	if (rtimeout < Min_rtimeout) rtimeout = 100;
	rtimeout = rtimeout+lrand48()%rtimeout;
	if (rtimeout> Max_rtimeout) rtimeout = Max_rtimeout;
	rtimer->adjust(rtimeout);
#endif
	pthread_mutex_unlock(&outgoing_req_mutex);

	rtimer->restart();
}

void R_rtime_handler()
{
	th_assert(R_node, "R_Client is not initialized");
	((R_Client*) R_node)->retransmit();
}


