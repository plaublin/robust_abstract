#ifndef _A_Node_h
#define _A_Node_h 1

#include <stdio.h>
#include "types.h"
#include "A_Principal.h"
#include "A_ITimer.h"
#include "th_assert.h"

#include "A_Statistics.h"
#include "libbyz.h"
#include "A_parameters.h"

class A_Message;
class rabin_priv;
class A_New_key;
class A_ITimer;

extern void atimer_handler();

class A_Node
{
public:
  A_Node(FILE *config_file, FILE *config_priv, short port = 0);
  // Effects: Create a new A_Node object using the information in
  // "config_file" and "config_priv".  If port is 0, use the first 
  // line from configuration whose host address matches the address 
  // of this host to represent this principal. Otherwise, search 
  // for a line with port "port".

  virtual ~A_Node();
  // Effects: Deallocates all storage associated with A_node.

  View view() const;
  // Effects: Returns the last view known to this A_node.

  int n() const;
  int f() const;
  int n_f() const;
  int np() const;

  int num_clients() const;

  int id() const;
  // Effects: Returns the principal identifier of the current A_node.

  A_Principal *i_to_p(int id) const;
  // Effects: Returns the principal that corresponds to 
  // identifier "id" or 0 if "id" is not valid.

  A_Principal *principal() const;
  // Effects: Returns a pointer to the principal identifier associated
  // with the current A_node.

  bool is_replica(int id) const;
  // Effects: Returns true iff id() is the identifier of a valid A_replica.

  int primary(View vi) const;
  // Effects: Returns the identifier of the primary for view v.

  inline int primary() const;
  // Effects: Returns  the identifier of the primary for current view.

  // 
  // Communication methods:
  //
  static const int All_replicas = -1;
  void send(A_Message *m, int i);
  // Requires: "i" is either All_replicas or a valid principal
  // identifier.  
  // Effects: Sends an unreliable message "m" to all replicas or to
  // principal "i".

  void sendTCP(A_Message *m, int i, int* sockets);
  // Send the message m to i using the TCP sockets in the array sockets

  void send_on_client_port(A_Message *m, int i, int port);

  A_Message* recv();
  A_Message* recv(int s);
  // Effects: Blocks waiting to receive a message (while calling
  // handlers on expired timers) then returns message.  The caller is
  // responsible for deallocating the message.

  bool has_messages(long to);
  // Effects: Call handles on expired timers and returns true if
  // there are messages pending. It blocks to usecs waiting for messages 


  // 
  // Cryptography:
  // 

  //
  // Authenticator generation and verification:
  //
  int auth_size(int id = -1) const;
  // Effects: Returns the size in bytes of an authenticator for principal
  // "id" (or current principal if "id" is negative.)

  void gen_auth_out(char *src, unsigned src_len, char *dest = 0,
      bool faultyClient = false, bool to_print = false) const;
  // Requires: "src" points to a string of length at least "src_len"
  // bytes. If "dest" == 0, "src+src_len" must have size >= "sig_size()"; 
  // otherwise, "dest" points to a string of length at least "sig_size()".
  // Effects: Computes an authenticator of "src_len" bytes
  // starting at "src" (using out-keys for principals) and places the result in
  // "src"+"src_len" (if "dest" == 0) or "dest" (otherwise).

  void gen_auth_in(char *src, unsigned src_len, char *dest = 0,
      bool faultyClient = false, bool to_print = false) const;
  // Requires: same as gen_auth 
  // Effects: Same as gen_auth but authenticator is computed using
  // in-keys for principals

  bool
  verify_auth_in_print(int i, char *src, unsigned src_len, char *dest = 0) const;

  bool verify_auth_in(int i, char *src, unsigned src_len, char *dest = 0) const;
  // Requires: "i" is not the calling principal and same as gen_auth
  // Effects: If "i" is an invalid principal identifier or is the
  // identifier of the calling principal, returns false and does
  // nothing. Otherwise, returns true iff: "src"+"src_len" or ("dest"
  // if non-zero) contains an authenticator by principal "i" that is
  // valid for the calling principal (i.e. computed with calling
  // principal's in-key.)

