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
#include <fcntl.h>

#include "th_assert.h"
#include "zl_Replica.h"
#include "zl_Message_tags.h"
#include "zl_Reply.h"
#include "zl_Principal.h"
#include "zl_Request.h"
#include "zl_Missing.h"
#include "zl_Get_a_grip.h"
#include "zl_Order_request.h"
#include "zl_Client_notification.h"
#include "MD5.h"
#include "zl_Smasher.h"
#include "RobustMonitor.h"
#include "Switcher.h"

#define _MEASUREMENTS_ID_ (zl_replica->id())
#include "measurements.h"

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

// Global replica object.
zl_Replica *zl_replica;

#include <signal.h>
static void kill_replica(int sig)
{
    zl_replica->leave_mcast_group();

    fprintf(stderr, "Max req history size = %d\n", zl_replica->max_history_size);

    REPORT_TIMINGS;
    exit(0);
}

void switch_zl_replica(bool state)
{
    if (zl_replica != NULL)
	zl_replica->enable_replica(state);
}

void*zl_receive_group_requests_handler_helper(void *o)
{
#if 0
    pthread_attr_t tattr;
    int newprio;
    sched_param param;

    /* set the priority; others are unchanged */
    newprio = 70;
    param.sched_priority = newprio;

    /* set the new scheduling param */
int ret = pthread_attr_setschedparam (&tattr, &param);
#endif
	void **o2 = (void **)o;
	zl_Replica &r = (zl_Replica&) (*o2);
	r.zl_receive_group_requests_handler();
	return 0;
}

void*zl_receive_requests_handler_helper(void *o)
{
    pthread_attr_t tattr;
    int newprio;
    sched_param param;

    /* set the priority; others are unchanged */
    newprio = 70;
    param.sched_priority = newprio;

    /* set the new scheduling param */
    pthread_attr_setschedparam (&tattr, &param);

	void **o2 = (void **)o;
	zl_Replica &r = (zl_Replica&) (*o2);
	r.zl_receive_requests_handler();
	return 0;
}

void*zl_handle_incoming_messages_helper(void *o)
{
    pthread_attr_t tattr;
    int newprio;
    sched_param param;

    /* set the priority; others are unchanged */
    newprio = 30;
    param.sched_priority = newprio;

    /* set the new scheduling param */
    pthread_attr_setschedparam (&tattr, &param);

    void **o2 = (void **)o;
    zl_Replica &r = (zl_Replica&) (*o2);
    r.handle_incoming_messages_from_queue();
    return 0;
}

static void handle_robust_monitor_message(int s)
{
  zl_Message *m = zl_replica->zl_Node::recv_tcp(s);

  //fprintf(stderr,
  //    "zlight has received a message %d of size %d from the robust monitor\n",
  //    m->tag(), m->size());

  zl_replica->handle(m);
}


void zl_abort_timeout_handler()
{
	th_assert(zl_replica, "zl_replica is not initialized");
	zl_replica->retransmit_panic();
}

void zl_no_checkpoint_timeout_handler()
{
  th_assert(zl_replica, "zl_replica is not initialized");
  zl_replica->no_checkpoint_timeout();
}

