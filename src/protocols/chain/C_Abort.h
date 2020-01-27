#ifndef _C_Abort_h
#define _C_Abort_h 1

#include "types.h"
#include "Digest.h"
#include "Request_history.h"

#include "C_Message.h"
#include "C_Request.h"

class C_Principal;
class C_Request;

// 
// C_Abort messages have the following format.
//

struct C_Abort_rep : public C_Message_rep
{
   int replica; // replica which generated the abort
   Request_id rid; // id of the request which made replica panic
   Digest aborted_request_digest;
   Digest digest;
   short hist_size;
   short unused;
   int unused2;
};

// Followed by a hist_size tuples [cid, rid, digest]

class C_Abort : public C_Message
{
   //
   // C_Abort messages
   //
public:

   C_Abort(int p_replica, Request_id p_rid, Req_history_log<C_Request> &req_h);

   C_Abort(C_Abort_rep *contents);
   // Requires: "contents" contains a valid C_Abort_rep.
   // Effects: Creates a message from "contents". No copy is made of
   // "contents" and the storage associated with "contents" is not
   // deallocated if the message is later deleted. Useful to create
   // messages from reps contained in other messages.

   Request_id& request_id() const;
   // Effects: Fetches the request identifier from the message.

   Digest& digest() const;

   Digest& aborted_request_digest() const;

   int id() const;
   // Effects: Fetches the identifier of the replica that sent the message.

   unsigned short hist_size() const;

   void sign();

   bool verify();

   // bool verify_by_abstract();

   static bool convert(C_Message *m, C_Abort *&r);
   // Converts the incomming message to order-request message

private:
   C_Abort_rep &rep() const;
};

inline C_Abort_rep& C_Abort::rep() const
{
   th_assert(ALIGNED(msg), "Improperly aligned pointer");
   return *((C_Abort_rep*)msg);
}

inline unsigned short C_Abort::hist_size() const
{
   return rep().hist_size;
}

inline Request_id &C_Abort::request_id() const
{
   return rep().rid;
}

inline Digest &C_Abort::digest() const
{
   return rep().digest;
}

inline Digest &C_Abort::aborted_request_digest() const
{
   return rep().aborted_request_digest;
}

inline int C_Abort::id() const
{
   return rep().replica;
}

#endif // _C_Abort_h
