#ifndef _R_Request_h
#define _R_Request_h 1

#include "R_Message.h"
#include "R_BaseRequest.h"
#include "types.h"
#include "R_Reply.h"

#include "MD5.h"

class Digest;
class R_Principal;
class R_ACK;

//
// R_Request messages have the following format.
//
struct R_Request_rep: public R_BaseRequest_rep {
	short command_size;

	// Followed by a command which is "command_size" bytes long,
	// possible set of batched messages, and
	// authenticators.
};

class R_Request: public R_BaseRequest {
	//
	// R_Request messages:
	//
	// Requires: Requests that may have been allocated by library users
	// through the libbyz.h interface can not be trimmed (this could free
	// memory the user expects to be able to use.)
	//
public:
	R_Request(int sz=R_Max_message_size);
	R_Request(Request_id r);
	R_Request(R_Request_rep *contents);
	// Effects: Creates a new R_Request message with an empty
	// command and no authentication. The methods store_command and
	// authenticate should be used to finish message construction.
	// "rr" is the identifier of the replica from which the client
	// expects a full reply (if negative, client expects a full reply
	// from all replicas).

	~R_Request();

	static const int big_req_thresh = 0; // Maximum size of not-big requests
	char* store_command(int &max_len);
	// Effects: Returns a pointer to the location within the message
	// where the command should be stored and sets "max_len" to the number of
	// bytes available to store the reply. The caller can copy any command
	// with length less than "max_len" into the returned buffer.

	void finalize(int act_len, bool read_only);

	void authenticate(R_Rep_info *replies);
	// Effects: Terminates the construction of a request message by
	// setting the length of the command to "act_len", and appending an
	// authenticator. read-only should be true iff the request is read-only
	// (i.e., it will not change the service state).

	bool verify();

	// set of useful methods for returning pointers
	virtual char * const toauth_pos();
	virtual char * const tosign_pos() const;

	char* command(int &len);
	// Effects: Returns a pointer to the command and sets len to the
	// command size.

	char *MACs();

	static bool convert(R_Message *m1, R_Request *&m2);
	// Effects: If "m1" has the right size and tag of a "R_Request",
	// casts "m1" to a "R_Request" pointer, returns the pointer in
	// "m2" and returns true. Otherwise, it returns false.

	void comp_digest(Digest& d);
	// Effects: computes the digest of rid, cid, and the command.


private:
	R_Request_rep &rep() const;
	// Effects: Casts "msg" to a R_Request_rep&
};

inline R_Request_rep& R_Request::rep() const {
	th_assert(ALIGNED(msg), "Improperly aligned pointer");
	return *((R_Request_rep*) msg);
}

inline char *R_Request::command(int &len) {
	len = rep().command_size;
	return contents() + sizeof(R_Request_rep);
}

inline char * const R_Request::toauth_pos() {
    return contents() + sizeof(R_Request_rep) + rep().command_size ;
}

inline char *R_Request::MACs() {
	return contents() + sizeof(R_Request_rep) + rep().command_size;
}
#endif // _R_Request_h