  bool verify_auth_out(int i, char *src, unsigned src_len, char *dest = 0) const;
  // Requires: same as verify_auth 
  // Effects: same as verify_auth except that checks an authenticator
  // computed with calling principal's out-key.)

  //
  // Signature generation:
  //
  unsigned sig_size(int id = -1) const;
  // Requires: id < 0 | id >= num_principals
  // Effects: Returns the size in bytes of a signature for principal
  // "id" (or current principal if "id" is negative.)

  void gen_signature(const char *src, unsigned src_len, char *sig);
  // Requires: "sig" is at least sig_size() bytes long.
  // Effects: Generates a signature "sig" (from this principal) for
  // "src_len" bytes starting at "src" and puts the result in "sig".

  unsigned decrypt(char *src, unsigned src_len, char *dst, unsigned dst_len);
  // Effects: decrypts the cyphertext in "src" using this
  // principal's private key and places up to "dst_len" bytes of the 
  // result in "dst". Returns the number of bytes placed in "dst".

  //
  // Unique identifier generation:
  //  
  Request_id new_rid();
  // Effects: Computes a new request identifier. The new request
  // identifier is guaranteed to be larger than any request identifier
  // produced by the A_node in the past (even accross) reboots (assuming 
  // clock as returned by gettimeofday retains value after a crash.) 

  bool view_change_check;
  // if true, then a message is displayed in A_Node::verify_auth

  enum protocols_e instance_id() const;
  enum protocols_e next_instance_id() const;

protected:
  char service_name[256];
  int node_id; // identifier of the current A_node.
  int max_faulty; // Maximum number of faulty replicas.
  int num_replicas; // Number of replicas in the service. It must be
  // num_replicas == 3*max_faulty+1.
  int threshold; // Number of correct replicas. It must be
  // threshold == 2*max_faulty+1.

  rabin_priv *priv_key; // A_Node's private key.

  // Map from principal identifiers to A_Principal*. The first "num_replicas"
  // principals correspond to the replicas.
  A_Principal **principals;
  int num_principals;

  // Special principal associated with the group of replicas.
  A_Principal *group;

  View v; //  Last view known to this A_node.
  int cur_primary; // id of primary for the current view.

  // 
  // Handling authentication freshness
  //
  A_ITimer *atimer;
  friend void atimer_handler();

  friend class A_Verifier_thread;
  virtual void send_new_key();
  // Effects: Sends a new-key message and updates last_new_key.

  A_New_key *last_new_key; // Last new-key message we sent.


  // Communication variables.
  int sock;

  // for TCP connections:
  char **replicas_hostname;
  char **replicas_ipaddr;
  // ports on which the clients send requests to the replicas
  // i.e., replicas_ports[i] = port on which the clients send requests to A_replica i
  int* replicas_ports;

  char **clients_hostname;
  char **clients_ipaddr;
  // ports on which the clients bind
  int* clients_ports;

  Request_id cur_rid; // state for unique identifier generator.
  void new_tstamp();
  // Effects: Computes a new timestamp for rid.

  void gen_auth(char *src, unsigned src_len, bool in, char *dest,
      bool faultyClient = false, bool to_print = false) const;
  // Requires: "src" points to a string of length at least "src_len"
  // bytes. "dest" points to a string of length at least "sig_size()".
  // Effects: Computes an authenticator of "src_len" bytes starting at
  // "src" (using in-keys for principals if in is true and out-keys
  // otherwise) and places the result in "src"+"src_len" (if "dest" ==
  // 0) or "dest" (otherwise).