zl_Replica::zl_Replica(FILE *config_file, FILE *config_priv, char *host_name, short req_port) :
	zl_Node(config_file, config_priv, host_name, req_port), seqno(0), 
	cur_state(replica_state_NORMAL),
	aborts(3*f()+1, 3*f()+1),
	ah_2(NULL), missing(NULL), num_missing(0),
	missing_mask(), missing_store(), missing_store_seq(),
	outstanding(),
    pending_requests()
{
	// Fail if node is not a replica.
	if (!is_replica(id()))
	{
		th_fail("Node is not a replica");
	}

    zl_replica = this;
    great_switcher->register_switcher(instance_id(), switch_zl_replica);
    robust_monitor->register_handle_msg_func(this->instance_id(),
            handle_robust_monitor_message);

	pthread_mutex_init(&checkpointing_mutex, NULL);
	pthread_mutex_init(&switching_mutex, NULL);

	mcast_sock = socket(AF_INET, SOCK_DGRAM, 0);

	Addr tmp;
	tmp.sin_family = AF_INET;
	tmp.sin_addr.s_addr = htonl(INADDR_ANY); //group->address()->sin_addr.s_addr;
	tmp.sin_port = group->address()->sin_port;
	int error = bind(mcast_sock, (struct sockaddr*)&tmp, sizeof(Addr));
	if (error < 0)
	{
		perror("Unable to name group socket");
		exit(1);
	}

	// Set TTL larger than 1 to enable multicast across routers.
	u_char i = 20;
	error = setsockopt(mcast_sock, IPPROTO_IP, IP_MULTICAST_TTL, (char *)&i,
			sizeof(i));
	if (error < 0)
	{
		perror("unable to change TTL value");
		exit(1);
	}

	// Disable loopback
	u_char l = 0;
	error = setsockopt(mcast_sock, IPPROTO_IP, IP_MULTICAST_LOOP, &l, sizeof(l));
	if (error < 0)
	{
		perror("unable to disable loopback");
		exit(1);
	}

	struct in_addr interface_addr = principals[node_id]->address()->sin_addr;
	error = setsockopt (mcast_sock, IPPROTO_IP, IP_MULTICAST_IF, &interface_addr, sizeof(interface_addr));
	if (error < 0)
	{
	    perror("Unable to set outgoing interface for multicast");
	    exit(1);
	}

	error = fcntl(mcast_sock, F_SETFL, O_NONBLOCK);
	if (error < 0)
	{
		perror("unable to set socket to asynchronous mode");
		exit(1);
	}

	n_retrans = 0;
	rtimeout = 20;
	rtimer = new zl_ITimer(rtimeout, zl_abort_timeout_handler);
  ctimer = new zl_ITimer(CHECKPOINTING_TIMEOUT, zl_no_checkpoint_timeout_handler);

   fprintf(stderr, "Constructor: creating a new Req_history_log\n");
	rh = new Req_history_log<zl_Request>();

	replies = new zl_Rep_info(num_principals);
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

	join_mcast_group();

#if 1
   struct sigaction act;
   act.sa_handler = kill_replica;
   sigemptyset (&act.sa_mask);
   act.sa_flags = 0;
   sigaction (SIGINT, &act, NULL);
   sigaction (SIGTERM, &act, NULL);
#endif

   checkpoint_in_progress = false;
   current_nb_requests_while_checkpointing = 0;
   last_chkp_seqno = 0;

   nb_sent_replies = 0;
   max_history_size = 0;
   req_count_switch = 0;

	pthread_t handler_thread;

	if (pthread_create(&handler_thread, NULL,
			&zl_handle_incoming_messages_helper, (void *) this) != 0)
	{
		fprintf(stderr,
		"Failed to create the thread for processing client requests\n");
		exit(1);
	}
	if (pthread_create(&zl_receive_requests_handler_thread, NULL,
		    &zl_receive_requests_handler_helper, (void *)this)!= 0)
	{
	    fprintf(stderr, "Failed to create the thread for receiving client requests\n");
	    exit(1);
	}

	fprintf(stderr, "Created the thread for receiving client requests\n");
	if (pthread_create(&zl_receive_group_requests_handler_thread, NULL,
		    &zl_receive_group_requests_handler_helper, (void *)this)!= 0)
	{
	    fprintf(stderr, "Failed to create the thread for receiving group requests\n");
	    exit(1);
	}
	fprintf(stderr, "Created the thread for receiving group requests\n");
}

zl_Replica::~zl_Replica()
{
    cur_state = replica_state_STOP;
    delete rtimer;
    if (missing)
	delete missing;
    if (ah_2)
    	delete ah_2;
    delete replies;
   fprintf(stderr, "Destructor: destructing a Req_history_log\n");
    delete rh;
}

void zl_Replica::register_exec(int(*e)(Byz_req *, Byz_rep *, Byz_buffer *, int, bool))
{
	exec_command = e;
}

void zl_Replica::register_perform_checkpoint(int(*p)())
{
	perform_checkpoint = p;
}

void zl_Replica::zl_receive_group_requests_handler()
{
	fprintf(stderr,"Iam here\n");
	zl_Message* msg;

	while (1)
	{
	    msg = zl_Node::recv(mcast_sock);

	    // Enqueue the request
	    pthread_mutex_lock(&incoming_queue_mutex);
	    {
		// fprintf(stderr, "Got the mutex\n");
		incoming_queue.append(msg);
		pthread_cond_signal(&not_empty_incoming_queue_cond);
	    }
	    pthread_mutex_unlock(&incoming_queue_mutex);
	}
	pthread_exit(NULL);
}

