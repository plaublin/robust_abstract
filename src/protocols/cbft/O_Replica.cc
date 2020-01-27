#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <map>

#include "th_assert.h"
#include "O_Replica.h"
#include "O_Message_tags.h"
#include "O_Reply.h"
#include "O_Principal.h"
#include "O_Request.h"
#include "O_Missing.h"
#include "O_Get_a_grip.h"
#include "MD5.h"

#include "O_Smasher.h"

#include "Switcher.h"

// Global replica object.
O_Replica *O_replica;

#define printDebug(...) \
    do { \
	struct timeval tv; \
	gettimeofday(&tv, NULL); \
	fprintf(stderr, "%u.%06u: ", tv.tv_sec, tv.tv_usec); \
	fprintf(stderr, __VA_ARGS__); \
    } while (0);
#undef printDebug
#define printDebug(...) \
    do { } while (0);

void switch_o_replica(bool state)
{
    if (O_replica != NULL)
	O_replica->enable_replica(state);
}

/*
void*O_handle_incoming_messages_helper(void *o)
{
	void **o2 = (void **)o;
	O_Replica &r = (O_Replica&) (*o2);
	r.handle_incoming_messages();
	return 0;
}
*/

void O_abort_timeout_handler()
{
th_assert(0, "Why I am here? at O_abort_timeout_handler");
	th_assert(O_replica, "O_replica is not initialized");
	O_replica->retransmit_panic();
}

O_Replica::O_Replica(FILE *config_file, FILE *config_priv, char *host_name, short req_port) :
	O_Node(config_file, config_priv, host_name, req_port), seqno(0), checkpoint_store(),
	cur_state(replica_state_NORMAL),
	aborts(num_replicas, num_replicas),
	ah_2(NULL), missing(NULL), num_missing(0),
	missing_mask(), missing_store(), missing_store_seq(),
	outstanding()
{
	// Fail if node is not a replica.
	if (!is_replica(id()))
	{
		th_fail("Node is not a replica");
	}

	O_replica = this;
	great_switcher->register_switcher(instance_id(), switch_o_replica);

	n_retrans = 0;
	//rtimeout = 10;
	//rtimer = new O_ITimer(rtimeout, O_abort_timeout_handler);

	rh = new Req_history_log<O_Request>();

	replies = new O_Rep_info(num_principals);
	replier = 0;
	nb_retransmissions = 0;

	// Read view change, status, and recovery timeouts from replica's portion
	// of "config_file"
	int vt, st, rt;
	fscanf(config_file, "%d\n", &vt);
	fscanf(config_file, "%d\n", &st);
	fscanf(config_file, "%d\n", &rt);

	// Create timers and randomize times to avoid collisions.
	srand48(getpid());

	//join_mcast_group();

	// Disable loopback
	//u_char l = 0;
	//int error = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &l, sizeof(l));
	//if (error < 0)
	//{
		//perror("unable to disable loopback");
		//exit(1);
	//}

//pthread_t handler_thread;
//
//if (pthread_create(&handler_thread, NULL,
//		&O_handle_incoming_messages_helper, (void *) this) != 0)
//{
//	fprintf(stderr,
//	"Failed to create the thread for receiving client requests\n");
//	exit(1);
//}
   serverListen();
	fprintf(stderr, "Created the thread for receiving client requests\n");
}

O_Replica::~O_Replica()
{
    cur_state = replica_state_STOP;
    //delete rtimer;
    if (missing)
	delete missing;
    if (ah_2)
    	delete ah_2;
    delete replies;
    delete rh;
}

void O_Replica::register_exec(int(*e)(Byz_req *, Byz_rep *, Byz_buffer *, int, bool))
{
	exec_command = e;
}

void O_Replica::register_perform_checkpoint(int(*p)())
{
	perform_checkpoint = p;
}

