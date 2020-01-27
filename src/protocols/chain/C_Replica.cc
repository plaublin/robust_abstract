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
#include <netinet/tcp.h>
#include <errno.h>
#include <sys/syscall.h>

#include "th_assert.h"
#include "C_Message_tags.h"
#include "C_Request.h"
#include "C_Checkpoint.h"
#include "C_Reply.h"
#include "C_Panic.h"
#include "C_Principal.h"
#include "C_Replica.h"
#include "RobustMonitor.h"
#include "MD5.h"
#include "Switcher.h"
#include "C_Smasher.h"
#include "C_Missing.h"
#include "C_Get_a_grip.h"
#include "C_Client_notification.h"

#define _MEASUREMENTS_ID_ (C_replica->id())
#include "measurements.h"
#include "statistics.h"

//#define TRACES

// Global replica object.
C_Replica *C_replica;

#include <signal.h>
static void kill_replica(int sig)
{
  fprintf(stderr, "Max req history size = %d\n", C_replica->max_history_size);

  REPORT_STATS;REPORT_TIMINGS;
  exit(0);
}

int gettid(void) {
    return (int)syscall(SYS_gettid);
}

// Function for the thread receiving messages from the predecessor in the chain
//extern "C"
void*requests_from_predecessor_handler_helper(void *o)
{
    fprintf(stderr, "Thread %d: requests from predecessor handler\n", gettid());
  void **o2 = (void **) o;
  C_Replica &r = (C_Replica&) (*o2);
  r.requests_from_predecessor_handler();
  return 0;
}

void* robust_handler_helper(void *o)
{
    fprintf(stderr, "Thread %d: robust handler handler\n", gettid());
  void **o2 = (void **) o;
  C_Replica &r = (C_Replica&) (*o2);
  r.robust_handler();
  return 0;
}

static void handle_robust_monitor_message(int s)
{
  C_Message *m = C_replica->C_Node::recv(s);

  pthread_mutex_lock(&C_replica->robust_message_queue_mutex);
  {
    C_replica->robust_messages_queue.append(m);
    pthread_cond_signal(&C_replica->robust_message_queue_cond);
  }
  pthread_mutex_unlock(&C_replica->robust_message_queue_mutex);
}

// Function for the thread receiving messages from clients
//extern "C"
void*C_client_requests_handler_helper(void *o)
{
    fprintf(stderr, "Thread %d: client requests handler handler\n", gettid());
  void **o2 = (void **) o;
  C_Replica &r = (C_Replica&) (*o2);
  r.c_client_requests_handler();
  return 0;
}

inline void*C_message_queue_handler_helper(void *o)
{
    fprintf(stderr, "Thread %d: message queue handler handler\n", gettid());
  void **o2 = (void **) o;
  C_Replica &r = (C_Replica&) (*o2);
  //temp_replica_class = (Replica<class Request_T, class Reply_T>&)(*o);
  //  r.recv1();
  //temp_replica_class.do_recv_from_queue();
  r.do_recv_from_queue();
  return 0;
}

void switch_c_replica(bool state)
{
  if (C_replica != NULL)
    C_replica->enable_replica(state);
}

void C_no_checkpoint_timeout_handler()
{
  th_assert(C_replica, "C_replica is not initialized");
  C_replica->no_checkpoint_timeout();
}

void C_no_request_timeout_handler()
{
  th_assert(C_replica, "C_replica is not initialized");
  C_replica->no_request_timeout();
}

C_Replica::C_Replica(FILE *config_file, FILE *config_priv, char* host_name,
    short port) :
  C_Node(config_file, config_priv, host_name, port), seqno(0), last_seqno(0),
      replies(num_principals), cur_state(replica_state_NORMAL),
      aborts(3 * f() + 1, 3 * f() + 1), ah_2(NULL), missing(NULL),
      num_missing(0), missing_mask(), missing_store(), missing_store_seq(),
      robust_messages_queue()