void zl_Replica::zl_receive_requests_handler()
{
	fprintf(stderr,"Iam here\n");
	zl_Message* msg;

	while (1)
	{
	    msg = zl_Node::recv();

	    // Enqueue the request
	    pthread_mutex_lock(&incoming_queue_mutex);
	    {
		// fprintf(stderr, "Got the mutex\n");
		incoming_queue.append(msg);
		pthread_cond_signal(&not_empty_incoming_queue_cond);
	    }
	    pthread_mutex_unlock(&incoming_queue_mutex);
	}
	pthread_exit(NULL);
}

void zl_Replica::handle_incoming_messages_from_queue()
{
	zl_Message* msg;
	while (1)
	{
		pthread_mutex_lock(&incoming_queue_mutex);
		{
			while (incoming_queue.size() == 0)
			{

				pthread_cond_wait(&not_empty_incoming_queue_cond,
						&incoming_queue_mutex);
			}
			msg = incoming_queue.remove();
		}
		pthread_mutex_unlock(&incoming_queue_mutex);

		if (unlikely(msg == NULL))
		    continue;

        handle(msg);
    }
}

void zl_Replica::handle(zl_Message *msg) {
    switch (msg->tag())
    {
        case zl_Request_tag:
            ENTRY_TIME;
            gen_handle<zl_Request>(msg);
            EXIT_TIME;
            break;

        case zl_Order_request_tag:
            ENTRY_TIME;
            gen_handle<zl_Order_request>(msg);
            EXIT_TIME;
            break;

        case zl_Checkpoint_tag:
            gen_handle<zl_Checkpoint>(msg);
            break;

        case zl_Panic_tag:
            gen_handle<zl_Panic>(msg);
            break;

        case zl_Abort_tag:
            gen_handle<zl_Abort>(msg);
            break;

        case zl_Missing_tag:
            gen_handle<zl_Missing>(msg);
            break;

        case zl_Get_a_grip_tag:
            gen_handle<zl_Get_a_grip>(msg);
            break;

        case zl_Client_notification_tag:
            gen_handle<zl_Client_notification>(msg);
            break;

        default:
            // Unknown message type.
            delete msg;
    }
}

