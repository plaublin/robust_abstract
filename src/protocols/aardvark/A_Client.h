#ifndef _A_Client_h
#define _A_Client_h 1

#include <stdio.h>
#include "types.h"
#include "A_Node.h"
#include "A_Certificate.h"
#include "Array.h"
#include <time.h>
class A_Reply;
class A_Request;
class A_ITimer;
extern void A_rtimer_handler();

class A_Client: public A_Node
{
public:
  A_Client(FILE *config_file, FILE *config_priv, short port = 0);
  // Effects: Creates a new A_Client object using the information in
  // "config_file" and "config_priv". The line of config assigned to
  // this client is the first one with the right host address (if
  // port==0) or the first with the right host address and port equal
  // to "port".

  virtual ~A_Client();
  // Effects: Deallocates all storage associated with this.

  bool send_request(A_Request *req, bool faultyClient = false);
  // Effects: Sends request m to the service. Returns FALSE iff two
  // consecutive request were made without waiting for a reply between
  // them.
  // If faultyClient == true, the MAC of the wrapped request is corrupted

  // open loop send request operation
  bool send_request_open_loop(A_Request *req, bool faultyClient = false);

  A_Reply *recv_reply();
  // Effects: Blocks until it receives enough reply messages for
  // the previous request. returns a pointer to the reply. The caller is
  // responsible for deallocating the request and reply messages.

  // recv reply. If there is no reply, wait for at most to usec.
  // Used to empty the TCP buffer.
  // return the number of received messages
  int recv_reply_noblock(long to);

  // open loop recv reply operation
  A_Reply* recv_reply_open_loop(void);

  Request_id get_rid() const;
  // Effects: Returns the current outstanding request identifier. The request
  // identifier is updated to a new value when the previous message is
  // delivered to the user.

  void reset();
  // Effects: Resets client state to ensure independence of experimental
  // points.
  // More precisely, resets the timeout rtimeout to its initial value.

  void flood_replicas();
  // Effects: this client will send messages of size A_Max_message_size to all the replicas,
  // forever.

private:
  A_Request *out_req; // Outstanding request
  bool need_auth; // Whether to compute new authenticator for out_req
  Request_id out_rid; // Identifier of the outstanding request
  int n_retrans; // Number of retransmissions of out_req
  int rtimeout; // Timeout period in msecs

  // Maximum retransmission timeout in msecs
  static const int Max_rtimeout = 10000;

  // Minimum retransmission timeout after retransmission 
  // in msecs
  static const int Min_rtimeout = 10;

  A_Cycle_counter latency; // Used to measure latency.

  // Multiplier used to obtain retransmission timeout from avg_latency
  static const int Rtimeout_mult = 4;

  A_Certificate<A_Reply> t_reps; // A_Certificate with tentative replies (size 2f+1)
  A_Certificate<A_Reply> c_reps; // A_Certificate with committed replies (size f+1)

  // Open loop
  // max number of A_Certificate<A_Reply> at the client
  #define MAX_NB_REPLY_CERTS (max_out)

  A_Certificate<A_Reply> t_reps_ol[MAX_NB_REPLY_CERTS]; // A_Certificate with tentative replies (size 2f+1)
  A_Certificate<A_Reply> c_reps_ol[MAX_NB_REPLY_CERTS]; // A_Certificate with committed replies (size f+1)
  Request_id t_reps_rid_ol[MAX_NB_REPLY_CERTS];
  Request_id c_reps_rid_ol[MAX_NB_REPLY_CERTS];

  friend void A_rtimer_handler();
  A_ITimer *rtimer; // Retransmission timer

  void retransmit();
  // Effects: Retransmits any outstanding request and last new-key message.

  void send_new_key();
  // Effects: Calls A_Node's send_new_key, and cleans up stale replies in
  // certificates.

  A_Message* recvTCP();
  // Receive a message using the TCP sockets

  // sockets for communications with replicas
  int *replicas_sockets;
};

inline Request_id A_Client::get_rid() const
{
  return out_rid;
}

#endif // _A_Client_h