/*
void O_Replica::handle_incoming_messages()
{
	O_Message* msg;
	while (1)
	{
		msg = O_Node::recv();

		switch (msg->tag())
		{
		    case O_Request_tag:
			gen_handle<O_Request>(msg);
			break;

		    case O_Checkpoint_tag:
			gen_handle<O_Checkpoint>(msg);
			break;

		    case O_Panic_tag:
			gen_handle<O_Panic>(msg);
			break;

		    case O_Abort_tag:
			gen_handle<O_Abort>(msg);
			break;

		    case O_Missing_tag:
			gen_handle<O_Missing>(msg);
			break;

		    case O_Get_a_grip_tag:
			gen_handle<O_Get_a_grip>(msg);
			break;

		    default:
			// Unknown message type.
			delete msg;
		}
	}
}
*/

//#define recv_dbg
//#define maysamdbg

void O_Replica::handle(O_Request *m, int sock) {
#ifdef maysamdbg
	fprintf(stderr, "===Replica: O_Request: id=%d primary=%d m->order=%d totalOrder=%d\n", id(), primary(), m->getOrder(), totalOrder);
	fprintf(stderr, "1");
#endif
//Added by Maysam Yabandeh
   principals[m->client_id()]->remotefd = sock;
   if (id() != primary() && m->getOrder() < 0) //it is not ordered
   {
		fprintf(stderr, "Error: not ordered message in a Replica: O_Request: id=%d primary=%d m->order=%d totalOrder=%d\n", id(), primary(), m->getOrder(), totalOrder);
		delete m;
	    return;
	}
   if (id() == primary() && m->getOrder() >= 0) //it is ordered
   {
		fprintf(stderr, "Error: ordered message in Primary: O_Request: id=%d primary=%d m->order=%d totalOrder=%d\n", id(), primary(), m->getOrder(), totalOrder);
		delete m;
	    return;
	}
	if (id() != primary())
	{
		std::vector<O_Request*>::iterator bi = bufferedRequests.begin();
		for (; bi < bufferedRequests.end(); bi++)
			if ( m->getOrder() == (*bi)->getOrder())
			{
			   //O_Ack ack(m->getOrder(), id());
			   //send(&ack, m->client_id());
				delete m;
			   return;
			}
	}
#ifdef maysamdbg
	fprintf(stderr, "2");
#endif
        // accept only if in normal state
	if (cur_state == replica_state_STOP) {
	    int cid = m->client_id();

	    O_Reply qr(0, m->request_id(), node_id, replies->digest(cid), i_to_p(cid), cid);
	    qr.set_instance_id(chain);
	    qr.authenticate(i_to_p(cid), 0);

	    send(&qr, cid);

	    delete m;
#ifdef maysamdbg
	fprintf(stderr, "3");
#endif
	    return;
	} else if (cur_state != replica_state_NORMAL) {
	    OutstandingRequests outs;
	    outs.cid = m->client_id();
	    outs.rid = m->request_id();
	    outstanding.push_back(outs);
	    delete m;
#ifdef maysamdbg
	fprintf(stderr, "4");
#endif
	    return;
	}
	if (!m->verify())
	{
	    fprintf(stderr, "O_Replica::handle(): request verification failed.\n");
	    delete m;
#ifdef maysamdbg
	fprintf(stderr, "5");
#endif
	    return;
	}

	if (m->is_read_only())
	{
	    fprintf(stderr, "O_Replica::handle(): read-only requests are not handled.\n");
	    delete m;
#ifdef maysamdbg
	fprintf(stderr, "6");
#endif
	    return;
	}

	int cid = m->client_id();
	Request_id rid = m->request_id();

#ifdef maysamdbg
	fprintf(stderr, "7");
#endif
#ifdef TRACES
	fprintf(stderr, "O_Replica::handle() (cid = %d, rid=%llu)\n", cid, rid);
#endif


	Request_id last_rid = replies->req_id(cid);

	if (last_rid <= rid)
	{
#ifdef maysamdbg
	fprintf(stderr, "8");
#endif
	    if (last_rid == rid)
	    {
#ifdef maysamdbg
	fprintf(stderr, "9");
#endif
		// Request has already been executed.
		nb_retransmissions++;
		if (nb_retransmissions % 100== 0)
		{
		    fprintf(stderr, "O_Replica: nb retransmissions = %d\n", nb_retransmissions);
		}
	    }

	    // Request has not been executed.
#ifdef maysamdbg
	fprintf(stderr, "10");
#endif
	    if (!execute_request(m))
		delete m;
		//Added by Maysam Yabandeh
		else
			if (id() != primary() && m->getOrder() < totalOrder) //it was not out of order
			{
			   std::vector<O_Request*>::iterator bi = bufferedRequests.begin();
#ifdef maysamdbg
	fprintf(stderr, "\nbi size is %d\n", bufferedRequests.size());
#endif
			   for (; bi < bufferedRequests.end(); ) //bi++ it must not be here since we reset the bi to begin
				if (totalOrder == (*bi)->getOrder())
				{
#ifdef maysamdbg
	fprintf(stderr, "bi match %d = %d\n", totalOrder, (*bi)->getOrder());
#endif
					execute_request(*bi);
					//delete *bi;
					bufferedRequests.erase(bi);
					bi = bufferedRequests.begin();
				}
   else
	{
#ifdef maysamdbg
	fprintf(stderr, "bi no match %d != %d\n", totalOrder, (*bi)->getOrder());
#endif
	bi++;
}
			}
	    return;
	}

#ifdef maysamdbg
	fprintf(stderr, "11");
#endif
	// XXX: what to do here? should we delete the request?
	// that may invalidate the pointer in request history
	delete m;
}

