#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h> 
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <netinet/tcp.h> // for TCP_NODELAY
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "th_assert.h"
#include "A_Client.h"
#include "A_ITimer.h"
#include "A_Message.h"
#include "A_Reply.h"
#include "A_Request.h"
#include "A_Wrapped_request.h"
#include "A_Req_queue.h"
#include "Array.h"
#include "tcp_net.h"

//#define ADJUST_RTIMEOUT 1

A_Client::A_Client(FILE *config_file, FILE *config_priv, short port) :
  A_Node(config_file, config_priv, port), t_reps(2 * f() + 1), c_reps(f() + 1)
{
  // Fail if A_node is is a A_replica.
  if (is_replica(id()))
    th_fail("Node is a replica");

  rtimeout = client_retrans_timeout; // Initial timeout value
  rtimer = new A_ITimer(rtimeout, A_rtimer_handler);

  out_rid = new_rid();
  out_req = 0;

  // Open loop, certificate for replies
  for (int i=0; i<MAX_NB_REPLY_CERTS; i++) {
    t_reps_rid_ol[i] = 0;
    c_reps_rid_ol[i] = 0;
  }

  // Multicast new key to all replicas.
  send_new_key();
  atimer->start();

  replicas_sockets = (int*) malloc(sizeof(int) * num_replicas);

  for (int i = 0; i < num_replicas; i++)
  {
    // create the socket
    replicas_sockets[i] = socket(AF_INET, SOCK_STREAM, 0);
    if (replicas_sockets[i] == -1)
    {
      perror("Error while creating the socket! ");
      exit(errno);
    }

    // TCP NO DELAY
    int flag = 1;
    int result = setsockopt(replicas_sockets[i], IPPROTO_TCP, TCP_NODELAY,
        (char*) &flag, sizeof(int));
    if (result == -1)
    {
      perror("Error while setting TCP NO DELAY! ");
    }

    // re-use addr
    flag = 1;
    setsockopt(replicas_sockets[i], SOL_SOCKET, SO_REUSEADDR, &flag,
        sizeof(flag));

    // bind to the port defined in the config file
    struct sockaddr_in configured_sockaddr;
    configured_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    configured_sockaddr.sin_family = AF_INET;
    configured_sockaddr.sin_port = htons(clients_ports[id() - num_replicas]);

    if (bind(replicas_sockets[i], (struct sockaddr*) &configured_sockaddr,
        sizeof(configured_sockaddr)) == -1)
    {
      perror("Error while binding to the socket! ");
      exit(errno);
    }

    // connect to the server

    // since we have multiple NICs, use replicas_ipaddr[], and not replicas_hostname[]
    struct sockaddr_in addr;
    addr.sin_addr.s_addr = inet_addr(replicas_ipaddr[i]);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(replicas_ports[i]);

    while (true)
    {
      if (connect(replicas_sockets[i], (struct sockaddr*) &addr, sizeof(addr))
          < 0)
      {
        fprintf(stderr, "Client %i to replica %i: ", id(), i);
        perror("Cannot connect");
        sleep(1);
      }
      else
      {
        fprintf(stderr, "Client %i to replica %i: connection successful!\n",
            id(), i);
        break;
      }
    }
  }
}

A_Client::~A_Client()
{
  delete rtimer;
}

void A_Client::reset()
{
  rtimeout = client_retrans_timeout;
}

void A_Client::flood_replicas()
{
  // flood with max-sized messages
  A_Message m(A_Wrapped_request_tag, A_Max_message_size);
  A_Message::set_mac_unvalid((A_Message_rep*)(m.contents()));

  while (true)
  {
    sendTCP(&m, All_replicas, replicas_sockets);
  }
}

