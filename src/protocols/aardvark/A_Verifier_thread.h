#ifndef _Verifier_thread_h
#define _Verifier_thread_h 1

#include <time.h>
#include <stddef.h>
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
#include <fcntl.h> //for non_blocking socket
#include <errno.h>

#include "th_assert.h"
#include "A_Message_tags.h"
#include "A_ITimer.h"
#include "A_Request.h"
#include "A_Pre_prepare.h"
#include "A_Prepare.h"
#include "A_Commit.h"
#include "A_Checkpoint.h"
#include "A_New_key.h"
#include "A_Status.h"
#include "A_Fetch.h"
#include "A_Data.h"
#include "A_Meta_data.h"
#include "A_Meta_data_d.h"
#include "A_View_change.h"
#include "A_View_change_ack.h"
#include "A_New_view.h"
#include "A_Principal.h"
#include "A_Prepared_cert.h"
#include "A_Reply.h"
#include "K_max.h"
#include "A_Verifier_thread.h"
#include "A_Replica.h"

class A_Verifier_thread
{

public:

  A_Verifier_thread()
  {
    message = "Verifier thread running!";
  }

  void *run(void);

  int client_socket; //read client request from there

private:

  /* the socket on which the A_replica listens for connections from incoming clients */
  int bootstrap_clients_socket;
  struct sockaddr_in bootstrap_clients_sin;

  char* message;
  bool* blacklisted; //this will point to an array of num_principals bools, initialized at 0
  //client with id client_id is blacklisted iff blacklisted[client_id]==true

  struct timeval select_timeout;

  fd_set fdset;

  A_Message* m;

};

extern "C" void *Verifier_thread_startup(void *);

#endif //_Verifier_thread_h