void zl_Replica::handle(zl_Request *m) {
        // accept only if in normal state
#ifdef TRACES
	fprintf(stderr, "zl_Replica[%d]: handling request from %d, state %d\n", id(), m->client_id(), cur_state);
#endif

	if (unlikely(cur_state == replica_state_STOP)) {
	    int cid = m->client_id();

	    zl_Reply qr(0, m->request_id(), node_id, replies->digest(cid), i_to_p(cid), cid);
	    qr.set_instance_id(next_instance_id());
	    qr.authenticate(i_to_p(cid), 0);

	    send(&qr, cid);

	    delete m;
	    return;
	} else if (unlikely(cur_state != replica_state_NORMAL)) {
	    OutstandingRequests outs;
	    outs.cid = m->client_id();
	    outs.rid = m->request_id();
        pthread_mutex_lock(&switching_mutex);
	    outstanding.push_back(outs);
        pthread_mutex_unlock(&switching_mutex);
	    delete m;
	    return;
	}

  // start the timer if it is stopped
  // does nothing if it is running
  // it ensures quorum will not switch if it does
  // not receive any request at all (because we are
  // at the beginning of the experiment or switching)
  ctimer->restart();

  req_count_switch++;

	if (unlikely(!m->verify()))
	{
	    fprintf(stderr, "zl_Replica::handle(): request verification failed.\n");
	    delete m;
	    return;
	}

	if (unlikely(m->is_read_only()))
	{
	    fprintf(stderr, "zl_Replica::handle(): read-only requests are not handled.\n");
	    delete m;
	    return;
	}

	int cid = m->client_id();
	Request_id rid = m->request_id();

#ifdef TRACES
	fprintf(stderr, "zl_Replica::handle() (cid = %d, rid=%llu)\n", cid, rid);
#endif

    pthread_mutex_lock(&checkpointing_mutex);
    if (checkpoint_in_progress) {
        if (current_nb_requests_while_checkpointing < rh->max_nb_requests_while_checkpointing()) {
            // can handle the request
            current_nb_requests_while_checkpointing++;
        } else {
            //fprintf(stderr, "Checkpoint in progress, cannot handle the request <%d, %lld>\n", cid, m->request_id());

            //fprintf(stderr, "The checkpoints are: ");
            //for (std::map<Seqno, CheckpointSet*>::iterator it = current_checkpoints.begin(); it != current_checkpoints.end(); it++) {
            //   CheckpointSet* cset = it->second;
            //   fprintf(stderr, " [%lld, %d]", cset->get_seqno(), cset->count());
            //}
            //fprintf(stderr, "\n");

            delete m;
            pthread_mutex_unlock(&checkpointing_mutex);
            return;
        }
    }
    pthread_mutex_unlock(&checkpointing_mutex);

	Request_id last_rid = replies->req_id(cid);

	if (id() == primary())
	{
	    if (last_rid <= rid)
	    {
		if (last_rid == rid)
		{
		    // Request has already been executed.
		    nb_retransmissions++;
		    if (nb_retransmissions % 100== 0)
		    {
             fprintf(stderr, "zl_Replica: nb retransmissions = %d\n", nb_retransmissions);
		    }
		} else {
		    // Request has not been executed.
		    seqno++;
		}

		if (!execute_request(m)) {
		    delete m;
		    return;
		}

		// now, send the request to all other replicas
		zl_Order_request *order_req = new zl_Order_request(0, seqno, m);
		send(order_req, zl_All_replicas);
		delete order_req;

		// don't delete request, it goes into the history
		delete m;
		return;
	    }
	} else {
	    if (last_rid == rid)
	    {
		// Request has already been executed.
		nb_retransmissions++;
		if (nb_retransmissions % 100== 0)
		{
		    fprintf(stderr, "zl_Replica: nb retransmissions = %d\n", nb_retransmissions);
		}

		execute_request(m);
	    } else {
#ifdef CLIENT_MULTICAST
            fprintf(stderr, "zl_Replica[%d]: received request <%d, %lld>, state %d\n", id(), m->client_id(), m->request_id(), cur_state);

            //TODO we need to copy the request and add the copy to the map
            //If we have already received the ordering message, then
            //execute the request, remove it from the map
            zl_Message_id msgid(cid, rid);
            std::map<zl_Message_id, zl_Pending_Request>::const_iterator it = pending_requests.find(msgid);

            // new request
            if (it == pending_requests.end()) {
                void* rep_ = malloc(m->size());
                memcpy(rep_, m->contents(), m->size());
                zl_Request *copy = new zl_Request((zl_Request_rep*)rep_);
                zl_Pending_Request pr (copy, 0);

                pending_requests.insert(std::pair<zl_Message_id, zl_Pending_Request>(msgid, pr));
            } else {
                bool need_to_execute = false;
                zl_Pending_Request pr = it->second;
                if (pr->first == NULL && pr->second != 0) { // request ordered by primary. Need to execute
                    void* rep_ = malloc(m->size());
                    memcpy(rep_, m->contents(), m->size());
                    zl_Request *copy = new zl_Request((zl_Request_rep*)rep_);
                    pr->first = copy;
                    pending_requests.insert(std::pair<zl_Message_id, zl_Pending_Request>(msgid, pr));
                    need_to_execute = true;
                } else if (pr->first == NULL && pr->second == 0) { // request already received but not yet ordered. Do nothing
                } else { // request not executed last time. Need to execute
                    need_to_execute = true;
                }

                if (need_to_execute && last_rid == rid) {
                    execute_request(m);
                    //TODO remove request from pending_requests
                }
            }
#endif
        }

	    // we're not the primary, so just drop the packet
	    delete m;
	    return;
	}

	// XXX: what to do here? should we delete the request?
	// that may invalidate the pointer in request history
	delete m;
}