{
  // Fail if node is not a replica.
  if (!is_replica(id()))
  {
    th_fail("Node is not a replica");
  }

  rh = new Req_history_log<C_Request>();

  ctimer = new C_ITimer(CHECKPOINTING_TIMEOUT, C_no_checkpoint_timeout_handler);
  ftimer = new C_ITimer(REQUEST_FEEDBACK_TIMEOUT, C_no_request_timeout_handler);

  seqno = 0;
  req_count_switch = 0;

  // Read view change, status, and recovery timeouts from replica's portion
  // of "config_file"
  int vt, st, rt;
  fscanf(config_file, "%d\n", &vt);
  fscanf(config_file, "%d\n", &st);
  fscanf(config_file, "%d\n", &rt);

  // Create timers and randomize times to avoid collisions.
  srand48(getpid());

  exec_command = 0;

#if 1
  struct sigaction act;
  act.sa_handler = kill_replica;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;
  sigaction(SIGINT, &act, NULL);
  sigaction(SIGTERM, &act, NULL);
#endif

  nb_sent_replies = 0;
  max_history_size = 0;

  checkpoint_in_progress = false;
  current_nb_requests_while_checkpointing = 0;
  last_chkp_seqno = 0;

  pthread_mutex_init(&checkpointing_mutex, NULL);

  pthread_mutex_init(&robust_message_queue_mutex, NULL);
  pthread_cond_init(&robust_message_queue_cond, NULL) ;

  // Create server socket
  in_socket = createServerSocket(ntohs(principals[node_id]->TCP_addr.sin_port));

  // Create receiving thread
  if (pthread_create(&requests_from_predecessor_handler_thread, 0,
      &requests_from_predecessor_handler_helper, (void *) this) != 0)
  {
    fprintf(
        stderr,
        "Failed to create the thread for receiving messages from predecessor in the chain\n");
    exit(1);
  }

  // Create receiving thread
  if (pthread_create(&C_robust_handler_thread, 0,
      &robust_handler_helper, (void *) this) != 0)
  {
    fprintf(
        stderr,
        "Failed to create the thread for handling robust messages\n");
    exit(1);
  }

  // Connect to principals[(node_id + 1) % num_r]
  out_socket = createClientSocket(
      principals[(node_id + 1) % num_replicas]->TCP_addr);

  //if (node_id == 0 || node_id == (num_replicas - 1))
  //{
    fprintf(stderr, "Creating client socket\n");
    in_socket_for_clients = createNonBlockingServerSocket(
        ntohs(principals[node_id]->TCP_addr_for_clients.sin_port));
    if (pthread_create(&C_client_requests_handler_thread, NULL,
        &C_client_requests_handler_helper, (void *) this) != 0)
    {
      fprintf(stderr,
          "Failed to create the thread for receiving client requests\n");
      exit(1);
    }
  //}

  great_switcher->register_switcher(instance_id(), switch_c_replica);
  robust_monitor->register_handle_msg_func(this->instance_id(),
      handle_robust_monitor_message);
}

C_Replica::~C_Replica()
{
  cur_state = replica_state_STOP;
  delete rh;
}

void C_Replica::do_recv_from_queue()
{
  C_Message *m;

  while (1)
  {
    pthread_mutex_lock(&incoming_queue_mutex);
    {
      while (incoming_queue.size() == 0)
      {
        pthread_cond_wait(&not_empty_incoming_queue_cond, &incoming_queue_mutex);
      }
      m = incoming_queue.remove();
    }
    pthread_mutex_unlock(&incoming_queue_mutex);
    handle(m);
  }
}

void C_Replica::register_exec(
    int(*e)(Byz_req *, Byz_rep *, Byz_buffer *, int, bool))
{
  exec_command = e;
}

void C_Replica::requests_from_predecessor_handler()
{
  int socket = -1;

  listen(in_socket, 1);
  while ((socket = accept(in_socket, NULL, NULL)) < 0)
  {
    if (errno != EINTR)
    {
      perror("Cannot accept connection \n");
      exit(-1);
    }
  }

  // Loop to receive messages.
  while (1)
  {
    C_Message* m = C_Node::recv(socket);
    ENTRY_TIME_POS(1);
#ifdef TRACES
    fprintf(stderr, "Received message\n");
#endif
#ifdef DO_STATISTICS
    if (id() == 0)
    {
      UPDATE_IN(C,POST,m->size());
    }
    else
    {
      UPDATE_IN(R,POST,m->size());
    }
#endif

    // Enqueue the message
    pthread_mutex_lock(&incoming_queue_mutex);
    {
      incoming_queue.append(m);
      pthread_cond_signal(&not_empty_incoming_queue_cond);
    }
    pthread_mutex_unlock(&incoming_queue_mutex);
    EXIT_TIME_POS(1);
  }
}

