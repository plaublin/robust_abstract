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
#include <signal.h>

#include "th_assert.h"
#include "Q_Replica.h"
#include "Q_Message_tags.h"
#include "Q_Reply.h"
#include "Q_Principal.h"
#include "Q_Request.h"
#include "Q_Missing.h"
#include "Q_Get_a_grip.h"
#include "MD5.h"

#include "Q_Smasher.h"

#include "Switcher.h"
#include "RobustMonitor.h"

#define _MEASUREMENTS_ID_ (Q_replica->id())
#include "measurements.h"
#include "statistics.h"

// Global replica object.
Q_Replica *Q_replica;

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

static void kill_replica(int sig)
{
  REPORT_STATS; REPORT_TIMINGS;
  //Q_replica->leave_mcast_group();
  exit(0);
}

void switch_q_replica(bool state)
{
  if (Q_replica != NULL)
    Q_replica->enable_replica(state);
}

void*Q_handle_incoming_messages_helper(void *o)
{
  void **o2 = (void **) o;
  Q_Replica &r = (Q_Replica&) (*o2);
  r.handle_incoming_messages();
  return 0;
}

void Q_abort_timeout_handler()
{
  th_assert(Q_replica, "Q_replica is not initialized");
  Q_replica->retransmit_panic();
}

void Q_no_checkpoint_timeout_handler()
{
  th_assert(Q_replica, "Q_replica is not initialized");
  Q_replica->no_checkpoint_timeout();
}

static void handle_robust_monitor_message(int s)
{
  Q_Message *m = Q_replica->Q_Node::recv(s);

  //fprintf(stderr,
  //    "Chain has received a message %d of size %d from the robust monitor\n",
  //    m->tag(), m->size());
  
  Q_replica->handle(m);
}

inline void*Q_message_queue_handler_helper(void *o)
{
  void **o2 = (void **) o;
  Q_Replica &r = (Q_Replica&) (*o2);
  r.do_recv_from_network();
  return 0;
}

void Q_Replica::do_recv_from_network()
{
  Q_Message *msg;
  while (1)
  {
    msg = Q_Node::recv();

    pthread_mutex_lock(&incoming_queue_mutex);
    {
      incoming_queue.append(msg);
      pthread_cond_signal(&not_empty_incoming_queue_cond);
    }
    pthread_mutex_unlock(&incoming_queue_mutex);
  }
}

Q_Replica::Q_Replica(FILE *config_file, FILE *config_priv, char *host_name,
    short req_port) :
  Q_Node(config_file, config_priv, host_name, req_port), seqno(0),
      cur_state(replica_state_NORMAL),
      aborts(3 * f() + 1, 3 * f() + 1), ah_2(NULL), missing(NULL),
      num_missing(0), missing_mask(), missing_store(), missing_store_seq(),
      outstanding()
{
  // Fail if node is not a replica.
  if (!is_replica(id()))
  {
    th_fail("Node is not a replica");
  }

  Q_replica = this;
  great_switcher->register_switcher(instance_id(), switch_q_replica);
  robust_monitor->register_handle_msg_func(this->instance_id(),
      handle_robust_monitor_message);

  n_retrans = 0;
  rtimeout = 10;
  rtimer = new Q_ITimer(rtimeout, Q_abort_timeout_handler);
  ctimer = new Q_ITimer(CHECKPOINTING_TIMEOUT, Q_no_checkpoint_timeout_handler);

  rh = new Req_history_log<Q_Request> ();

  replies = new Q_Rep_info(num_principals);
  replier = 0;
  nb_retransmissions = 0;
  req_count_switch = 0;

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
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;
  sigaction(SIGINT, &act, NULL);
  sigaction(SIGTERM, &act, NULL);
#endif

  pthread_mutex_init(&checkpointing_mutex, NULL);
  pthread_mutex_init(&switching_mutex, NULL);

  checkpoint_in_progress = false;
  current_nb_requests_while_checkpointing = 0;
  last_chkp_seqno = 0;

  // Disable loopback
  u_char l = 0;
  int error = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &l, sizeof(l));
  if (error < 0)
  {
    perror("unable to disable loopback");
    exit(1);
  }

  pthread_t handler_thread;
  if (pthread_create(&handler_thread, NULL, &Q_handle_incoming_messages_helper,
      (void *) this) != 0)
  {
    fprintf(stderr,
        "Failed to create the thread for processing requests\n");
    exit(1);
  }
  fprintf(stderr, "Created the thread for processing requests\n");

  pthread_t handler_thread2;
  if (pthread_create(&handler_thread2, NULL, &Q_message_queue_handler_helper,
      (void *) this) != 0)
  {
    fprintf(stderr,
        "Failed to create the thread for receiving from network\n");
    exit(1);
  }
  fprintf(stderr, "Created the thread for receiving from network\n");

}