bool A_Client::send_request(A_Request *req, bool faultyClient)
{
  bool ro = req->is_read_only();

  //create the wrapped request

  A_Req_queue tmp; //req_queue used just to store the request, because wrapped request constructor takes a request queue as a parameter
  //It can be easily modified to accept a request* instead of a request queue, but I prefer to do exactly the same things
  //that are done for the generation of a A_Pre_prepare message.
  A_Request *cloned = req->clone();
  //cloned->re_authenticate(false);
  tmp.append(cloned);
  A_Wrapped_request* wrapped = new A_Wrapped_request((Seqno) id(), (View) 1, tmp,
      faultyClient);
  //request wrapped!!!

  if (out_req == 0)
  {
    //Always send the request to all replicas
    sendTCP(wrapped, All_replicas, replicas_sockets);

    delete wrapped;

    if (!faultyClient)
    {
      out_req = req;
      need_auth = false;
      n_retrans = 0;

#ifdef ADJUST_RTIMEOUT
      // Adjust timeout to reflect average latency
      rtimer->adjust(rtimeout);

      // Start timer to measure request latency
      latency.reset();
      latency.start();
#endif

      rtimer->start();
      return true;
    }
    else
    {
      return false;
    }
  }
  else
  {
    // Another request is being processed.
    return false;
  }
}

// open loop send request operation
bool A_Client::send_request_open_loop(A_Request *req, bool faultyClient)
{
  bool ro = req->is_read_only();

  //create the wrapped request

  A_Req_queue tmp; //req_queue used just to store the request, because wrapped request constructor takes a request queue as a parameter
  //It can be easily modified to accept a request* instead of a request queue, but I prefer to do exactly the same things
  //that are done for the generation of a A_Pre_prepare message.
  A_Request *cloned = req->clone();
  //cloned->re_authenticate(false);
  tmp.append(cloned);
  A_Wrapped_request* wrapped = new A_Wrapped_request((Seqno) id(), (View) 1, tmp,
      faultyClient);
  //request wrapped!!!

  // Send request to service
  if (faultyClient || ro || req->size() > A_Request::big_req_thresh)
  {
    // read-only requests and big requests are multicast to all replicas.
    sendTCP(wrapped, All_replicas, replicas_sockets);
  }
  else
  {
    // when a view change occurs, the client does not know immediately who is the primary.
    // Thus, the client sends its requests to all the replicas, avoiding a possible timeout.
    //sendTCP(wrapped, All_replicas, replicas_sockets);

    // read-write requests are sent to the primary only.
    sendTCP(wrapped, primary(), replicas_sockets);
  }

  delete wrapped;

  out_rid = new_rid();

  if (!faultyClient)
  {
    need_auth = false;

#ifdef ADJUST_RTIMEOUT
    // Adjust timeout to reflect average latency
    rtimer->adjust(rtimeout);

    // Start timer to measure request latency
    latency.reset();
    latency.start();
#endif

    return true;
  }
  else
  {
    return false;
  }
}


A_Message* A_Client::recvTCP()
{
  A_Message *m = 0;
  struct timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = 20000; // 20000 is the argument of A_Node::hasMessages(to)
  fd_set fdset;

  while (true)
  {
    // select
    FD_ZERO(&fdset);
    int maxsock = 0;

    for (int i = 0; i < num_replicas; i++)
    {
      FD_SET(replicas_sockets[i], &fdset);
      maxsock = MAX(maxsock, replicas_sockets[i]);
    }

    int ret = select(maxsock + 1, &fdset, 0, 0, &timeout);
    if (ret > 0)
    {
      for (int i = 0; i < num_replicas; i++)
      {
        if (FD_ISSET(replicas_sockets[i], &fdset))
        {
          m = new A_Message(A_Max_message_size);

          // 1) first of all, we need to receive the A_Message_rep (in order to get the message size)
          int msg_rep_size = recvMsg(replicas_sockets[i],
              (void*) m->contents(), sizeof(A_Message_rep));

          // 2) now that we have the size of the message, receive the content
          /*int msg_size =*/ recvMsg(replicas_sockets[i],
              (void*) ((char*) m->contents() + msg_rep_size), m->size()
                  - msg_rep_size);

          //int ret = msg_rep_size + msg_size;
          A_ITimer::handle_timeouts();

          return m;
        }
      }
    }

    A_ITimer::handle_timeouts();
  }

  return m;
}