  bool
  verify_auth(int i, char *src, unsigned src_len, bool in, char *dest,
      bool print = false) const;
  // Requires: "i" is not the calling principal and same as gen_auth
  // Effects: If "i" is an invalid principal identifier, returns false
  // and does nothing. Otherwise, returns true iff "dest" contains an
  // authenticator by principal "i" that is valid for the calling
  // principal (i.e. computed with calling principal's in-key if in is
  // true and out-keys otherwise.)

};

inline View A_Node::view() const
{
  return v;
}

inline int A_Node::n() const
{
  return num_replicas;
}

inline int A_Node::f() const
{
  return max_faulty;
}

inline int A_Node::n_f() const
{
  return threshold;
}

inline int A_Node::np() const
{
  return num_principals;
}

inline int A_Node::num_clients() const
{
  return num_principals - num_replicas;
}

inline int A_Node::id() const
{
  return node_id;
}

inline A_Principal* A_Node::i_to_p(int id) const
{
  if (id < 0 || id >= num_principals)
    return 0;
  return principals[id];
}

inline A_Principal* A_Node::principal() const
{
  return i_to_p(id());
}

inline bool A_Node::is_replica(int id) const
{
  return id >= 0 && id < num_replicas;
}

inline int A_Node::primary(View vi) const
{
  return (vi == v) ? cur_primary : (vi % num_replicas);
}

inline int A_Node::primary() const
{
  return cur_primary;
}

#ifdef USE_SECRET_SUFFIX_MD5
inline int A_Node::auth_size(int id) const
{
  if (id < 0) id = node_id;
  return ((id < num_replicas) ? num_replicas - 1 : num_replicas) * MAC_size;
}
#else
inline int A_Node::auth_size(int id) const
{
  if (id < 0)
    id = node_id;
  return ((id < num_replicas) ? num_replicas - 1 : num_replicas) * UMAC_size
      + UNonce_size;
}
#endif

inline void A_Node::gen_auth_out(char *src, unsigned src_len, char *dest,
    bool faultyClient, bool to_print) const
{
  if (dest == 0)
    dest = src + src_len;
  gen_auth(src, src_len, false, dest, faultyClient, to_print);
}

inline void A_Node::gen_auth_in(char *src, unsigned src_len, char *dest,
    bool faultyClient, bool to_print) const
{
  if (dest == 0)
    dest = src + src_len;
  gen_auth(src, src_len, true, dest, faultyClient, to_print);
}

inline bool A_Node::verify_auth_in_print(int i, char *src, unsigned src_len,
    char *dest) const
{
  if (dest == 0)
    dest = src + src_len;
  return verify_auth(i, src, src_len, true, dest, true);
}

inline bool A_Node::verify_auth_in(int i, char *src, unsigned src_len, char *dest) const
{
  if (dest == 0)
    dest = src + src_len;
  return verify_auth(i, src, src_len, true, dest);
}

inline bool A_Node::verify_auth_out(int i, char *src, unsigned src_len,
    char *dest) const
{
  if (dest == 0)
    dest = src + src_len;
  return verify_auth(i, src, src_len, false, dest);
}

inline unsigned A_Node::sig_size(int id) const
{
  if (id < 0)
    id = node_id;
  th_assert(id < num_principals, "Invalid argument");
  return principals[id]->sig_size();
}

inline int cypher_size(char *dst, unsigned dst_len)
{
  // Effects: Returns the size of the cypher in dst or 0 if dst
  // does not contain a valid cypher.
  if (dst_len < 2 * sizeof(unsigned))
    return 0;

  unsigned csize;
  dst += sizeof(unsigned);
  memcpy((char*) &csize, dst, sizeof(unsigned));

  if (csize <= dst_len)
    return csize + 2 * sizeof(unsigned);
  else
    return 0;
}

inline enum protocols_e A_Node::instance_id() const
{
    return aardvark;
}

inline enum protocols_e A_Node::next_instance_id() const
{
    //return quorum;
    return chain;
}

// Pointer to global A_node object.
extern A_Node *A_node;

#endif // _Node_h