Q_Replica::~Q_Replica()
{
  cur_state = replica_state_STOP;
  delete rtimer;
  if (missing)
    delete missing;
  if (ah_2)
    delete ah_2;
  delete replies;
  delete rh;
}

void Q_Replica::register_exec(
    int(*e)(Byz_req *, Byz_rep *, Byz_buffer *, int, bool))
{
  exec_command = e;
}

void Q_Replica::register_perform_checkpoint(int(*p)())
{
  perform_checkpoint = p;
}

void Q_Replica::handle_incoming_messages()
{
  Q_Message* msg;
  while (1)
  {
    pthread_mutex_lock(&incoming_queue_mutex);
    {
      while (incoming_queue.size() == 0)
      {
        pthread_cond_wait(&not_empty_incoming_queue_cond, &incoming_queue_mutex);
      }
      msg = incoming_queue.remove();
    }
    pthread_mutex_unlock(&incoming_queue_mutex);

    UPDATE_IN(C,POST,msg->size());

    handle(msg);
  }
}

void Q_Replica::handle(Q_Message *msg) {
  switch (msg->tag())
  {
  case Q_Request_tag:
    ENTRY_TIME;
    gen_handle<Q_Request> (msg);
    EXIT_TIME;
    break;

  case Q_Checkpoint_tag:
    gen_handle<Q_Checkpoint> (msg);
    break;

  case Q_Panic_tag:
    gen_handle<Q_Panic> (msg);
    break;

  case Q_Abort_tag:
    gen_handle<Q_Abort> (msg);
    break;

  case Q_Missing_tag:
    gen_handle<Q_Missing> (msg);
    break;

  case Q_Get_a_grip_tag:
    gen_handle<Q_Get_a_grip> (msg);
    break;

  default:
    // Unknown message type.
    delete msg;
  }
}