bool O_Replica::execute_request(O_Request *req)
{
#ifdef maysamdbg
	fprintf(stderr, "11 ");
#endif
	int cid = req->client_id();
	Request_id rid = req->request_id();

	if (replies->req_id(cid) > rid)
	{
		return false;
	}

	if (replies->req_id(cid) == rid)
	{
		// Request has already been executed and we have the reply to
		// the request. Resend reply and don't execute request
		// to ensure idempotence.

		// All fields of the reply are correctly set
		replies->send_reply(cid, 0, id());
		return false;
	}

	//Added by Maysam Yabandeh
	//#define recv_dbg
	if (id() != primary())
	{
	if (totalOrder != req->getOrder())
	{
	   bufferedRequests.push_back(req);
		#ifdef recv_dbg
		fprintf(stderr, "= %d) out of order request %d (%d)\n", id(), req->getOrder(), totalOrder);
		#endif
		return true;
	}
		#ifdef recv_dbg
	else
		fprintf(stderr, "= %d) totalOrder=%d\n", id(), totalOrder);
		#endif
	}
		#ifdef recv_dbg
	else
		fprintf(stderr, "=== %d) totalOrder=%d\n", id(), totalOrder);
		#endif


#ifdef maysamdbg
	fprintf(stderr, "12 ");
#endif
	// Obtain "in" and "out" buffers to call exec_command
	Byz_req inb;
	Byz_rep outb;
	Byz_buffer non_det;
	inb.contents = req->command(inb.size);
	outb.contents = replies->new_reply(cid, outb.size);
	//non_det.contents = pp->choices(non_det.size);
#ifdef maysamdbg
	fprintf(stderr, "1size=%d \n", outb.size);
#endif

	// Execute command in a regular request.
	exec_command(&inb, &outb, &non_det, cid, false);
#ifdef maysamdbg
	fprintf(stderr, "2size=%d \n", outb.size);
#endif

	// perform_checkpoint();

	if (outb.size % ALIGNMENT_BYTES)
	{
		for (int i=0; i < ALIGNMENT_BYTES - (outb.size % ALIGNMENT_BYTES); i++)
		{
			outb.contents[outb.size+i] = 0;
		}
	}

#ifdef maysamdbg
	fprintf(stderr, "3size=%d \n", outb.size);
#endif

	Digest d;
	//Added by Maysam Yabandeh
	//the order is differet from primary to other replicas
	int tmpOrder = req->rep().order;
	req->rep().order = -1;
	if (rh->add_request(req, seqno, d))
	    seqno++;
	req->rep().order = tmpOrder;

	// Finish constructing the reply.
	replies->end_reply(cid, rid, outb.size, d);

	//   int replier = 1+ (req.request_id() % (num_replicas - 1));
	int replier = 2;

	//Added by Maysam Yabandeh
	//In 0/0 experiment the size is zero
	//if (outb.size != 0)
	{
#ifdef maysamdbg
	fprintf(stderr, "13 ");
#endif
		//if (outb.size < 50|| replier == node_id || replier < 0)
		//I do not understand the else branch, hence I changes the size to make this branch always works
		if (outb.size < 8500|| replier == node_id || replier < 0)
		{
#ifdef maysamdbg
	fprintf(stderr, "14 ");
#endif
			// Send full reply.
#ifdef TRACES
			fprintf(stderr, "Replica::execute_prepared: %d Sending full reply (outb.size = %d, req.replier = %d)\n", id(), outb.size, req->replier());
#endif
			//Added by Maysam Yabandeh
			replies->send_reply(cid, 0, id(), true, totalOrder++);
		} else
		{
#ifdef maysamdbg
	fprintf(stderr, "15 ");
#endif
			// Send empty reply.
#ifdef TRACES
			fprintf(stderr, "Replica::execute_prepared: %d Sending emtpy reply (outb.size = %d, req.replier = %d)\n", id(), outb.size, req->replier());
#endif
			O_Reply empty(0, req->request_id(), node_id, replies->digest(cid),
					i_to_p(cid), cid);
			//Added by Maysam Yabandeh
			O_Reply_rep& rr = empty.rep();
			rr.order = totalOrder++;
			//TODO: this ruins the digest, we need to do it before computing digest. However, this branch is weird to me anyway, so I try to avoid this branch.
			send(&empty, cid);
		}
	}
#ifdef maysamdbg
	fprintf(stderr, "16 ");
#endif

	// send the checkpoint if necessary
	//if (rh->should_checkpoint()) {
	//Added by Maysam Yabandeh - no checkpoint for the moment
	if (0) {
	    O_Checkpoint *chkp = new O_Checkpoint();
	    // fill in the checkpoint message
	    chkp->set_seqno(rh->get_top_seqno());
	    chkp->set_digest(rh->rh_digest());
	    // sign
	    O_node->gen_signature(chkp->contents(), sizeof(O_Checkpoint_rep),
		    chkp->contents()+sizeof(O_Checkpoint_rep));
	    // add it to the store
	    O_CheckpointSet *checkpoints = NULL;
	    if (!checkpoint_store.find(chkp->get_seqno(), &checkpoints)) {
		checkpoints = new O_CheckpointSet(n());
		checkpoints->store(chkp);
		checkpoint_store.add(chkp->get_seqno(), checkpoints);
		//fprintf(stderr, "O_Replica[%d]: checkpoint seqno %lld added to the list\n", id(), chkp->get_seqno());
	    } else {
		fprintf(stderr, "O_Replica[%d]: checkpoint set already exists for seqno %lld\n", id(), chkp->get_seqno());
		checkpoints->store(chkp);
	    }
	    // send it
	    send(chkp, O_All_replicas);
	}
	return true;
}