void C_Replica::handle(C_Message *m)
{
  switch (m->tag())
  {
  case C_Request_tag:
    ENTRY_TIME;
    gen_handle<C_Request> (m);
    EXIT_TIME;
    break;
  case C_Checkpoint_tag:
    gen_handle<C_Checkpoint> (m);
    break;
  case C_Panic_tag:
    gen_handle<C_Panic> (m);
    break;
  case C_Abort_tag:
    gen_handle<C_Abort> (m);
    break;
  case C_Missing_tag:
    gen_handle<C_Missing> (m);
    break;
  case C_Get_a_grip_tag:
    gen_handle<C_Get_a_grip> (m);
    break;
  case C_Client_notification_tag:
    gen_handle<C_Client_notification>(m);
    break;
  default:
    // Unknown message type.
    delete m;
  }
}

void C_Replica::handle(C_Request *req, bool handle_pending)
{
  int cid = req->client_id();

#ifdef TRACES
  fprintf(stderr, "C_Replica %d: handle request <%lld, %d, %lld>\n", id(), req->seqno(), cid, req->request_id());
#endif

  if (cur_state == replica_state_STOP)
  {
    if (node_id < num_replicas - 1)
    {
#ifdef TRACES
        fprintf(stderr, "C_Replica %d: Forwarding the request in STOP state\n", id());
#endif
      int len = req->size();
      send_all(out_socket, req->contents(), &len);
    }
    else
    {
#ifdef TRACES
      fprintf(stderr, "C_Replying to the client in STOP state\n");
#endif
      if (connectlist[req->listnum()] == 0)
      {
        fprintf(
            stderr,
            "Error while sending a reply to client %d in STOP state: I do not have a valid socket!\n",
            req->client_id());
        exit(1);
      }

      C_Reply *rep = replies.reply(cid);
      rep->set_instance_id(next_instance_id());
      replies.send_reply(cid, connectlist[req->listnum()], req->MACs());
      rep->reset_switch();
    }

    delete req;
    return;
  }
  else if (cur_state != replica_state_NORMAL)
  {
    //fprintf(stderr, "Weird state: %d, cannot handle the request <%d, %lld>\n", cur_state, cid, req->request_id());
    delete req;
    return;
  }

  pthread_mutex_lock(&checkpointing_mutex);

  if (checkpoint_in_progress) {
     if (current_nb_requests_while_checkpointing < rh->max_nb_requests_while_checkpointing()) {
        // can handle the request
        current_nb_requests_while_checkpointing++;
     } else {
      //  fprintf(stderr, "Checkpoint in progress, cannot handle the request <%lld, %d, %lld>\n", req->seqno(), cid, req->request_id());

        //fprintf(stderr, "The checkpoints are: ");
        //for (std::map<Seqno, CheckpointSet*>::iterator it = current_checkpoints.begin(); it != current_checkpoints.end(); it++) {
        //   CheckpointSet* cset = it->second;
        //   fprintf(stderr, " [%lld, %d]", cset->get_seqno(), cset->count());
        //}
        //fprintf(stderr, "\n");

        //The first replica has not assigned a sequence number yet:
        if (id() == 0) {
          primary_pending_requests.push_back(req);
        } else {
          if (pending_requests.find(req->seqno()) == pending_requests.end()) {
            pending_requests[req->seqno()] = req;
          } else {
            delete req;
          }
        }

        pthread_mutex_unlock(&checkpointing_mutex);
        return;
     }
  } else {
     if (!handle_pending) {
          if (id() == 0) {
           for (std::vector<C_Request*>::iterator it = primary_pending_requests.begin(); it != primary_pending_requests.end(); it++) {
              C_Request *r = *it;
                  //fprintf(stderr, "After checkpoint, handle the request <%lld, %d, %lld>\n", r->seqno(), r->client_id(), r->request_id());
              handle(r, true);
           }
           primary_pending_requests.clear();
        } else {
           for (std::map<Seqno, C_Request*>::iterator it = pending_requests.begin(); it != pending_requests.end(); it++) {
              C_Request *r = it->second;
                  //fprintf(stderr, "After checkpoint, handle the request <%lld, %d, %lld>\n", r->seqno(), r->client_id(), r->request_id());
              handle(r, true);
           }
           pending_requests.clear();
        }
     }
  }
  pthread_mutex_unlock(&checkpointing_mutex);

  // start the timer if it is stopped
  // does nothing if it is running
  // it ensures chain will not switch if it does
  // not receive any request at all (because we are
  // at the beginning of the experiment or switching)
  ctimer->restart();
  ftimer->restart();

  robust_monitor->add_client_req(cid, req->request_id());

  req_count_switch++;

  if (unlikely(req->is_read_only()))
  {
    fprintf(stderr,
        "C_Replica %d: Read-only requests are currently not handled\n", id());
    delete req;
    return;
  }

  int authentication_offset = 0;
  if (unlikely(!req->verify(&authentication_offset)))
  {
    fprintf(stderr, "C_Replica %d: verify returned FALSE\n", id());
    delete req;
    return;
  }

#ifdef TRACES
  fprintf(stderr, "*********** C_Replica %d: message verified\n", id());
#endif

  // different processing paths for the head and the rest of the nodes
  if (node_id == 0)
  {
    if (!is_seen(req))
    {
      // new request, should execute, and reply to the client. afterwards, send order request to other replicas
      seqno++;
      req->set_seqno(seqno);
      execute_request(req);
    }
    else
    {
      if (!is_old(req))
      {
        // Request has already been executed, and it was the last response, thus just reply back
        Seqno seq = replies.sent_seqno(req->client_id());
        req->set_seqno(seq);
      }
      else
      {
        // just discard...
    fprintf(stderr, "Just discard the request <%d, %lld>\n", cid, req->request_id());
        delete req;
        return;
      }
    }
  }
  else
  {
    if (!is_seen(req))
    {
      // new request, should execute, and reply to the client. afterwards, send order request to other replicas
      if (req->seqno() != last_seqno + 1)
      {
        fprintf(
            stderr,
            "C_Replica[%d]: out of order message (got: %lld), (expected: %lld)\n",
            id(), req->seqno(), last_seqno + 1);
        // maybe panic?
        delete req;
        return;
      }
      // Execute req
      seqno = last_seqno + 1;
      execute_request(req);
    }
    else
    {
      if (!is_old(req))
      {
        // Request has already been executed, and it was the last response, thus just reply back
      }
    }
  }

  C_Reply_rep *rr = replies.reply_rep(req->client_id());
  req->authenticate(C_node->id(), authentication_offset, rr);

  if (node_id < num_replicas - 1)
  {
    // Forwarding the request
#ifdef TRACES
    fprintf(stderr, "C_Replica %d: Forwarding the request\n", id());
#endif
    int len = req->size();
    EXIT_TIME;
    send_all(out_socket, req->contents(), &len);
    UPDATE_OUT(R,POST,len);
    //return;
  }
  else
  {
    // C_Replying to the client
#ifdef TRACES
    fprintf(stderr, "C_Replying to the client\n");
#endif

    EXIT_TIME;

    if (connectlist[req->listnum()] == 0)
    {
      fprintf(
          stderr,
          "Error while sending a reply to client %d: I do not have a valid socket!\n",
          req->client_id());
      exit(1);
    }

    if (cur_state == replica_state_STOP)
    {
      C_Reply *rep = replies.reply(cid);
      rep->set_instance_id(next_instance_id());
    }

    nb_sent_replies++;
#ifdef DO_STATISTICS
    int sent =
#endif
    replies.send_reply(cid, connectlist[req->listnum()], req->MACs());
    UPDATE_OUT(C,POST,sent);

    if (cur_state == replica_state_STOP)
    {
      C_Reply *rep = replies.reply(cid);
      rep->reset_switch();
    }
    //return;
  }

  delete req;
  EXIT_TIME;
}

