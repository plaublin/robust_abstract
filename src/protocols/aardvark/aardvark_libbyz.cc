#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "aardvark_libbyz.h"
#include "Array.h"
#include "A_Request.h"
#include "A_Reply.h"
#include "A_Client.h"
#include "A_Replica.h"
#include "A_Statistics.h"
#include "A_State_defs.h"

// FLAGS for faulty server behavior
bool _corruptClientMAC = false;

bool corruptClientMAC()
{
  return _corruptClientMAC;
}

void setCorruptClientMAC(bool val)
{
  _corruptClientMAC = val;
}

#include "A_attacks.h"

static void wait_chld(int sig)
{
  // Get rid of zombies created by sfs code.
  while (waitpid(-1, 0, WNOHANG) > 0)
    ;
}

int aardvark_init_client(char *conf, char *conf_priv, short port)
{
  // signal handler to get rid of zombies
  struct sigaction act;
  act.sa_handler = wait_chld;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;
  sigaction(SIGCHLD, &act, NULL);

  FILE *config_file = fopen(conf, "r");
  if (config_file == 0)
  {
    fprintf(stderr, "libbyz: Invalid configuration file %s \n", conf);
    return -1;
  }

  FILE *config_priv_file = fopen(conf_priv, "r");
  if (config_priv_file == 0)
  {
    fprintf(stderr, "libbyz: Invalid private configuration file %s \n", conf);
    return -1;
  }

  // Initialize random number generator
  srand48(getpid());

  A_Client* client = new A_Client(config_file, config_priv_file, port);
  A_node = client;
  return 0;
}

void aardvark_reset_client()
{
  ((A_Client*) A_node)->reset();
}

int aardvark_alloc_request(Byz_req *req)
{
  A_Request* request = new A_Request((Request_id) 0);
  if (request == 0)
    return -1;

  int len;
  // store_command uses the pointer to len in order to modify it
  req->contents = request->store_command(len);
  req->size = len;
  req->opaque = (void*) request;
  return 0;
}

// request id of the last sent request
unsigned long long last_request_id;

int aardvark_send_request(Byz_req *req, bool ro, bool faultyClient)
{
  A_Request *request = (A_Request *) req->opaque;
  request->request_id() = ((A_Client*) A_node)->get_rid();

  last_request_id = ((A_Client*) A_node)->get_rid();

#ifndef SIGN_REQUESTS
  request->authenticate(req->size, ro, faultyClient);
#else
  request->sign(req->size);
#endif

  bool retval = ((A_Client*) A_node)->send_request(request, faultyClient);
  return (retval) ? 0 : -1;
}

long long aardvark_recv_reply(Byz_rep *rep)
{
  A_Reply *reply = ((A_Client*) A_node)->recv_reply();
  if (reply == NULL) {
    rep->opaque = NULL;
    return -1;
  } else if (reply->should_switch()) {
    next_protocol_instance = reply->next_instance_id();
    rep->opaque = NULL;
    return -127;
  } else {
  rep->contents = reply->reply(rep->size);
  rep->opaque = reply;

  // return the current view number
  return reply->view();
  }
}

int aardvark_invoke(Byz_req *req, Byz_rep *rep, bool ro, bool faultyClient)
{
  if (aardvark_send_request(req, ro, faultyClient) == -1)
    return -1;
  return aardvark_recv_reply(rep);
}

/* if called, then the client will flood the replicas forever */
void aardvark_flood_replicas()
{
  ((A_Client*)A_node)->flood_replicas();
}

void aardvark_free_request(Byz_req *req)
{
  A_Request *request = (A_Request *) req->opaque;
  delete request;
}

void aardvark_free_reply(Byz_rep *rep)
{
  A_Reply *reply = (A_Reply *) rep->opaque;
  delete reply;
}

// recv reply. If there is no reply, wait for at most timeout usec.
// Used to empty the TCP buffer.
// return the number of received messages
int aardvark_recv_reply_noblock(long timeout)
{
  return ((A_Client*)A_node)->recv_reply_noblock(timeout);
}


/* OPEN LOOP */

// send the request req
int aardvark_open_loop_send_request(Byz_req *req, bool ro, bool faultyClient) {
  A_Request *request = (A_Request *) req->opaque;
  request->request_id() = ((A_Client*) A_node)->get_rid();

#ifndef SIGN_REQUESTS
  request->authenticate(req->size, ro, faultyClient);
#else
  request->sign(req->size);
#endif

  bool retval = ((A_Client*) A_node)->send_request_open_loop(request, faultyClient);
  return (retval) ? 0 : -1;
}

// Recv a reply. Return the view number. Set *rid to the id of the received reply.
int aardvark_open_loop_recv_reply(unsigned long long *rid, Byz_rep *rep) {
  A_Reply *reply = ((A_Client*) A_node)->recv_reply_open_loop();
  if (reply == NULL)
    return -1;
  rep->contents = reply->reply(rep->size);
  rep->opaque = reply;

  *rid = reply->request_id();
  return reply->view();
}

int aardvark_init_replica(char *conf, char *conf_priv, char *mem, unsigned int size,
    int(*exec)(Byz_req *, Byz_rep *, Byz_buffer *, int, bool, long int),
    void(*comp_ndet)(Seqno, Byz_buffer *), int ndet_max_len,
    long int byz_pre_prepare_delay, long int delay_every_p_pp,
    long int exec_command_delay, int sliding_window_size)
{
  // signal handler to get rid of zombies
  struct sigaction act;
  act.sa_handler = wait_chld;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;
  sigaction(SIGCHLD, &act, NULL);

  FILE *config_file = fopen(conf, "r");
  if (config_file == 0)
  {
    fprintf(stderr, "libbyz: Invalid configuration file %s \n", conf);
    return -1;
  }

  FILE *config_priv_file = fopen(conf_priv, "r");
  if (config_priv_file == 0)
  {
    fprintf(stderr, "libbyz: Invalid private configuration file %s \n",
        conf_priv);
    return -1;
  }

  // Initialize random number generator
  srand48(getpid());

  A_replica = new A_Replica(config_file, config_priv_file, mem, size,
      byz_pre_prepare_delay, delay_every_p_pp, false, exec_command_delay, sliding_window_size);
  A_node = A_replica;

  // Register service-specific functions.
  A_replica->register_exec(exec);
  A_replica->register_nondet_choices(comp_ndet, ndet_max_len);
  return A_replica->used_state_bytes();
}

void aardvark_modify(char *mem, int size)
{
  A_replica->modify(mem, size);
}

void Byz_replica_run()
{
  A_stats.zero_stats();
  A_replica->recv();
}

void _A_aardvark_modify_index(int bindex)
{
  A_replica->modify_index(bindex);
}

void aardvark_reset_stats()
{
  A_stats.zero_stats();
}

void aardvark_print_stats()
{
  A_stats.print_stats();
}