void O_Replica::handle(O_Checkpoint *c, int sock)
{
    // verify signature
    if (!c->verify()) {
	fprintf(stderr, "Couldn't verify the signature of O_Checkpoint\n");
	delete c;
	return;
    }

    // optimization: if less than last removed, discard
    if (checkpoint_store.last() != 0 && c->get_seqno() < checkpoint_store.last())
    {
	fprintf(stderr, "Checkpoint is for older than last removed, discarding\n");
	delete c;
	return;
    }

    // store
    O_CheckpointSet *checkpoints = NULL;
    if (!checkpoint_store.find(c->get_seqno(), &checkpoints)) {
	checkpoints = new O_CheckpointSet(n());
	checkpoints->store(c);
	checkpoint_store.add(c->get_seqno(), checkpoints);
    } else {
	checkpoints->store(c);
    }
    // check whether full
    // if so, clear, and truncate history
    if (checkpoints->size() == n()) {
	bool same = false;
	for (int i=0; i<n(); i++) {
	    O_Checkpoint *cc = checkpoints->fetch(i);
	    same = c->match(cc);
	    if (!same)
		break;
	}
	// we should now truncate the history
	if (same) {
	    checkpoint_store.remove(c->get_seqno());
	    rh->truncate_history(c->get_seqno());
	} else {
	    // send the panic message to everyone
	    fprintf(stderr, "O_Replica[%d]::O_Checkpoint: checkpoints are not the same, panicking!\n", id());
	    O_Panic qp(id(), 0);
	    send(&qp, O_All_replicas);
	    cur_state = replica_state_PANICKING;
	    n_retrans = 0;
	    //rtimer->restop();
	    //rtimer->start();
	}
    }
}