void C_Replica::execute_request(C_Request *req)
{
  int cid = req->client_id();
  Request_id rid = req->request_id();

#ifdef TRACES
  fprintf(stderr, "C_Replica[%d]: executing request %lld for client %d\n", id(), rid, cid);
#endif

  // Obtain "in" and "out" buffers to call exec_command
  Byz_req inb;
  Byz_rep outb;
  Byz_buffer non_det;
  inb.contents = req->command(inb.size);
  outb.contents = replies.new_reply(cid, outb.size, seqno);
  //non_det.contents = pp->choices(non_det.size);

  // Execute command in a regular request.
  //C_BEGIN_TIME(exec);
  exec_command(&inb, &outb, &non_det, cid, false);
  //C_END_TIME(exec);

  // perform_checkpoint();

#if 0
  if (outb.size % ALIGNMENT_BYTES)
  {
    for (int i=0; i < ALIGNMENT_BYTES - (outb.size % ALIGNMENT_BYTES); i++)
    {
      outb.contents[outb.size+i] = 0;
    }
  }
#endif

  last_seqno = seqno;

  C_Request *copy = new C_Request();
  memcpy(copy->contents(), req->contents(), req->size());
  copy->trim();

  pthread_mutex_lock(&robust_message_queue_mutex);
  {
    robust_messages_queue.append(copy);
    pthread_cond_signal(&robust_message_queue_cond);
  }
  pthread_mutex_unlock(&robust_message_queue_mutex);

  // Finish constructing the reply.
  replies.end_reply(cid, rid, outb.size);
}