bool zl_Replica::execute_request(zl_Request *req)
{
	int cid = req->client_id();
	Request_id rid = req->request_id();

#ifdef TRACES
	fprintf(stderr, "zl_Replica[%d]: executing request %lld for client %d\n", id(), rid, cid);
#endif
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

	// Obtain "in" and "out" buffers to call exec_command
	Byz_req inb;
	Byz_rep outb;
	Byz_buffer non_det;
	inb.contents = req->command(inb.size);
	outb.contents = replies->new_reply(cid, outb.size);
	//non_det.contents = pp->choices(non_det.size);

	// Execute command in a regular request.
	exec_command(&inb, &outb, &non_det, cid, false);

	// perform_checkpoint();

	if (outb.size % ALIGNMENT_BYTES)
	{
		for (int i=0; i < ALIGNMENT_BYTES - (outb.size % ALIGNMENT_BYTES); i++)
		{
			outb.contents[outb.size+i] = 0;
		}
	}

	last_seqno = seqno;
	Digest d;
	d = req->digest();

  zl_Request *copy = new zl_Request();
  memcpy(copy->contents(), req->contents(), req->size());
  copy->trim();
  rh->add_request(copy, seqno, d);

	// Finish constructing the reply.
	replies->end_reply(cid, rid, outb.size, d);
   int replier = rid % num_replicas;

#ifdef TRACES
     fprintf(stderr, "zl_Replica[%d]: Preparing to respond to client, with outb.size = %d\n", id(), outb.size);
#endif
     if (outb.size < 50 || replier == node_id || replier < 0)
     {
        // Send full reply.
#ifdef TRACES
        fprintf(stderr, "Replica::execute_prepared: %d Sending full reply (outb.size = %d, req.replier = %d)\n", id(), outb.size, req->replier());
#endif
        replies->send_reply(cid, 0, id(), true);
        nb_sent_replies++;
     } else
     {
        // Send empty reply.
#ifdef TRACES
        fprintf(stderr, "Replica::execute_prepared: %d Sending emtpy reply (outb.size = %d, req.replier = %d)\n", id(), outb.size, req->replier());
#endif
        zl_Reply empty(0, req->request_id(), node_id, replies->digest(cid),
              i_to_p(cid), cid);
        nb_sent_replies++;
        send(&empty, cid);
     }

     // send the checkpoint if necessary
     if (rh->should_checkpoint()) {
        zl_Checkpoint *chkp = new zl_Checkpoint();
        chkp->set_seqno(rh->get_top_seqno());
        chkp->set_digest(rh->rh_digest());
        zl_node->gen_signature(chkp->contents(), sizeof(zl_Checkpoint_rep),
              chkp->contents()+sizeof(zl_Checkpoint_rep));

        checkpoint_in_progress = true;
        robust_monitor->send_message(chkp->contents(), chkp->size(), -1, instance_id());
        handle(chkp);
     }

     return true;
}

void zl_Replica::handle(zl_Order_request *m)
{
   static int cnt = 0;
   if (m->verify())
   {
      int cid = m->client_id();
      Request_id rid = m->request_id();

      Request_id last_rid = replies->req_id(cid);

      if (last_rid <= rid)
      {
         //fprintf(stderr, "%d receiving order_request ")
         int rep_size = ((zl_Message_rep *)m->stored_request())->size;
         //((Reply_rep *)gag->reply())->replica = gag->id();
         void *rep_ = malloc(rep_size);
         memcpy(rep_, m->stored_request(), rep_size);
         zl_Request *req = new zl_Request((zl_Request_rep *)rep_);

         if (req == NULL)
         {
            fprintf(stderr, "zl_Replica[%d]: unable to get request from Order message\n", id());
            free(rep_);
            delete m;
            return;
         }

         if (m->seqno() != last_seqno+1) {
            // out of order message
            fprintf(stderr, "zl_Replica[%d]: out of order message (got: %lld), (expected: %lld)\n", id(), m->seqno(), last_seqno+1);
            // XXX: maybe panic?
            delete req;
            delete m;
            free(rep_);
            return;
         }

         if (!req->verify()) {
            fprintf(stderr, "zl_Replica[%d]: couldn't verify request from Order message\n", id());
            delete req;
            delete m;
            free(rep_);
            return;
         }
         // Execute req
         seqno = last_seqno+1;
         execute_request(req);

         delete req;
         free(rep_);
      }
   }
   else
   {
      fprintf(stderr, "Unable to verify Order_request message\n");
   }
   delete m;
}

void zl_Replica::handle(zl_Client_notification *m) {
   pthread_mutex_lock(&checkpointing_mutex);
   switch(m->get_op_type()) {
      case zl_Client_notification_request:
         robust_monitor->add_client_req_notif();
         break;

      case zl_Client_notification_reply:
         robust_monitor->add_client_rep_notif();
         break;

      default:
         fprintf(stderr, "zl_Replica[%d] has received a zl_Client_notification with unknown op_type: %hd\n", id(), m->get_op_type());
         break;
   }
   pthread_mutex_unlock(&checkpointing_mutex);

   delete m;
}