void O_Replica::handle(O_Panic *m, int sock)
{
   //   fprintf(stderr, "O_Panic[%d]: Handling Panic message\n", id());

   int cid = m->client_id();
   Request_id rid = m->request_id();

   //fprintf(stderr, "O_Replica[%d]:: receiving panic for (cid = %d rid = %llu)\n", id(), cid, rid);
   if (cur_state != replica_state_PANICKING && cur_state != replica_state_STOP) {
       O_Panic qp(id(), 0);
       send(&qp, O_All_replicas);
       n_retrans = 0;
       cur_state = replica_state_PANICKING;
       //rtimer->restop();
       //rtimer->start();
   }
   // put the client in the list
   if (!is_replica(cid)) {
       if (cur_state == replica_state_STOP) {
	    O_Reply qr(0, rid, node_id, replies->digest(cid), i_to_p(cid), cid);
	    qr.set_instance_id(chain);
	    qr.authenticate(i_to_p(cid), 0);

	    send(&qr, cid);
	    delete m;
	    return;
       } else {
       	   OutstandingRequests outs;
       	   outs.cid = cid;
       	   outs.rid = rid;
       	   outstanding.push_back(outs);
       }
   }

   // notify others
   broadcast_abort(rid);
   delete m;
#if 0
   else
   {
      // Check that there is not ongoing panic from this client
      if (!pinfos[cid].ongoing && replies.req_id(cid) <= rid)
      {
         pinfos[cid].ongoing = true;
         pinfos[cid].out_rid = rid;
         // Note that for now, we assume the requests must already have been received by replicas. If not, we'll abort.

         // Create On_behalf_request
         On_behalf_request *obr = new On_behalf_request(cid, rid, id());

         // Activate timer and clear certificate
         pinfos[cid].rep_cert->clear();
         pinfos[cid].timer->start();

         // Invoke Small Abstract
         send(obr, All_replicas);
         delete obr;
      }
      delete m;
   }
#endif
}