// Returns true if request is old, i.e., there was newer request from the same client seen
bool C_Replica::is_old(C_Request *req)
{
  int cid = req->client_id();
  Request_id rid = req->request_id();

  if (rid < replies.req_id(cid))
  {
    return true;
  }

  return false;
}

// Returns true is request was seen
bool C_Replica::is_seen(C_Request *req)
{
  int cid = req->client_id();
  Request_id rid = req->request_id();

  if (rid <= replies.req_id(cid))
  {
    return true;
  }

  return false;
}

void C_Replica::handle(C_Client_notification *m) {
  pthread_mutex_lock(&checkpointing_mutex);
  ctimer->restart();
  switch(m->get_op_type()) {
      case C_Client_notification_request:
          robust_monitor->add_client_req_notif(m->get_cid(), m->get_rid());
          break;

      case C_Client_notification_reply:
          robust_monitor->add_client_rep_notif(m->get_cid());
          break;

      default:
          fprintf(stderr, "C_Replica[%d] has received a C_Client_notification with unknown op_type: %hd\n", id(), m->get_op_type());
          break;
  }
  pthread_mutex_unlock(&checkpointing_mutex);

  delete m;
}

void C_Replica::handle(C_Checkpoint *c)
{
  //fprintf(stderr, "Handle checkpoint from %d for %lld\n", c->id(), c->get_seqno());

  // verify signature
  if (!c->verify())
  {
    fprintf(stderr, "Couldn't verify the signature of C_Checkpoint\n");
    delete c;
    return;
  }

  if (c->get_seqno() <= last_chkp_seqno) {
     //fprintf(stderr, "Checkpoint is too old: %lld < %lld\n", c->get_seqno(), last_chkp_seqno);
     delete c;
     return;
  }

  pthread_mutex_lock(&checkpointing_mutex);
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
    delete c;
    pthread_mutex_unlock(&checkpointing_mutex);
    return;
  }

//  fprintf(stderr, "Handle checkpoint <%d, %lld>: cset size is %d\n", c->id(), c->get_seqno(), cset->count());

  if (cset->is_complete()) {
    bool same = false;
    for (int i = 0; i < n(); i++)
    {
      C_Checkpoint *cc = cset->get(i);
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

    //fprintf(stderr, "Checkpoint %lld finished!\n", cset->get_seqno());

    current_checkpoints.erase(cset->get_seqno());
    delete cset;

    ctimer->stop();
    ctimer->start();

    robust_monitor->compute_expectations();

    static int nb_calls = 0;
    if (/*++nb_calls == 100 ||*/ robust_monitor->should_panic() || robust_monitor->fairness_problem()) {
       fprintf(stderr, "C_Replica[%d] should panic, send PANIC!\n", id());
       robust_monitor->print_expectations();
       C_Panic *qp = new C_Panic(id(), 0);
       handle(qp);
       nb_calls = 0;
    }

    pthread_mutex_unlock(&checkpointing_mutex);
  } else {
    pthread_mutex_unlock(&checkpointing_mutex);
  }
}

void C_Replica::handle(C_Panic *m)
{
  int cid = m->client_id();
  Request_id rid = m->request_id();

  fprintf(stderr, "C_Replica[%d] handle C_Panic for (%d, %llu)\n", id(), cid,
      rid);

  if (cur_state != replica_state_PANICKING && cur_state != replica_state_STOP)
  {
    C_Panic qp(id(), rid);
    robust_monitor->send_message(qp.contents(), qp.size(), -1, instance_id());
    cur_state = replica_state_PANICKING;
  }
  // put the client in the list
  if (!is_replica(cid))
  {
    if (cur_state == replica_state_STOP)
    {
      delete m;
      return;
    }
  }

  // notify others
  broadcast_abort(rid);
  delete m;
}