void zl_Replica::handle(zl_Checkpoint *c)
{
   //fprintf(stderr, "Handle checkpoint from %d for %lld\n", c->id(), c->get_seqno());

   // verify signature
   if (!c->verify()) {
      fprintf(stderr, "Couldn't verify the signature of zl_Checkpoint\n");
      delete c;
      return;
   }

   pthread_mutex_lock(&checkpointing_mutex);
   if (c->get_seqno() <= last_chkp_seqno) {
      //fprintf(stderr, "Checkpoint is too old: %lld < %lld\n", c->get_seqno(), last_chkp_seqno);
      pthread_mutex_unlock(&checkpointing_mutex);
      delete c;
      return;
   }

   std::map<Seqno, CheckpointSet*>::iterator it = current_checkpoints.find(c->get_seqno());
   CheckpointSet *cset;
   if (it == current_checkpoints.end()) {
      cset = new CheckpointSet(n(), 2*f()+1, c->get_seqno());
      current_checkpoints[c->get_seqno()] = cset;
   } else {
      cset = current_checkpoints[c->get_seqno()];
   }

   bool ret = cset->add(c->id(), c);
   if (!ret) {
     fprintf(stderr, "Cannot add the checkpoint %lld from %d\n", c->get_seqno(), c->id());
     pthread_mutex_unlock(&checkpointing_mutex);
     delete c;
     return;
   }

   if (cset->is_complete()) {
     bool same = false;
     for (int i = 0; i < n(); i++)
     {
        zl_Checkpoint *cc = cset->get(i);
        if (cc != NULL) {
           same = c->match(cc);
           if (!same)
              break;
        }
     }

     //PL Well, if it does not match, then maybe we should clear the cset,
     // remove it from the cset, etc...

     max_history_size = (rh->size() > max_history_size ? rh->size() : max_history_size);
     rh->truncate_history(cset->get_seqno());
     current_nb_requests_while_checkpointing = 0;
     checkpoint_in_progress = false;
     last_chkp_seqno = cset->get_seqno();

     current_checkpoints.erase(cset->get_seqno());
     delete cset;

     ctimer->stop();
     ctimer->start();

     if (robust_monitor->should_panic()) {
        fprintf(stderr, "zl_Replica[%d] should panic, send PANIC!\n", id());
        zl_Panic *qp = new zl_Panic(id(), 0);
        handle(qp);
     }
   }
   pthread_mutex_unlock(&checkpointing_mutex);
}

void zl_Replica::handle(zl_Panic *m)
{
   //   fprintf(stderr, "zl_Panic[%d]: Handling Panic message\n", id());

  //XXX PL NEVER SWITCH
  delete m;
  return;

   int cid = m->client_id();
   Request_id rid = m->request_id();

//   fprintf(stderr, "zl_Replica:: receiving panic for (cid = %d rid = %llu)\n", cid, rid);
   pthread_mutex_lock(&switching_mutex);
   if (cur_state != replica_state_PANICKING) {
       zl_Panic qp(id(), 0);
       robust_monitor->send_message(qp.contents(), qp.size(), -1, instance_id());
       rtimer->start();
       cur_state = replica_state_PANICKING;
   }
   // put the client in the list
   if (!is_replica(cid)) {
       OutstandingRequests outs;
       outs.cid = cid;
       outs.rid = rid;
       outstanding.push_back(outs);
   }
   pthread_mutex_unlock(&switching_mutex);

   // notify others
   broadcast_abort(rid);
   delete m;
}