void O_Replica::handle(O_Abort *m, int sock)
{
    if (cur_state == replica_state_PANICKING)
    {

        if (!m->verify())
        {
            fprintf(stderr, "O_Replica[%d]: Unable to verify an Abort message\n", id());
            delete m;
            return;
        }

#ifdef TRACE
        fprintf(stderr, "O_Replica[%d]: received Abort message from %d (cid = %d, rid = %llu) (hist_size = %d)\n", id(), m->id(), m->client_id(), m->request_id(), m->hist_size());
#endif

        if (!aborts.add(m))
        {
            fprintf(stderr, "O_Replica[%d]: Failed to add Abort from %d to the certificate\n", id(), m->id());
            delete m;
            return;
        }
        if (aborts.is_complete())
        {
	    // since we have enough aborts, let's extract abort histories...
	    // hist_size keeps how many entries are there
	    //rtimer->stop();
	    unsigned int max_size = 0;
	    for (int i = 0; i < aborts.size() ; i++)
	    {
		Ac_entry *ace = aborts[i];
		if (ace == NULL)
		    continue;
		O_Abort *abort = ace->abort;
		if (max_size < abort->hist_size())
		    max_size = abort->hist_size();
	    }

	    // now, extract the history
	    O_Smasher qsmash(max_size, f(), aborts);
	    qsmash.process(id());
	    ah_2 = qsmash.get_ah();
	    // once you extract the history, find the missing requests
	    missing = qsmash.get_missing();
	    missing_mask.clear();
	    missing_mask.append(true, missing->size());
	    missing_store.clear();
	    missing_store.append(NULL, missing->size());
	    missing_store_seq.clear();
	    missing_store_seq.append(0, missing->size());
	    num_missing = missing->size();

	    aborts.clear();
	    // store these missing request
	    //qsmash.close();

	    // send out cry for help, to obtain the missing ones
	    // if there's no need for help, just stop
	    // XXX: switch to another protocol
	    if (missing->size() == 0) {
		// just replace the history with AH_2

		Req_history_log<O_Request> *newrh = new Req_history_log<O_Request>();
		for (int i=0; i<ah_2->size(); i++) {
		    // if it is in rh, just copy it
		    O_Request *ar = NULL;
		    Rh_entry<O_Request> *rhe = NULL;
		    AbortHistoryElement *ahe = NULL;
		    // XXX: delete superflous ones
		    ahe = ah_2->slot(i);
		    rhe = rh->find(ahe->cid, ahe->rid);
		    if (rhe != NULL) {
			ar = rhe->req;
			newrh->add_request(ar, rhe->seqno(), rhe->digest());
			rhe->req = NULL;
		    }
		}
		missing_store.clear();
		missing_store_seq.clear();
		missing_mask.clear();
		num_missing = 0;

		for (int i=0;i<ah_2->size(); i++)
		    if (ah_2->slot(i) != NULL)
			delete ah_2->slot(i);
		delete ah_2;
		delete rh;
		rh = newrh;
		seqno = rh->get_top_seqno()+1;

		fprintf(stderr, "O_Replica[%d]: seqno is %llu\n", id(), seqno);

		delete missing;
		missing = NULL;

		cur_state = replica_state_STOP;
		great_switcher->switch_state(instance_id(), false);
		great_switcher->switch_state(pbft, true);


		// now, we should notify all waiting clients
		notify_outstanding();

		return;
	    }

	    O_Missing qmis(id(), missing);
	    send(&qmis, O_All_replicas);

	    // and then wait to get a grip on these missing request
	    cur_state = replica_state_MISWAITING;
	    // once you receive them all, you can switch
            // aborts.clear();
            // cur_state = replica_state_NORMAL;
            return;
        }
    }
    else
    {
        // fprintf(stderr, "O_Replica[%d]: Receiving an Abort message during Panic mode\n", id());
        delete m;
    }
}

void O_Replica::retransmit_panic()
{
    //fprintf(stderr, "O_Replica[%d]: will retransmit PANIC!\n", id());
    static const int nk_thresh = 3;

    n_retrans++;
    if (n_retrans == nk_thresh) {
	//rtimer->stop();
	aborts.clear();
	delete rh;
	if (missing)
	    delete missing;
	missing = NULL;

	rh = new Req_history_log<O_Request>();
	cur_state = replica_state_STOP;
	great_switcher->switch_state(quorum, false);
	great_switcher->switch_state(chain, true);
	n_retrans = 0;
	return;
    }

    O_Panic qp(id(), 0);
    send(&qp, O_All_replicas);
    cur_state = replica_state_PANICKING;

    //rtimer->restart();
}