A_Reply *A_Client::recv_reply()
{
  if (out_req == 0)
    // Nothing to wait for.
    return 0;

  //
  // Wait for reply
  //
  while (1)
  {
    A_Message* m = 0;
    m = recvTCP();

    A_Reply* rep;
    if (!A_Reply::convert(m, rep) || rep->request_id() != out_rid)
    {
      delete m;
      continue;
    }

    A_Certificate<A_Reply> &reps = (rep->is_tentative()) ? t_reps : c_reps;
    if (reps.is_complete())
    {
      // We have a complete certificate without a full reply.
      if (!rep->full() || !rep->verify() || !rep->match(reps.cvalue()))
      {
        delete rep;
        continue;
      }
    }
    else
    {
      reps.add(rep);
      rep = (reps.is_complete() && reps.cvalue()->full()) ? reps.cvalue_clear()
          : 0;
    }

    if (rep)
    {
      out_rid = new_rid();
      rtimer->stop();
      out_req = 0;
      t_reps.clear();
      c_reps.clear();

      // Choose view in returned rep. TODO: could make performance
      // more robust to attacks by picking the median view in the
      // certificate.
      v = rep->view();
      cur_primary = v % num_replicas;

#ifdef ADJUST_RTIMEOUT
      latency.stop();
      rtimeout = (3*rtimeout+
          latency.elapsed()*Rtimeout_mult/(clock_mhz*1000))/4+1;
#endif

      return rep;
    }
  }
}


// recv reply. If there is no reply, wait for at most to usec.
// Used to empty the TCP buffer.
// return the number of received messages
int A_Client::recv_reply_noblock(long to) {
  int n = 0;
  struct timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = to;
  fd_set fdset;

  // select
  FD_ZERO(&fdset);
  int maxsock = 0;

  for (int i = 0; i < num_replicas; i++)
  {
    FD_SET(replicas_sockets[i], &fdset);
    maxsock = MAX(maxsock, replicas_sockets[i]);
  }

  int ret = select(maxsock + 1, &fdset, 0, 0, &timeout);
  if (ret > 0)
  {
    for (int i = 0; i < num_replicas; i++)
    {
      if (FD_ISSET(replicas_sockets[i], &fdset))
      {
        A_Message *m = new A_Message(A_Max_message_size);

        // 1) first of all, we need to receive the A_Message_rep (in order to get the message size)
        int msg_rep_size = recvMsg(replicas_sockets[i],
            (void*) m->contents(), sizeof(A_Message_rep));

        // 2) now that we have the size of the message, receive the content
        recvMsg(replicas_sockets[i],
            (void*) ((char*) m->contents() + msg_rep_size), m->size()
                - msg_rep_size);

        n++;
        delete m;
      }
    }
  }

  return n;
}



