#ifndef _R_ACK_h
#define _R_ACK_h 1

#include "R_Message.h"
#include "R_BaseRequest.h"
#include "types.h"
#include "R_Reply.h"

class Digest;
class R_Principal;

//
// R_ACK messages have the following format.
//
struct R_ACK_rep: public R_BaseRequest_rep {
	// Followed by authenticator for this message
};

class R_ACK: public R_BaseRequest {
	//
	// R_ACK messages:
	//
	// Requires: Requests that may have been allocated by library users
	// through the libbyz.h interface can not be trimmed (this could free
	// memory the user expects to be able to use.)
	//
public:
	R_ACK();
	R_ACK(R_ACK_rep *rep);
	R_ACK(int cid, int replica_id, Seqno& seqno, Request_id& r, Digest& d);
	// Effects: Creates a new R_ACK message

	~R_ACK();

	static const int big_req_thresh = 0; // Maximum size of not-big requests

	static bool convert(R_Message *m1, R_ACK *&m2);
	// Effects: If "m1" has the right size and tag of a "R_ACK",
	// casts "m1" to a "R_ACK" pointer, returns the pointer in
	// "m2" and returns true. Otherwise, it returns false.

	// set of useful methods for returning pointers
	char * const toauth_pos() { return contents() + sizeof(R_ACK_rep); }

	void take_authenticators(R_Request *req);
	void authenticate(R_Rep_info *replies);
	bool verify(R_Request *req);

	// replica sets this after creating the ACK. it is a signal for the sending replica to know how to authenticate the message
	bool just_created;

private:
	R_ACK_rep &rep() const;
	// Effects: Casts "msg" to a R_ACK_rep&
};

inline R_ACK_rep& R_ACK::rep() const {
	th_assert(ALIGNED(msg), "Improperly aligned pointer");
	return *((R_ACK_rep*) msg);
}

#endif // _R_ACK_h