void O_Replica::handle(O_Missing *m, int sock)
{
    // extract the replica
    int replica_id = m->id();
    ssize_t offset = sizeof(O_Missing_rep);
    char *contents = m->contents();
    for (int i = 0; i < m->hist_size(); i++) {
	int cur_cid;
	Request_id cur_rid;

	// extract the requests
	memcpy((char *)&cur_cid, contents+offset, sizeof(int));
	offset += sizeof(int);
	memcpy((char *)&cur_rid, contents+offset, sizeof(Request_id));
	offset += sizeof(Request_id);

	// find them in the history
	Rh_entry<O_Request> *rhe = rh->find(cur_cid, cur_rid);
	if (rhe != NULL) {
	    // and construct Get-a-grip messages out of request
	    O_Get_a_grip qgag(cur_cid, cur_rid, id(), rhe->seqno(), (O_Request*)rhe->req);
	    send(&qgag, replica_id);
	}
    }
    delete m;
}

void O_Replica::handle(O_Get_a_grip *m, int sock)
{
    if (cur_state != replica_state_MISWAITING) {
    	fprintf(stderr, "O_Replica[%d]::O_get_a_grip: got unneeded QGAG message\n", id());
	delete m;
	return;
    }

    // extract the data
    int cid = m->client_id();
    Request_id rid = m->request_id();
    Seqno r_seqno = m->seqno();

    int rep_size = ((O_Message_rep *)m->stored_request())->size;
    //((Reply_rep *)gag->reply())->replica = gag->id();
    void *rep_ = malloc(rep_size);
    memcpy(rep_, m->stored_request(), rep_size);
    O_Request *req = new O_Request((O_Request_rep *)rep_);

    // now, make sure the message is good
    bool found = false;
    int i = 0;
    for (i=0; i<missing->size(); i++)
    {
	if (missing_mask[i] == false)
	    continue;

	AbortHistoryElement *ahe = missing->slot(i);
	if (ahe->cid == cid
		&& ahe->rid == rid
		&& ahe->d == req->digest())
	{
	    // found it...
	    found = true;
	    missing_mask[i] = false;
	    missing_store[i] = req;
	    missing_store_seq[i] = r_seqno;
	    num_missing--;
	    break;
	}
    }

    if (found && num_missing == 0) {
    	// just replace the history with AH_2
	Req_history_log<O_Request> *newrh = new Req_history_log<O_Request>();
	for (int i=0; i<ah_2->size(); i++) {
	    // if it is in rh, just copy it
	    O_Request *ar = NULL;
	    Rh_entry<O_Request> *rhe = NULL;
	    if ((rhe = rh->find(ah_2->slot(i)->cid, ah_2->slot(i)->rid)) != NULL) {
	    	ar = rhe->req;
	    	newrh->add_request(ar, rhe->seqno(), rhe->digest());
		rhe->req = NULL;
		continue;
	    } else {
	    	// we should find it in missing_store
		for(int j=0; j<missing_store.size(); j++) {
		    if (missing_store[j]->client_id() == ah_2->slot(i)->cid
		    	    &&
			    missing_store[j]->request_id() == ah_2->slot(i)->rid) {
			newrh->add_request(missing_store[j], missing_store_seq[j], missing_store[j]->digest());
			break;
		    }
		}
	    }
	}
	missing_store.clear();
	missing_store_seq.clear();
	missing_mask.clear();
	num_missing = 0;

	for (int i=0; i<ah_2->size(); i++)
	    if (ah_2->slot(i) != NULL)
		delete ah_2->slot(i);
	delete ah_2;
	delete rh;
	rh = newrh;
	seqno = rh->get_top_seqno()+1;

	delete missing;
	missing = NULL;

	cur_state = replica_state_STOP;
	great_switcher->switch_state(instance_id(), false);
	great_switcher->switch_state(pbft, true);

	notify_outstanding();
    }

    // if so, store it, or discard it...
    delete m;
}