// open loop recv reply operation
A_Reply* A_Client::recv_reply_open_loop(void) {
  while (1)
  {
    A_Message* m = 0;
    m = recvTCP();

#ifdef MSG_DEBUG
    fprintf(stderr, "Client %i has received a message: tag=%i\n", id(), m->tag());
#endif

    A_Reply* rep;
    if (!A_Reply::convert(m, rep))
    {
      delete m;
      continue;
    }

    A_Certificate<A_Reply> *reps;
    Request_id *rid;

    if (rep->is_tentative()) {
      reps = &t_reps_ol[rep->request_id() % MAX_NB_REPLY_CERTS];
      rid = &t_reps_rid_ol[rep->request_id() % MAX_NB_REPLY_CERTS];
    } else {
      reps = &c_reps_ol[rep->request_id() % MAX_NB_REPLY_CERTS];
      rid = &c_reps_rid_ol[rep->request_id() % MAX_NB_REPLY_CERTS];
    }

    if (*rid != rep->request_id()) {
      reps->clear();
      *rid = rep->request_id();
    }

    if (reps->is_complete())
    {
      // We have a complete certificate without a full reply.
      if (!rep->full() || !rep->verify() || !rep->match(reps->cvalue()))
      {
        delete rep;
        continue;
      }
    }
    else
    {
      reps->add(rep);
      rep = (reps->is_complete() && reps->cvalue()->full()) ? reps->cvalue_clear()
          : 0;
    }

    if (rep)
    {
      t_reps_ol[rep->request_id() % MAX_NB_REPLY_CERTS].clear();
      c_reps_ol[rep->request_id() % MAX_NB_REPLY_CERTS].clear();

      // Choose view in returned rep. TODO: could make performance
      // more robust to attacks by picking the median view in the
      // certificate.
      v = rep->view();
      cur_primary = v % num_replicas;

#ifdef ADJUST_RTIMEOUT
      latency.stop();
      rtimeout = (3*rtimeout+
          latency.elapsed()*Rtimeout_mult/(clock_mhz*1000))/4+1;
#endif

      return rep;
    }
  }
}

void A_rtimer_handler()
{
  th_assert(A_node, "Client is not initialized");
  ((A_Client*) A_node)->retransmit();
}

void A_Client::retransmit()
{
  // Retransmit any outstanding request.
  static const int thresh = 1;
  static const int nk_thresh = 4;
  static const int nk_thresh_1 = 100;

  if (out_req != 0)
  {
    INCR_OP(req_retrans);

    //    fprintf(stderr, ".");
    n_retrans++;

    if (n_retrans == nk_thresh || n_retrans % nk_thresh_1 == 0)
    {
      send_new_key();
    }

    bool ro = out_req->is_read_only();
    bool change = (ro || out_req->replier() >= 0) && n_retrans > thresh;
    //    fprintf(stderr, "%d %d %d %d\n", id(), n_retrans, ro, out_req->replier());

    if (need_auth || change)
    {
      // Compute new authenticator for request
      out_req->re_authenticate(change);
      need_auth = false;
      if (ro && change)
        t_reps.clear();
    }

    A_Req_queue tmp;
    A_Request* cloned = out_req->clone();
    tmp.append(cloned);
    A_Wrapped_request* wrapped = new A_Wrapped_request((Seqno) id(), (View) 1, tmp);

    if (out_req->is_read_only() || n_retrans > thresh || out_req->size()
        > A_Request::big_req_thresh)
    {
      // read-only requests, requests retransmitted more than
      // mcast_threshold times, and big requests are multicast to all
      // replicas.
      //      fprintf(stderr, "retransmitting\n");
      sendTCP(wrapped, All_replicas, replicas_sockets);
      //send(out_req, primary());
    }
    else
    {
      // read-write requests are sent to the primary only.
      //      fprintf(stderr, "retransmitting\n");
      //send(out_req, primary());
      sendTCP(wrapped, All_replicas, replicas_sockets);
    }

    delete wrapped;
  }
  else
  {
    fprintf(stderr, "Timeout has expired but there is no pending request\n");
  }

#ifdef ADJUST_RTIMEOUT
  // exponential back off
  if (rtimeout < Min_rtimeout) rtimeout = 100;
  rtimeout = rtimeout+lrand48()%rtimeout;
  if (rtimeout > Max_rtimeout) rtimeout = Max_rtimeout;
  rtimer->adjust(rtimeout);
#endif

  rtimer->restart();
}

void A_Client::send_new_key()
{
  A_Node::send_new_key();
  need_auth = true;

  // Cleanup reply messages authenticated with old keys.
  t_reps.clear();
  c_reps.clear();
}