void Q_Replica::handle(Q_Request *m)
{
  pthread_mutex_lock(&checkpointing_mutex);
  if (checkpoint_in_progress)
  {
    if (current_nb_requests_while_checkpointing
        < rh->max_nb_requests_while_checkpointing())
    {
      // can handle the request
      current_nb_requests_while_checkpointing++;
    }
    else
    {
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

  // accept only if in normal state
  if (cur_state == replica_state_STOP)
  {
    int cid = m->client_id();

    Q_Reply qr(0, m->request_id(), node_id, replies->digest(cid), i_to_p(cid),
        cid);
    qr.set_instance_id(next_instance_id());
    qr.authenticate(i_to_p(cid), 0);

    send(&qr, cid);

    delete m;
    return;
  }
  else if (cur_state != replica_state_NORMAL)
  {
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

  //PP delay attack
#if 0
  if (id() == 0)
      usleep(10000);
#endif

  req_count_switch++;

  if (!m->verify())
  {
    fprintf(stderr, "Q_Replica::handle(): request verification failed.\n");
    delete m;
    return;
  }

  if (m->is_read_only())
  {
    fprintf(stderr,
        "Q_Replica::handle(): read-only requests are not handled.\n");
    delete m;
    return;
  }

  int cid = m->client_id();
  Request_id rid = m->request_id();

#ifdef TRACES
  fprintf(stderr, "Q_Replica::handle() (cid = %d, rid=%llu)\n", cid, rid);
#endif

  Request_id last_rid = replies->req_id(cid);

  if (last_rid <= rid)
  {
    if (last_rid == rid)
    {
      // Request has already been executed.
      nb_retransmissions++;
      if (nb_retransmissions % 100 == 0)
      {
        fprintf(stderr, "Q_Replica: nb retransmissions = %d\n",
            nb_retransmissions);
      }
    }

    // Request has not been executed.
    if (!execute_request(m))
      delete m;
    return;
  }

  // XXX: what to do here? should we delete the request?
  // that may invalidate the pointer in request history
  delete m;
}

bool Q_Replica::execute_request(Q_Request *req)
{
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

#ifdef DO_STATISTICS
    int sent =
#endif
    replies->send_reply(cid, 0, id());
    UPDATE_OUT(C,POST,sent);
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
  exec_command(&inb, &outb, (Byz_buffer*) &seqno, cid, false);

  // perform_checkpoint();

  if (outb.size % ALIGNMENT_BYTES)
  {
    for (int i = 0; i < ALIGNMENT_BYTES - (outb.size % ALIGNMENT_BYTES); i++)
    {
      outb.contents[outb.size + i] = 0;
    }
  }

  Digest d;
  if (rh->add_request(req, seqno, d)) {
    seqno++;
  }

  // Finish constructing the reply.
  replies->end_reply(cid, rid, outb.size, d);

  //   int replier = 1+ (req.request_id() % (num_replicas - 1));
  int replier = 2;

  if (outb.size != 0)
  {
    if (outb.size < 50 || replier == node_id || replier < 0)
    {
      // Send full reply.
#ifdef TRACES
      fprintf(stderr, "Replica::execute_prepared: %d Sending full reply (outb.size = %d, req.replier = %d)\n", id(), outb.size, req->replier());
#endif
#ifdef DO_STATISTICS
      int sent =
#endif
      replies->send_reply(cid, 0, id(), true);
      UPDATE_OUT(C,POST,sent);
    }
    else
    {
      // Send empty reply.
#ifdef TRACES
      fprintf(stderr, "Replica::execute_prepared: %d Sending emtpy reply (outb.size = %d, req.replier = %d)\n", id(), outb.size, req->replier());
#endif
      Q_Reply empty(0, req->request_id(), node_id, replies->digest(cid),
          i_to_p(cid), cid);
      send(&empty, cid);
      UPDATE_OUT(C,POST,empty.size());
    }
  }

  if (req->request_id() % NOTIF_PERIOD == 0)
  {
    pthread_mutex_lock(&checkpointing_mutex);
    robust_monitor->add_client_rep_notif();
    pthread_mutex_unlock(&checkpointing_mutex);
  }

  // send the checkpoint if necessary
  if (rh->should_checkpoint())
  {
    rh->truncate_history(rh->get_top_seqno());
    //Deactivate checkpointing
      /*
    Q_Checkpoint *chkp = new Q_Checkpoint();
    // fill in the checkpoint message
    chkp->set_seqno(rh->get_top_seqno());
    chkp->set_digest(rh->rh_digest());
    chkp->re_authenticate(NULL, true);
    checkpoint_in_progress = true;
    robust_monitor->send_message(chkp->contents(), chkp->size(), -1, instance_id());
    handle(chkp);
    */
  }

  return true;
}

void Q_Replica::handle(Q_Checkpoint *c)
{
  // verify signature
  if (!c->verify())
  {
    fprintf(stderr, "Couldn't verify the signature of Q_Checkpoint\n");
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
      Q_Checkpoint *cc = cset->get(i);
      if (cc != NULL)
      {
        same = c->match(cc);
        if (!same)
          break;
      }
    }

    //PL Well, if it does not match, then maybe we should clear the cset,
    // remove it from the cset, etc...

    //fprintf(stderr, "rh.size()=%d\n", rh->size());
    rh->truncate_history(cset->get_seqno());
    current_nb_requests_while_checkpointing = 0;
    checkpoint_in_progress = false;
    last_chkp_seqno = cset->get_seqno();

    current_checkpoints.erase(cset->get_seqno());
    delete cset;

    ctimer->stop();
    ctimer->start();

    if (robust_monitor->should_panic()) {
       fprintf(stderr, "Q_Replica[%d] should panic, send PANIC!\n", id());
       Q_Panic *qp = new Q_Panic(id(), 0);
       handle(qp);
    }
  }
  pthread_mutex_unlock(&checkpointing_mutex);
}

void Q_Replica::handle(Q_Panic *m)
{
  //   fprintf(stderr, "Q_Panic[%d]: Handling Panic message\n", id());

  //XXX PL NEVER SWITCH
  delete m;
  return;

  int cid = m->client_id();
  Request_id rid = m->request_id();

  //fprintf(stderr, "Q_Replica[%d]:: receiving panic for (cid = %d rid = %llu)\n", id(), cid, rid);
  pthread_mutex_lock(&switching_mutex);
  if (cur_state != replica_state_PANICKING && cur_state != replica_state_STOP)
  {
    Q_Panic qp(id(), 0);
    robust_monitor->send_message(qp.contents(), qp.size(), -1, instance_id());
    n_retrans = 0;
    cur_state = replica_state_PANICKING;
    rtimer->restop();
    rtimer->start();
  }
  // put the client in the list
  if (!is_replica(cid))
  {
    if (cur_state == replica_state_STOP)
    {
      Q_Reply qr(0, rid, node_id, replies->digest(cid), i_to_p(cid), cid);
      qr.set_instance_id(next_instance_id());
      qr.authenticate(i_to_p(cid), 0);

      send(&qr, cid);
      delete m;
      pthread_mutex_unlock(&switching_mutex);
      return;
    }
    else
    {
      OutstandingRequests outs;
      outs.cid = cid;
      outs.rid = rid;
      outstanding.push_back(outs);
    }
  }
  pthread_mutex_unlock(&switching_mutex);

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

void Q_Replica::handle(Q_Abort *m)
{
  if (cur_state == replica_state_PANICKING)
  {

    if (!m->verify())
    {
      fprintf(stderr, "Q_Replica[%d]: Unable to verify an Abort message\n",
          id());
      delete m;
      return;
    }

#ifdef TRACE
    fprintf(stderr, "Q_Replica[%d]: received Abort message from %d (cid = %d, rid = %llu) (hist_size = %d)\n", id(), m->id(), m->client_id(), m->request_id(), m->hist_size());
#endif

    pthread_mutex_lock(&switching_mutex);
    if (!aborts.add(m))
    {
      fprintf(stderr,
          "Q_Replica[%d]: Failed to add Abort from %d to the certificate\n",
          id(), m->id());
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
      for (int i = 0; i < aborts.size(); i++)
      {
        Ac_entry *ace = aborts[i];
        if (ace == NULL)
          continue;
        Q_Abort *abort = ace->abort;
        if (max_size < abort->hist_size())
          max_size = abort->hist_size();
      }

      // now, extract the history
      Q_Smasher qsmash(max_size, f(), aborts);
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
      if (missing->size() == 0)
      {
        // just replace the history with AH_2

        Req_history_log<Q_Request> *newrh = new Req_history_log<Q_Request> ();
        for (int i = 0; i < ah_2->size(); i++)
        {
          // if it is in rh, just copy it
          Q_Request *ar = NULL;
          Rh_entry<Q_Request> *rhe = NULL;
          AbortHistoryElement *ahe = NULL;
          // XXX: delete superflous ones
          ahe = ah_2->slot(i);
          rhe = rh->find(ahe->cid, ahe->rid);
          if (rhe != NULL)
          {
            ar = rhe->req;
            newrh->add_request(ar, rhe->seqno(), rhe->digest());
            rhe->req = NULL;
          }
        }
        missing_store.clear();
        missing_store_seq.clear();
        missing_mask.clear();
        num_missing = 0;

        for (int i = 0; i < ah_2->size(); i++)
          if (ah_2->slot(i) != NULL)
            delete ah_2->slot(i);
        delete ah_2;
        delete rh;
        rh = newrh;
        seqno = rh->get_top_seqno() + 1;

//        fprintf(stderr, "Q_Replica[%d]: seqno is %llu\n", id(), seqno);

        delete missing;
        missing = NULL;

        cur_state = replica_state_STOP;
        pthread_mutex_unlock(&switching_mutex);
        great_switcher->switch_state(instance_id(), false);
        great_switcher->switch_state(next_instance_id(), true);

        // now, we should notify all waiting clients
        notify_outstanding();

        return;
      }

      Q_Missing qmis(id(), missing);
      robust_monitor->send_message(qmis.contents(), qmis.size(), -1, instance_id());

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
    // fprintf(stderr, "Q_Replica[%d]: Receiving an Abort message during Panic mode\n", id());
    delete m;
  }
}

void Q_Replica::retransmit_panic()
{
  //fprintf(stderr, "Q_Replica[%d]: will retransmit PANIC!\n", id());
  static const int nk_thresh = 3;

  pthread_mutex_lock(&switching_mutex);
  n_retrans++;
  if (n_retrans == nk_thresh)
  {
    rtimer->stop();
    aborts.clear();
    delete rh;
    if (missing)
      delete missing;
    missing = NULL;

    rh = new Req_history_log<Q_Request> ();
    cur_state = replica_state_STOP;
    great_switcher->switch_state(instance_id(), false);
    great_switcher->switch_state(next_instance_id(), true);
    n_retrans = 0;
    return;
  }

  Q_Panic qp(id(), 0);
  send(&qp, Q_All_replicas);
  robust_monitor->send_message(qp.contents(), qp.size(), -1, instance_id());
  cur_state = replica_state_PANICKING;
  pthread_mutex_unlock(&switching_mutex);

  rtimer->restart();
}

void Q_Replica::handle(Q_Missing *m)
{
  // extract the replica
  int replica_id = m->id();
  ssize_t offset = sizeof(Q_Missing_rep);
  char *contents = m->contents();
  for (int i = 0; i < m->hist_size(); i++)
  {
    int cur_cid;
    Request_id cur_rid;

    // extract the requests
    memcpy((char *) &cur_cid, contents + offset, sizeof(int));
    offset += sizeof(int);
    memcpy((char *) &cur_rid, contents + offset, sizeof(Request_id));
    offset += sizeof(Request_id);

    // find them in the history
    Rh_entry<Q_Request> *rhe = rh->find(cur_cid, cur_rid);
    if (rhe != NULL)
    {
      // and construct Get-a-grip messages out of request
      Q_Get_a_grip qgag(cur_cid, cur_rid, id(), rhe->seqno(),
          (Q_Request*) rhe->req);
      robust_monitor->send_message(qgag.contents(), qgag.size(), replica_id, instance_id());
    }
  }
  delete m;
}

void Q_Replica::handle(Q_Get_a_grip *m)
{
  if (cur_state != replica_state_MISWAITING)
  {
//    fprintf(stderr, "Q_Replica[%d]::Q_get_a_grip: got unneeded QGAG message\n",
//        id());
    delete m;
    return;
  }

  // extract the data
  int cid = m->client_id();
  Request_id rid = m->request_id();
  Seqno r_seqno = m->seqno();

  int rep_size = ((Q_Message_rep *) m->stored_request())->size;
  //((Reply_rep *)gag->reply())->replica = gag->id();
  void *rep_ = malloc(rep_size);
  memcpy(rep_, m->stored_request(), rep_size);
  Q_Request *req = new Q_Request((Q_Request_rep *) rep_);

  // now, make sure the message is good
  bool found = false;
  int i = 0;
  for (i = 0; i < missing->size(); i++)
  {
    if (missing_mask[i] == false)
      continue;

    AbortHistoryElement *ahe = missing->slot(i);
    if (ahe->cid == cid && ahe->rid == rid && ahe->d == req->digest())
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

  if (found && num_missing == 0)
  {
    // just replace the history with AH_2
    Req_history_log<Q_Request> *newrh = new Req_history_log<Q_Request> ();
    for (int i = 0; i < ah_2->size(); i++)
    {
      // if it is in rh, just copy it
      Q_Request *ar = NULL;
      Rh_entry<Q_Request> *rhe = NULL;
      if ((rhe = rh->find(ah_2->slot(i)->cid, ah_2->slot(i)->rid)) != NULL)
      {
        ar = rhe->req;
        newrh->add_request(ar, rhe->seqno(), rhe->digest());
        rhe->req = NULL;
        continue;
      }
      else
      {
        // we should find it in missing_store
        for (int j = 0; j < missing_store.size(); j++)
        {
          if (missing_store[j]->client_id() == ah_2->slot(i)->cid
              && missing_store[j]->request_id() == ah_2->slot(i)->rid)
          {
            newrh->add_request(missing_store[j], missing_store_seq[j],
                missing_store[j]->digest());
            break;
          }
        }
      }
    }
    missing_store.clear();
    missing_store_seq.clear();
    missing_mask.clear();
    num_missing = 0;

    for (int i = 0; i < ah_2->size(); i++)
      if (ah_2->slot(i) != NULL)
        delete ah_2->slot(i);
    delete ah_2;
    delete rh;
    rh = newrh;
    seqno = rh->get_top_seqno() + 1;

    delete missing;
    missing = NULL;

    cur_state = replica_state_STOP;
    great_switcher->switch_state(instance_id(), false);
    great_switcher->switch_state(next_instance_id(), true);

    notify_outstanding();
  }

  // if so, store it, or discard it...
  delete m;
}

void Q_Replica::broadcast_abort(Request_id out_rid)
{
  //fprintf(stderr, "Q_Replica[%d]::broadcast_abort for (cid = %d rid = %llu)\n", id(), cid, out_rid);
  Q_Abort *a_message = new Q_Abort(node_id, out_rid, *rh);

  a_message->sign();
  robust_monitor->send_message(a_message->contents(), a_message->size(), -1, instance_id());
  // since we're not receiving it back:
  pthread_mutex_lock(&switching_mutex);
  if (!aborts.add(a_message))
    delete a_message;
  pthread_mutex_unlock(&switching_mutex);
}

void Q_Replica::notify_outstanding()
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

    Q_Reply qr(0, rid, node_id, replies->digest(cid), i_to_p(cid), cid);
    qr.set_instance_id(next_instance_id());
    qr.authenticate(i_to_p(cid), 0);

    send(&qr, cid);
  }

  // cleanup
  outstanding.clear();
  pthread_mutex_unlock(&switching_mutex);
}

void Q_Replica::join_mcast_group()
{
  struct ip_mreq req;
  bzero(&req, sizeof(req));

  req.imr_multiaddr.s_addr = group->address()->sin_addr.s_addr;
  req.imr_interface.s_addr = principals[node_id]->address()->sin_addr.s_addr;
  //req.imr_interface.s_addr = htonl(INADDR_ANY);
  int error = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *) &req,
      sizeof(req));
  if (error < 0)
  {
    perror("Unable to join group");
    exit(1);
  }

  struct in_addr interface_addr;
  bzero(&interface_addr, sizeof(interface_addr));
  interface_addr.s_addr = principals[id()]->address()->sin_addr.s_addr;
  fprintf(stderr, "Address is %lx\n", interface_addr.s_addr);
  return;
  error = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF,
      (char *) &interface_addr, sizeof(interface_addr));
  if (error < 0)
  {
    perror("Unable to bind to interface");
    //exit(1);
  }
}