void O_Replica::broadcast_abort(Request_id out_rid)
{
   //fprintf(stderr, "O_Replica[%d]::broadcast_abort for (cid = %d rid = %llu)\n", id(), cid, out_rid);
   O_Abort *a_message = new O_Abort(node_id, out_rid, *rh);

   a_message->sign();
   send(a_message, O_All_replicas);
   // since we're not receiving it back:
   if (!aborts.add(a_message))
       delete a_message;
}

void O_Replica::notify_outstanding()
{
    std::list<OutstandingRequests>::iterator it;
    std::map<int,Request_id> omap;
    std::map<int,Request_id>::iterator mit;

    great_switcher->switch_state(pbft, true);
    for (it=outstanding.begin(); it != outstanding.end(); it++) {
    	int cid = it->cid;
	Request_id rid = it->rid;
	if (omap.find(cid) == omap.end()) {
	    omap[cid] = rid;
	} else if (omap[cid] < rid) {
	    omap[cid] = rid;
	}
    }

    for (mit=omap.begin(); mit != omap.end(); mit++) {
	int cid = mit->first;
	Request_id rid = mit->second;

	O_Reply qr(0, rid, node_id, replies->digest(cid), i_to_p(cid), cid);
	qr.set_instance_id(pbft);
	qr.authenticate(i_to_p(cid), 0);

	send(&qr, cid);
    }

    // cleanup
    outstanding.clear();
}

void O_Replica::join_mcast_group()
{
	//struct ip_mreq req;
	//req.imr_multiaddr.s_addr = group->address()->sin_addr.s_addr;
	//req.imr_interface.s_addr = INADDR_ANY;
	//int error = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *) &req,
			//sizeof(req));
	//if (error < 0)
	//{
		//perror("Unable to join group");
		//exit(1);
	//}
}

void O_Replica::leave_mcast_group()
{
	//struct ip_mreq req;
	//req.imr_multiaddr.s_addr = group->address()->sin_addr.s_addr;
	//req.imr_interface.s_addr = INADDR_ANY;
	//int error = setsockopt(sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, (char *) &req,
			//sizeof(req));
	//if (error < 0)
	//{
		//perror("Unable to leave group");
		//exit(1);
	//}
}

void O_Replica::enable_replica(bool state)
{
    printDebug("O_Replica[%d]: will try to switch state, switch %d, when in state %d\n", id(), state, cur_state);
    if (cur_state == replica_state_NORMAL || cur_state == replica_state_STOP) {
	if (state) {
	    if (cur_state != replica_state_NORMAL) {
		delete rh;
		rh = new Req_history_log<O_Request>();
		checkpoint_store.clear_all();
		outstanding.clear();
		seqno = 0;
	    }
	    cur_state = replica_state_NORMAL;
	} else
	    cur_state = replica_state_STOP;
    }
}


void O_Replica::processReceivedMessage(O_Message* msg, int sock) 
{
//fprintf(stderr, "=O_Replica::processReceivedMessage\n");
		switch (msg->tag())
		{
		    case O_Request_tag:
			gen_handle<O_Request>(msg, sock);
			break;

		    case O_Checkpoint_tag:
			gen_handle<O_Checkpoint>(msg, sock);
			break;

		    case O_Panic_tag:
			gen_handle<O_Panic>(msg, sock);
			break;

		    case O_Abort_tag:
			gen_handle<O_Abort>(msg, sock);
			break;

		    case O_Missing_tag:
			gen_handle<O_Missing>(msg, sock);
			break;

		    case O_Get_a_grip_tag:
			gen_handle<O_Get_a_grip>(msg, sock);
			break;

		    default:
			// Unknown message type.
			delete msg;
		}
}