void C_Replica::broadcast_abort(Request_id out_rid)
{
  fprintf(stderr, "C_Replica[%d]::broadcast_abort for (rid = %llu)\n", id(),
      out_rid);
  C_Abort *a_message = new C_Abort(node_id, out_rid, *rh);

  a_message->sign();
  robust_monitor->send_message(a_message->contents(), a_message->size(), -1, instance_id());
  // since we're not receiving it back:
  if (!aborts.add(a_message))
    delete a_message;
}

void C_Replica::handle(C_Abort *m)
{
  if (cur_state == replica_state_PANICKING)
  {
    if (!m->verify())
    {
      fprintf(stderr, "C_Replica[%d]: Unable to verify an Abort message\n",
          id());
      delete m;
      return;
    }

//#ifdef TRACE
    fprintf(stderr, "C_Replica[%d]: received Abort message from %d (rid = %llu) (hist_size = %d)\n", id(), m->id(), m->request_id(), m->hist_size());
//#endif

    if (!aborts.add(m))
    {
      fprintf(stderr,
          "C_Replica[%d]: Failed to add Abort from %d to the certificate\n",
          id(), m->id());
      delete m;
      return;
    }
    if (aborts.is_complete())
    {
      // since we have enough aborts, let's extract abort histories...
      // hist_size keeps how many entries are there
      unsigned int max_size = 0;
      for (int i = 0; i < aborts.size(); i++)
      {
        Ac_entry *ace = aborts[i];
        if (ace == NULL)
          continue;
        C_Abort *abort = ace->abort;
        if (max_size < abort->hist_size())
          max_size = abort->hist_size();
      }

      // now, extract the history
      C_Smasher qsmash(max_size, f(), aborts);
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
      if (missing->size() == 0)
      {
        // just replace the history with AH_2

        Req_history_log<C_Request> *newrh = new Req_history_log<C_Request> ();
        for (int i = 0; i < ah_2->size(); i++)
        {
          // if it is in rh, just copy it
          C_Request *ar = NULL;
          Rh_entry<C_Request> *rhe = NULL;
          AbortHistoryElement *ahe = NULL;
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

        //fprintf(stderr, "C_Replica[%d]: seqno is %llu\n", id(), seqno);

        delete missing;
        missing = NULL;

        cur_state = replica_state_STOP;
        great_switcher->switch_state(instance_id(), false);
        great_switcher->switch_state(next_instance_id(), true);

        return;
      }

      C_Missing qmis(id(), missing);
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
    //fprintf(stderr, "C_Replica[%d]: Receiving an Abort message during Panic mode\n", id());
    delete m;
  }
}

void C_Replica::handle(C_Missing *m)
{
  // extract the replica
  int replica_id = m->id();
  
  //fprintf(stderr, "C_Replica[%d]: handle C_Missing from %d\n", id(), replica_id);

  ssize_t offset = sizeof(C_Missing_rep);
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
    Rh_entry<C_Request> *rhe = rh->find(cur_cid, cur_rid);
    if (rhe != NULL)
    {
      // and construct Get-a-grip messages out of request
      C_Get_a_grip qgag(cur_cid, cur_rid, id(), rhe->seqno(),
          (C_Request*) rhe->req);
      //fprintf(stderr, "C_Replica[%d]: send a CGQG to %d\n", id(), replica_id);
      robust_monitor->send_message(qgag.contents(), qgag.size(), replica_id, instance_id());
    }
  }
  delete m;
}

void C_Replica::handle(C_Get_a_grip *m)
{
  if (cur_state != replica_state_MISWAITING)
  {
    //fprintf(stderr, "C_Replica[%d]::C_get_a_grip: got unneeded CGAG message\n",
    //    id());
    delete m;
    return;
  }

  // extract the data
  int cid = m->client_id();
  Request_id rid = m->request_id();
  Seqno r_seqno = m->seqno();

  int rep_size = ((C_Message_rep *) m->stored_request())->size;
  //((Reply_rep *)gag->reply())->replica = gag->id();
  void *rep_ = malloc(rep_size);
  memcpy(rep_, m->stored_request(), rep_size);
  C_Request *req = new C_Request((C_Request_rep *) rep_);

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
    Req_history_log<C_Request> *newrh = new Req_history_log<C_Request> ();
    for (int i = 0; i < ah_2->size(); i++)
    {
      // if it is in rh, just copy it
      C_Request *ar = NULL;
      Rh_entry<C_Request> *rhe = NULL;
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
  } else {
    //fprintf(stderr, "C_Replica[%d]: handle CGAG: still missing %d stuff\n", id(), num_missing);
  }

  // if so, store it, or discard it...
  delete m;
}


void C_Replica::c_client_requests_handler()
{
  struct timeval timeout;
  int readsocks;
  fd_set socks;

  fprintf(stderr, "Iam here\n");
  listen(in_socket_for_clients, MAX_CONNECTIONS);

  int highsock = in_socket_for_clients;
  memset((char *) &connectlist, 0, sizeof(connectlist));

  while (1)
  {
    FD_ZERO(&socks);
    FD_SET(in_socket_for_clients, &socks);

    // Loop through all the possible connections and add
    // those sockets to the fd_set
    for (int listnum = 0; listnum < MAX_CONNECTIONS; listnum++)
    {
      if (connectlist[listnum] != 0)
      {
        FD_SET(connectlist[listnum], &socks);
        if (connectlist[listnum] > highsock)
        {
          highsock = connectlist[listnum];
        }
      }
    }

    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    readsocks = select(highsock + 1, &socks, (fd_set *) 0, (fd_set *) 0,
        &timeout);
    if (readsocks < 0)
    {
      if (errno == EINTR)
      {
        continue;
      }
      else
      {
        perror("select");
        exit(1);
      }
    }
    if (readsocks == 0)
    {
      //fprintf(stderr, ".");
      fflush(stdout);
      ;
    }
    else
    {
      if (FD_ISSET(in_socket_for_clients, &socks))
      {
        handle_new_connection();
      }

      // Run through the sockets and check to see if anything
      // happened with them, if so 'service' them
      for (int listnum = 0; listnum < MAX_CONNECTIONS; listnum++)
      {
        // fprintf(stderr, "%d of %d, ",listnum,MAX_CONNECTIONS);
        if (FD_ISSET(connectlist[listnum], &socks))
        {
          C_Message* m = C_Node::recv(connectlist[listnum]);

          m->listnum() = listnum;
          // Enqueue the request
          pthread_mutex_lock(&incoming_queue_mutex);
          {
            // fprintf(stderr, "Got the mutex\n");
            incoming_queue.append(m);
            UPDATE_IN(C,POST,m->size());
            pthread_cond_signal(&not_empty_incoming_queue_cond);
          }
          pthread_mutex_unlock(&incoming_queue_mutex);
        }
      }
    }
  }
  pthread_exit(NULL);
}

void C_Replica::handle_new_connection()
{
  int listnum;
  int connection;
  sockaddr_in csin;
  socklen_t sinsize = sizeof(csin);

  // There is a new connection coming in
  // We  try to find a spot for it in connectlist
  connection
      = accept(in_socket_for_clients, (struct sockaddr*) &csin, &sinsize);
  if (connection < 0)
  {
    perror("accept");
    exit(EXIT_FAILURE);
  }
  setnonblocking(connection);

  char *hostname = inet_ntoa(csin.sin_addr);
  int client_port = ntohs(csin.sin_port);

  for (listnum = 0; listnum < num_principals; listnum++) {
      struct sockaddr_in caddr = principals[listnum]->TCP_addr_for_clients;
      if (caddr.sin_port == csin.sin_port && csin.sin_addr.s_addr == caddr.sin_addr.s_addr) {
          fprintf(stderr, "Connection accepted: FD=%d; Slot=%d, remote=%s:%i\n",
                  connection, listnum, hostname, client_port);
          connectlist[listnum] = connection;
          return;
      }
  }

  fprintf(stderr, "Unknown client.\n");
  close(connection);
}

void C_Replica::enable_replica(bool state)
{
  fprintf(stderr, "C_Replica[%d]: Time %lld will try to switch state, switch %d, when in state %d, after %ld requests\n", id(), diffC_Time(currentC_Time(), 0), state, cur_state, req_count_switch);
  if (cur_state == replica_state_NORMAL || cur_state == replica_state_STOP)
  {
    if (state)
    {
      if (cur_state != replica_state_NORMAL)
      {
        delete rh;
        rh = new Req_history_log<C_Request> ();

        pthread_mutex_lock(&checkpointing_mutex);
        {
            for (std::map<Seqno, CheckpointSet*>::iterator it = current_checkpoints.begin(); it != current_checkpoints.end(); it++) {
                delete it->second;
            }
            current_checkpoints.clear();

            for (std::vector<C_Request*>::iterator it = primary_pending_requests.begin(); it != primary_pending_requests.end(); it++) {
                delete *it;
            }
            primary_pending_requests.clear();

            for (std::map<Seqno, C_Request*>::iterator it = pending_requests.begin(); it != pending_requests.end(); it++) {
                delete it->second;
            }
            pending_requests.clear();

            rh->truncate_history(rh->get_top_seqno());
            ctimer->stop();
            ftimer->stop();
            checkpoint_in_progress = false;
        }
        pthread_mutex_unlock(&checkpointing_mutex);

        seqno = 0;
        last_seqno = 0;
        req_count_switch = 0;
      }
      cur_state = replica_state_NORMAL;
      robust_monitor->switch_protocol(instance_id());
    }
    else {
      cur_state = replica_state_STOP;
      pthread_mutex_lock(&checkpointing_mutex);
      {
        ctimer->stop();
        ftimer->stop();
        checkpoint_in_progress = false;
      }
      pthread_mutex_unlock(&checkpointing_mutex);
    }
  }
}

void C_Replica::robust_handler() {
   C_Message *m;

   while (1)
   {
    pthread_mutex_lock(&robust_message_queue_mutex);
    while (robust_messages_queue.size() == 0)
    {
        pthread_cond_wait(&robust_message_queue_cond, &robust_message_queue_mutex);
    }
    m = robust_messages_queue.remove();
    pthread_mutex_unlock(&robust_message_queue_mutex);

    if (m->tag() == C_Request_tag)
    {
      if (cur_state != replica_state_NORMAL) {
        delete m;
        continue;
      }

      C_Request *req = (C_Request*) m;

      rh->add_request(req, req->seqno(), req->digest());
      if (rh->should_checkpoint())
      {
        if (rh->get_top_seqno() <= last_chkp_seqno) {
          rh->truncate_history(last_chkp_seqno);
        } else {
          C_Checkpoint *chkp = new C_Checkpoint();
          // fill in the checkpoint message
          chkp->set_seqno(rh->get_top_seqno());
          chkp->set_digest(rh->rh_digest());
          chkp->re_authenticate(NULL, true);
          pthread_mutex_lock(&checkpointing_mutex);
          checkpoint_in_progress = true;
          pthread_mutex_unlock(&checkpointing_mutex);
        
          //fprintf(stderr, "C_Replica %d sends checkpoint %lld to all\n", id(), chkp->get_seqno());
          robust_monitor->send_message(chkp->contents(), chkp->size(), -1, instance_id());
          handle(chkp);
        }
      }
    }
    else
    {
      handle(m);
    }
  }
}

void C_Replica::no_checkpoint_timeout() {
   ctimer->stop();

   if (cur_state != replica_state_NORMAL)
   {
     return;
   }

   fprintf(stderr, "C_Replica[%d] no checkpoint, send PANIC!\n", id());
   robust_monitor->print_expectations();

   pthread_mutex_lock(&checkpointing_mutex);
   fprintf(stderr, "C_Replica[%d] cur_state=%d, primary pending reqs=%d, replica pending reqs=%d, current pending reqs while chkp=%d, checkpoint_in_progress=%s \n", id(), cur_state, primary_pending_requests.size(), pending_requests.size(), current_nb_requests_while_checkpointing, (checkpoint_in_progress?"true":"false"));
   fprintf(stderr, "The checkpoints are: ");
   for (std::map<Seqno, CheckpointSet*>::iterator it = current_checkpoints.begin(); it != current_checkpoints.end(); it++) {
      CheckpointSet* cset = it->second;
      fprintf(stderr, " [%lld, %d]", cset->get_seqno(), cset->count());
   }
   fprintf(stderr, "\n");
   pthread_mutex_unlock(&checkpointing_mutex);

   C_Panic *qp = new C_Panic(id(), 0);
   handle(qp);
}

void C_Replica::no_request_timeout() {
  ftimer->stop();

  if (cur_state == replica_state_NORMAL && robust_monitor->not_enough_requests()) {
    fprintf(stderr, "C_Replica[%d] not enough requests, send PANIC!\n", id());
    robust_monitor->print_expectations();

    C_Panic *qp = new C_Panic(id(), 0);
    handle(qp);
  }

  ftimer->start();
}