void zl_Replica::handle(zl_Abort *m)
{
  pthread_mutex_lock(&switching_mutex);
    if (cur_state == replica_state_PANICKING)
    {

        if (!m->verify())
        {
            fprintf(stderr, "zl_Replica[%d]: Unable to verify an Abort message\n", id());
            pthread_mutex_unlock(&switching_mutex);
            delete m;
            return;
        }

#ifdef TRACE
        fprintf(stderr, "zl_Replica[%d]: received Abort message from %d (cid = %d, rid = %llu) (hist_size = %d)\n", id(), m->id(), m->client_id(), m->request_id(), m->hist_size());
#endif

        if (!aborts.add(m))
        {
            fprintf(stderr, "zl_Replica[%d]: Failed to add Abort from %d to the certificate\n", id(), m->id());
            pthread_mutex_unlock(&switching_mutex);
            delete m;
            return;
        }
        if (aborts.is_complete())
        {
	    // since we have enough aborts, let's extract abort histories...
	    // hist_size keeps how many entries are there
	    rtimer->stop();
	    unsigned int max_size = 0;
	    for (int i = 0; i < aborts.size() ; i++)
	    {
		Ac_entry *ace = aborts[i];
		if (ace == NULL)
		    continue;
		zl_Abort *abort = ace->abort;
		if (max_size < abort->hist_size())
		    max_size = abort->hist_size();
	    }

	    // now, extract the history
	    zl_Smasher qsmash(max_size, f(), aborts);
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

		Req_history_log<zl_Request> *newrh = new Req_history_log<zl_Request>();
		for (int i=0; i<ah_2->size(); i++) {
		    // if it is in rh, just copy it
		    zl_Request *ar = NULL;
		    Rh_entry<zl_Request> *rhe = NULL;
		    AbortHistoryElement *ahe = NULL;
		    // XXX: delete superflous ones
		    ahe = ah_2->slot(i);
		    rhe = rh->find(ahe->cid, ahe->rid);
		    ar = rhe->req;
		    rhe->req = NULL;
		    newrh->add_request(ar, rhe->seqno(), rhe->digest());
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

//    fprintf(stderr, "Q_Replica[%d]: seqno is %llu\n", id(), seqno);

    delete missing;
    missing = NULL;

		cur_state = replica_state_STOP;
		great_switcher->switch_state(instance_id(), false);
		great_switcher->switch_state(next_instance_id(), true);

		// now, we should notify all waiting clients
		notify_outstanding();

        pthread_mutex_unlock(&switching_mutex);
		return;
	    }

	    zl_Missing qmis(id(), missing);
       robust_monitor->send_message(qmis.contents(), qmis.size(), -1, instance_id());

	    // and then wait to get a grip on these missing request
	    cur_state = replica_state_MISWAITING;
	    // once you receive them all, you can switch
            // aborts.clear();
            // cur_state = replica_state_NORMAL;
        pthread_mutex_unlock(&switching_mutex);
            return;
        }
    }
    else
    {
        // Receiving an Abort message during panic mode
//        fprintf(stderr, "zl_Replica[%d]: Receiving an Abort message during Panic mode\n", id());
        delete m;
    }
    pthread_mutex_unlock(&switching_mutex);
}

void zl_Replica::retransmit_panic()
{
    //fprintf(stderr, "zl_Replica[%d]: will retransmit PANIC!\n", id());
    zl_Panic qp(id(), 0);
    send(&qp, zl_All_replicas);
    pthread_mutex_lock(&switching_mutex);
    cur_state = replica_state_PANICKING;
    pthread_mutex_unlock(&switching_mutex);

    rtimer->restart();
}

void zl_Replica::handle(zl_Missing *m)
{
    // extract the replica
    int replica_id = m->id();
    ssize_t offset = sizeof(zl_Missing_rep);
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
	Rh_entry<zl_Request> *rhe = rh->find(cur_cid, cur_rid);
	if (rhe != NULL) {
	    // and construct Get-a-grip messages out of request
	    zl_Get_a_grip qgag(cur_cid, cur_rid, id(), rhe->seqno(), (zl_Request*)rhe->req);
       robust_monitor->send_message(qgag.contents(), qgag.size(), replica_id, instance_id());
	}
    }
    delete m;
}

