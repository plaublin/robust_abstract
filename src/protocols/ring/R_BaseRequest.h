#ifndef _R_BaseRequest_h
#define _R_BaseRequest_h 1

#include "R_Message.h"
#include "types.h"
#include "R_Reply.h"

class Digest;
class R_Principal;
class R_ACK;
class R_Request;

// contains only types which are going to be iterated over
// ie. only messages we batch
const int MAX_BATCH_SIZE = 10;
const int MAX_IN_A_BATCH_CHECK = 16; // since there may be duplicates, how much to take in one go.

//
// R_BaseRequest messages have the following format.
//
struct R_BaseRequest_rep: public R_Message_rep {
	// only these three will be used for calculating MAC
	uint32_t type; // can be 0 or 0xdeadbeef. 0 is when doing regular R_BaseRequest, 0xdeadbeef when working on R_ACK
	Digest od; // Digest of rid, cid, command.
	Seqno seqno; // sequence number given by the sequencer. it is non-nil if set

	// holding some data necessary for correct parsing
	int pb_offset; // holds an offset of first piggybacked req
	int pb_index; // holds an index of first piggybacked req

	int cid; // unique id of client who sends the request
	Request_id rid; // unique request identifier
	int replica_id; // id of the replica that received this request first
};

class R_BaseRequest: public R_Message {
	//
	// R_BaseRequest messages:
	//
	// Requires: Requests that may have been allocated by library users
	// through the libbyz.h interface can not be trimmed (this could free
	// memory the user expects to be able to use.)
	//
protected:
	R_BaseRequest(int t, unsigned sz);
public:
	R_BaseRequest(unsigned sz=0);
	R_BaseRequest(R_Message_rep *contents);
	~R_BaseRequest();

	void set_seqno(Seqno seq);
	// sets seqno
	Seqno& seqno() const { return rep().seqno; };
	bool is_sequenced() const { return rep().seqno != 0; };
	void set_replica(int id) { rep().replica_id = id; };

#ifdef REPLY_BY_ENTRY
	static const uint32_t MSG_IS_ACK=0xdead0000;
	static const uint32_t MSG_IS_ACK_RE=0x0000beef;

	void set_type(bool is_ack, bool is_ack_re=false) {
	    rep().type = (is_ack == true)?MSG_IS_ACK:0x0;
	    rep().type |= (is_ack_re == true)?MSG_IS_ACK_RE:0x0;
	}
#else
	static const uint32_t MSG_IS_ACK=0xdeadbeef;
	void set_type(bool is_ack) {
	    rep().type = (is_ack == true)?MSG_IS_ACK:0x0;
	}
#endif

	uint32_t get_type() const { return rep().type; }

	// set of useful methods for returning pointers
	char * const toauth_begin() const { return (char* const)&(rep().type); }
	size_t toauth_size() const { return sizeof(uint32_t)+sizeof(Seqno)+sizeof(Digest); }
	virtual char * const toauth_pos() { return contents() + sizeof(R_BaseRequest_rep); }

	virtual char * const tosign_pos() const { return contents() + sizeof(R_BaseRequest_rep); }

	int client_id() const;
	int replica_id() const;
	// Effects: Fetches the identifier of the client from the message.

	Request_id& request_id();
	// Effects: Fetches the request identifier from the message.

	char* command(int &len);
	// Effects: Returns a pointer to the command and sets len to the
	// command size.

	bool is_read_only() const;
	// Effects: Returns true iff the request message states that the
	// request is read-only.
	void set_read_only();

	void set_digest(Digest& d);
	Digest& digest() const {
		return rep().od;
	}

protected:
	R_BaseRequest_rep &rep() const;
	// Effects: Casts "msg" to a R_BaseRequest_rep&
};

inline R_BaseRequest_rep& R_BaseRequest::rep() const {
	th_assert(ALIGNED(msg), "Improperly aligned pointer");
	return *((R_BaseRequest_rep*) msg);
}

inline int R_BaseRequest::client_id() const {

	return rep().cid;
}

inline int R_BaseRequest::replica_id() const {

	return rep().replica_id;
}

inline Request_id &R_BaseRequest::request_id() {
	return rep().rid;
}

inline bool R_BaseRequest::is_read_only() const {
	return rep().extra & R_MESSAGE_READ_ONLY;
}

inline void R_BaseRequest::set_read_only() {
	rep().extra = rep().extra | R_MESSAGE_READ_ONLY;
}

inline void R_BaseRequest::set_seqno(Seqno seq) {
           rep().seqno = seq;
}

#endif // _R_BaseRequest_h
