#ifndef _PBFT_R_Abort_h
#define _PBFT_R_Abort_h 1

#include "types.h"
#include "Digest.h"
#include "Request_history.h"

#include "PBFT_R_Message.h"
#include "PBFT_R_Request.h"

class PBFT_R_Principal;
class PBFT_R_Request;

// 
// PBFT_R_Abort messages have the following format.
//

struct PBFT_R_Abort_rep : public PBFT_R_Message_rep
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

class PBFT_R_Abort : public PBFT_R_Message
{
   //
   // PBFT_R_Abort messages
   //
public:

   PBFT_R_Abort(int p_replica, Request_id p_rid, Req_history_log<PBFT_R_Request> &req_h);

   PBFT_R_Abort(PBFT_R_Abort_rep *contents);
   // Requires: "contents" contains a valid PBFT_R_Abort_rep.
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

   static bool convert(PBFT_R_Message *m, PBFT_R_Abort *&r);
   // Converts the incomming message to order-request message

private:
   PBFT_R_Abort_rep &rep() const;
};

inline PBFT_R_Abort_rep& PBFT_R_Abort::rep() const
{
   th_assert(ALIGNED(msg), "Improperly aligned pointer");
   return *((PBFT_R_Abort_rep*)msg);
}

inline unsigned short PBFT_R_Abort::hist_size() const
{
   return rep().hist_size;
}

inline Request_id &PBFT_R_Abort::request_id() const
{
   return rep().rid;
}

inline Digest &PBFT_R_Abort::digest() const
{
   return rep().digest;
}

inline Digest &PBFT_R_Abort::aborted_request_digest() const
{
   return rep().aborted_request_digest;
}

inline int PBFT_R_Abort::id() const
{
   return rep().replica;
}

#endif // _PBFT_R_Abort_h