void zl_Replica::handle(zl_Get_a_grip *m)
{
    pthread_mutex_lock(&switching_mutex);

    if (cur_state != replica_state_MISWAITING) {
    	fprintf(stderr, "zl_Replica[%d]::zl_get_a_grip: got unneeded zlGAG message\n", id());
    pthread_mutex_unlock(&switching_mutex);
	delete m;
	return;
    }

    // extract the data
    int cid = m->client_id();
    Request_id rid = m->request_id();
    Seqno r_seqno = m->seqno();

    int rep_size = ((zl_Message_rep *)m->stored_request())->size;
    //((Reply_rep *)gag->reply())->replica = gag->id();
    void *rep_ = malloc(rep_size);
    memcpy(rep_, m->stored_request(), rep_size);
    zl_Request *req = new zl_Request((zl_Request_rep *)rep_);

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
	Req_history_log<zl_Request> *newrh = new Req_history_log<zl_Request>();
	for (int i=0; i<ah_2->size(); i++) {
	    // if it is in rh, just copy it
	    zl_Request *ar = NULL;
	    Rh_entry<zl_Request> *rhe = NULL;
	    if ((rhe = rh->find(ah_2->slot(i)->cid, ah_2->slot(i)->rid)) != NULL) {
	    	ar = rhe->req;
	    	newrh->add_request(ar, rhe->seqno(), rhe->digest());
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

	cur_state = replica_state_STOP;
	great_switcher->switch_state(instance_id(), false);
	great_switcher->switch_state(next_instance_id(), true);

	notify_outstanding();
    }

    // if so, store it, or discard it...
    delete m;

    pthread_mutex_unlock(&switching_mutex);
}

void zl_Replica::broadcast_abort(Request_id out_rid)
{
//   fprintf(stderr, "zl_Replica[%d]::send_abort for (rid = %llu)\n", id(), out_rid);
   zl_Abort *a_message = new zl_Abort(node_id, out_rid, *rh);

   a_message->sign();
   robust_monitor->send_message(a_message->contents(), a_message->size(), -1, instance_id());
   // since we're not receiving it back:
   pthread_mutex_lock(&switching_mutex);
   if (!aborts.add(a_message))
       delete a_message;
   pthread_mutex_unlock(&switching_mutex);
}

void zl_Replica::notify_outstanding()
{
  std::list<OutstandingRequests>::iterator it;
  std::map<int, Request_id> omap;
  std::map<int, Request_id>::iterator mit;

  pthread_mutex_lock(&switching_mutex);

  for (it = outstanding.begin(); it != outstanding.end(); it++)
  {
    int cid = it->cid;
    Request_id rid = it->rid;
    if (omap.find(cid) == omap.end())
    {
      omap[cid] = rid;
    }
    else if (omap[cid] < rid)
    {
      omap[cid] = rid;
    }
  }

  for (mit = omap.begin(); mit != omap.end(); mit++)
  {
    int cid = mit->first;
    Request_id rid = mit->second;

    zl_Reply qr(0, rid, node_id, replies->digest(cid), i_to_p(cid), cid);
    qr.set_instance_id(next_instance_id());
    qr.authenticate(i_to_p(cid), 0);

//    fprintf(stderr, "Replica[%d] notify client %d about the abort for request %qd, with digest ", id(), cid, rid);
    replies->digest(cid).print();
    send(&qr, cid);
  }

  // cleanup
  outstanding.clear();

  pthread_mutex_unlock(&switching_mutex);
}

void zl_Replica::join_mcast_group()
{
	struct ip_mreq req;
	bzero(&req, sizeof(req));

	req.imr_multiaddr.s_addr = group->address()->sin_addr.s_addr;
	//req.imr_interface.s_addr = INADDR_ANY;
	req.imr_interface.s_addr = principals[node_id]->address()->sin_addr.s_addr;
	int error = setsockopt(mcast_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *) &req,
			sizeof(req));
	if (error < 0)
	{
		perror("Unable to join group");
		exit(1);
	}

	struct in_addr interface_addr;
	bzero(&interface_addr, sizeof(interface_addr));
	interface_addr.s_addr = principals[node_id]->address()->sin_addr.s_addr;
	fprintf(stderr, "Address is %lx\n", interface_addr.s_addr);
	error = setsockopt(mcast_sock, IPPROTO_IP, IP_MULTICAST_IF, (char *) &interface_addr,
			sizeof(interface_addr));
	if (error < 0)
	{
		perror("Unable to bind to interface");
		//exit(1);
	}
}

void zl_Replica::leave_mcast_group()
{
	struct ip_mreq req;
	req.imr_multiaddr.s_addr = group->address()->sin_addr.s_addr;
	//req.imr_interface.s_addr = INADDR_ANY;
	req.imr_interface.s_addr = principals[node_id]->address()->sin_addr.s_addr;
	int error = setsockopt(mcast_sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, (char *) &req,
			sizeof(req));
	if (error < 0)
	{
		perror("Unable to leave group");
		exit(1);
	}
}

void zl_Replica::enable_replica(bool state)
{
    pthread_mutex_lock(&switching_mutex);

    fprintf(stderr, "zl_Replica[%d]: will try to switch state, switch %d, when in state %d, after %ld reqs\n", id(), state, cur_state, req_count_switch);
    if (cur_state == replica_state_NORMAL || cur_state == replica_state_STOP) {
       if (state) {
          if (cur_state != replica_state_NORMAL) {
             delete rh;
             rh = new Req_history_log<zl_Request>();
             current_checkpoints.clear();
             outstanding.clear();
             seqno = 0;
             last_seqno = 0;
             req_count_switch = 0;
          }
          cur_state = replica_state_NORMAL;
          robust_monitor->switch_protocol(instance_id());
          ctimer->stop();
       } else {
          cur_state = replica_state_STOP;
          ctimer->stop();
       }
    }
    pthread_mutex_unlock(&switching_mutex);
}

void zl_Replica::no_checkpoint_timeout() {
   ctimer->stop();
   fprintf(stderr, "zl_Replica[%d] no checkpoint, send PANIC!\n", id());
   zl_Panic *qp = new zl_Panic(id(), 0);
   handle(qp);
}
