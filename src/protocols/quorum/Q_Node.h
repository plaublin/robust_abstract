#ifndef _Q_Node_h
#define _Q_Node_h 1

#include <stdio.h>
#include <pthread.h>
#include "types.h"
#include "libbyz.h"
#include "Q_Principal.h"
#include "Q_Mes_queue.h"
#include "th_assert.h"

class Q_Message;
class ITimer;
class rabin_priv;

class Q_Node {
public:
	static const int Q_All_replicas = -1;

	Q_Node(FILE *config_file, FILE *config_priv, char *host_name, short port = 0);
	// Effects: Create a new Node object using the information in
	// "config_file".  If port is 0, use the first
	// line from configuration whose host address matches the address
	// of this host to represent this principal. Otherwise, search
	// for a line with port "port".

	virtual ~Q_Node();
	// Effects: Deallocates all storage associated with node.

	int n() const;
	// Effects: Returns the number of replicas
	int f() const;
	// Effects: Returns the max number of faulty replicas
	int id() const;
	// Effects: Returns the principal identifier of the current node.

	Q_Principal *i_to_p(int id) const;
	// Effects: Returns the principal that corresponds to
	// identifier "id" or 0 if "id" is not valid.

	bool is_replica(int id) const;
	// Effects: Returns true iff id() is the identifier of a valid replica.

	enum protocols_e instance_id() const;
	// Effects: Returns the id of the instance (quorum)

  enum protocols_e next_instance_id() const;
  // Effects: Returns the id of the next instance


	//
	// Communication methods:
	//

	void send(Q_Message *m, int i);
	// Requires: "i" is either Q_All_replicas or a valid principal
	// identifier.
	// Effects: Sends an unreliable message "m" to all replicas or to
	// principal "i".

	Q_Message* recv();
	// Effects: Blocks waiting to receive a message (while calling
	// handlers on expired timers) then returns message.  The caller is
	// responsible for deallocating the message.

  Q_Message* recv(int socket);
  // Effects: Blocks waiting to receive a message (while calling
  // handlers on expired timers) on the given socket then returns message.  The caller is
  // responsible for deallocating the message.

  Q_Mes_queue incoming_queue;
  pthread_mutex_t incoming_queue_mutex;
  pthread_cond_t not_empty_incoming_queue_cond;

	bool has_messages(long to);
	// Effects: Call handles on expired timers and returns true if
	// there are messages pending. It blocks to usecs waiting for messages

	//
	// Cryptography:
	//
	unsigned sig_size(int id=-1) const;

	unsigned auth_size(int id = -1) const;
	// Effects: Returns the size in bytes of an authenticator for principal
	// "id" (or current principal if "id" is negative.)

	void gen_auth(char *src, unsigned src_len, char *dest = 0) const;
	// Requires: "src" points to a string of length at least "src_len"
	// bytes. If "dest" == 0, "src+src_len" must have size >= "sig_size()";
	// otherwise, "dest" points to a string of length at least "sig_size()".
	// Effects: Computes an authenticator of "src_len" bytes
	// starting at "src" (using out-keys for principals) and places the result in
	// "src"+"src_len" (if "dest" == 0) or "dest" (otherwise).

	bool
			verify_auth(int src_pid, char *src, unsigned src_len, char *dest =
					0) const;
	// Requires: "src_pid" is not the calling principal and same as gen_auth
	// Effects: If "src_pid" is an invalid principal identifier or is the
	// identifier of the calling principal, returns false and does
	// nothing. Otherwise, returns true iff: "src"+"src_len" or ("dest"
	// if non-zero) contains an authenticator by principal "src_pid" that is
	// valid for the calling principal

	void gen_signature(const char *src, unsigned src_len, char *sig);
	// Requires: "sig" is at least sig_size() bytes long.
	// Effects: Generates a signature "sig" (from this principal) for
	// "src_len" bytes starting at "src" and puts the result in "sig".

	unsigned decrypt(char *src, unsigned src_len, char *dst,
		unsigned dst_len);
	// Effects: decrypts the cyphertext in "src" using this
	// principal's private key and places up to "dst_len" bytes of the
	// result in "dst". Returns the number of bytes placed in "dst".

//
	// Unique identifier generation:
	//

	Request_id new_rid();
	// Effects: Computes a new request identifier. The new request
	// identifier is guaranteed to be larger than any request identifier
	// produced by the node in the past (even accross) reboots (assuming
	// clock as returned by gettimeofday retains value after a crash.)

protected:
	int node_id; // identifier of the current node.
	int max_faulty; // Maximum number of faulty replicas.
	int num_replicas; // Number of replicas in the service. It must be

	rabin_priv *priv_key; // Q_Node's private key.
	// Map from principal identifiers to Principal*. The first "num_replicas"
	// principals correspond to the replicas.
	Q_Principal **principals;
	int num_principals;

	// Special principal associated with the group of replicas.
	Q_Principal *group;

	// Communication variables.
	int sock;
	Request_id cur_rid; // state for unique identifier generator.
	void new_tstamp();
	// Effects: Computes a new timestamp for rid.
};

inline int Q_Node::n() const {
	return num_replicas;
}

inline int Q_Node::f() const {
	return max_faulty;
}

inline int Q_Node::id() const {
	return node_id;
}

inline Q_Principal* Q_Node::i_to_p(int id) const {
	if (id < 0 || id >= num_principals) {
		return 0;
	}
	return principals[id];
}

inline bool Q_Node::is_replica(int id) const {
	return id >= 0 && id < num_replicas;
}

inline unsigned Q_Node::auth_size(int id) const {
	if (id < 0) {
		id = node_id;
	}
	return ((id < num_replicas) ? num_replicas - 1 : num_replicas) * Q_UMAC_size
			+ Q_UNonce_size;
}

inline unsigned Q_Node::sig_size(int id) const
{
	if (id < 0)
		id = node_id;
	th_assert(id < num_principals, "Invalid argument");
	return principals[id]->sig_size();
}

inline enum protocols_e Q_Node::instance_id() const
{
    return quorum;
}

inline enum protocols_e Q_Node::next_instance_id() const
{
    return chain;
}

// Pointer to global node object.
extern Q_Node *Q_node;

extern Q_Principal *Q_my_principal;

#endif // _Q_Node_h
