#ifndef _Q_Abort_h
#define _Q_Abort_h 1

#include "types.h"
#include "Digest.h"
#include "Request_history.h"

#include "O_Message.h"
#include "O_Request.h"

class O_Principal;

// 
// O_Abort messages have the following format.
//

struct O_Abort_rep : public O_Message_rep
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

class O_Abort : public O_Message
{
   //
   // O_Abort messages
   //
public:

   O_Abort(int p_replica, Request_id p_rid, Req_history_log<O_Request> &req_h);

   O_Abort(O_Abort_rep *contents);
   // Requires: "contents" contains a valid O_Abort_rep.
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

   static bool convert(O_Message *m, O_Abort *&r);
   // Converts the incomming message to order-request message

private:
   O_Abort_rep &rep() const;
};

inline O_Abort_rep& O_Abort::rep() const
{
   th_assert(ALIGNED(msg), "Improperly aligned pointer");
   return *((O_Abort_rep*)msg);
}

inline unsigned short O_Abort::hist_size() const
{
   return rep().hist_size;
}

inline Request_id &O_Abort::request_id() const
{
   return rep().rid;
}

inline Digest &O_Abort::digest() const
{
   return rep().digest;
}

inline Digest &O_Abort::aborted_request_digest() const
{
   return rep().aborted_request_digest;
}

inline int O_Abort::id() const
{
   return rep().replica;
}

#endif // _Q_Abort_h
