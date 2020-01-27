#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>

#include "Traces.h"
#include "th_assert.h"
#include "Q_Client.h"
#include "Q_Message.h"
#include "Q_Message_tags.h"
#include "Q_Reply.h"
#include "Q_Request.h"

// Force template instantiation
#include "Q_Certificate.t"
template class Q_Certificate<Q_Reply>;

#define printDebug(...) \
    do { \
	struct timeval tv; \
	gettimeofday(&tv, NULL); \
	fprintf(stderr, "%u.%06u: ", tv.tv_sec, tv.tv_usec); \
	fprintf(stderr, __VA_ARGS__); \
    } while (0);
#undef printDebug
#define printDebug(...) \
    do { } while(0);

Q_Client::Q_Client(FILE *config_file, char *host_name, short port) :
	Q_Node(config_file, NULL, host_name, port), r_reps(3*f()+1)
{
	// Fail if node is is a replica.
	if (is_replica(id()))
	{
		th_fail("Node is a replica");
	}

	out_rid = new_rid();
	out_req = 0;

	is_panicking = false;
	bail_out = false;

	n_retrans = 0;
	rtimeout = 500;
	rtimer = new Q_ITimer(rtimeout, Q_rtimeQ_handler);
}

Q_Client::~Q_Client()
{
    delete rtimer;
}

bool Q_Client::send_request(Q_Request *req, int size, bool ro)
{
	if (out_req != 0)
	{
		return false;
	}

#ifdef TRACES
	fprintf(
			stderr,
			"Q_Client::send_request (out_rid = %llu) (req->rid = %llu) (req->size = %d)\n",
			out_rid, req->request_id(), req->size());
#endif

	out_req = req;
	if (id()==5 && ro)
	    req->set_unused();
	req->request_id() = out_rid;
	req->authenticate(size, false);

	r_reps.clear();
	n_retrans = 0;
	is_panicking = false;
  send(req, Q_All_replicas);

#ifdef ADJUST_RTIMEOUT
	// Adjust timeout to reflect average latency
	rtimer->adjust(rtimeout);

	// Start timer to measure request latency
	//latency.reset();
	//latency.start();
#endif
	rtimer->restop();
	rtimer->start();

	return true;
}

Q_Reply *Q_Client::recv_reply()
{
#ifdef TRACES
	fprintf(stderr,"Q_Client:: In recv reply\n");
#endif

	if (out_req == 0)
	{
		// Nothing to wait for.
		fprintf(stderr, "Q_Client::recv_reply: no outgoing request\n");
		return 0;
	}

	// Wait for reply
	while (1)
	{
		Q_Message *m = Q_Node::recv();

		Q_Reply* rep = NULL;
		if (!Q_Reply::convert(m, rep))
		{
			delete m;
			continue;
		}
		if (bail_out) {
		    bail_out = false;
		    is_panicking = false;
		    r_reps.clear();
		    out_req = 0;
		    rep = NULL;
		    return rep;
		}

		if (rep->request_id() != out_rid)
		{
			delete rep;
			continue;
		}

      if (rep->should_switch()) {
         rtimer->stop();
         r_reps.clear();
         out_req = 0;
         out_rid = new_rid();
         return rep;
      }

		if (!r_reps.add(rep, Q_node))
      {
         fprintf(stderr, "Rep does not match!\n");
			delete rep;
			continue;
		}

		if (r_reps.is_complete())
		{
			Q_Reply *r = (r_reps.cvalue()->full()) ? r_reps.cvalue() : 0;

			if (r != 0)
			{
				// We got a reply
				// check to see whether we need to switch
				rtimer->stop();
				out_rid = new_rid();
				if (r->should_switch()) {
				    //fprintf(stderr, "Q_Client[%d]: Should switch\n", id());
				}
				out_req = 0;
				n_retrans = 0;
				r_reps.clear();
				return r;
			}
			fprintf(stderr,
			"Complete certificate without full reply...\n");
		}
		continue;
	}
}

void Q_rtimeQ_handler()
{
	th_assert(Q_node, "Q_Client is not initialized");
	((Q_Client*) Q_node)->retransmit();
}

void Q_Client::retransmit()
{
	// Retransmit any outstanding request.
	static const int nk_thresh = 40;

	if (out_req != 0)
	{
		//    fprintf(stderr, ".");
		n_retrans++;
		send(out_req, Q_All_replicas);
	} else if (is_panicking) {
		n_retrans++;
		if (n_retrans == nk_thresh)
		{
		    fprintf(stderr, "Q_Client[%d]: They are not responding on panic!\n", id());
		    rtimer->stop();
		    n_retrans = 0;

		    // now, cause Q_Node to exit recv, and then to gracefully exit...
		    bail_out = true;
		    u_char yes = 1;
		    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &yes, sizeof(yes));
		    Digest d;
		    d.zero();
		    Q_Reply qr((View)0, out_rid, id(), d, i_to_p(id()), id());
		    qr.set_instance_id(next_instance_id());
		    send (&qr, id());
		    u_char no = 0;
		    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &no, sizeof(no));

		    r_reps.clear();
		    is_panicking = false;
		    return;
		}
	}

#ifdef ADJUST_RTIMEOUT
	// exponential back off
	if (rtimeout < Min_rtimeout) rtimeout = 100;
	rtimeout = rtimeout+lrand48()%rtimeout;
	if (rtimeout> Max_rtimeout) rtimeout = Max_rtimeout;
	rtimer->adjust(rtimeout);
#endif

	rtimer->restart();
}