void Q_Replica::leave_mcast_group()
{
  struct ip_mreq req;
  req.imr_multiaddr.s_addr = group->address()->sin_addr.s_addr;
  req.imr_interface.s_addr = principals[node_id]->address()->sin_addr.s_addr;
  //req.imr_interface.s_addr = htonl(INADDR_ANY);
  int error = setsockopt(sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, (char *) &req,
      sizeof(req));
  if (error < 0)
  {
    perror("Unable to leave group");
    exit(1);
  }
}

void Q_Replica::enable_replica(bool state)
{
  fprintf(stderr, "Q_Replica[%d]: will try to switch state, switch %d, when in state %d, after %ld reqs\n", id(), state, cur_state, req_count_switch);
  if (cur_state == replica_state_NORMAL || cur_state == replica_state_STOP)
  {
    if (state)
    {
      if (cur_state != replica_state_NORMAL)
      {
        delete rh;
        rh = new Req_history_log<Q_Request> ();
        current_checkpoints.clear();
        outstanding.clear();
        seqno = 0;
        req_count_switch = 0;
      }
      cur_state = replica_state_NORMAL;
      robust_monitor->switch_protocol(instance_id());
      ctimer->stop();
    }
    else
    {
      cur_state = replica_state_STOP;
      ctimer->stop();
    }
  }
}

void Q_Replica::no_checkpoint_timeout() {
   ctimer->stop();

   //Deactivate switching
   return;

   fprintf(stderr, "Q_Replica[%d] no checkpoint, send PANIC!\n", id());
   Q_Panic *qp = new Q_Panic(id(), 0);
   handle(qp);
}
