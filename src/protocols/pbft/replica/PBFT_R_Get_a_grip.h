#ifndef _PBFT_R_Get_a_grip_h
#define _PBFT_R_Get_a_grip_h 1

#include "types.h"
#include "PBFT_R_Message.h"
#include "Digest.h"
#include "PBFT_R_Request.h"
#include "PBFT_R_Reply.h"

class PBFT_R_Principal;

//
// PBFT_R_Get_a_grip messages have the following format.
//
//

struct PBFT_R_Get_a_grip_rep : public PBFT_R_Message_rep
{
   Request_id req_id;
   int cid;
   int replica;
   Seqno seqno;
};

class PBFT_R_Get_a_grip : public PBFT_R_Message
{
   //
   // PBFT_R_Get_a_grip messages
   //
public:
   PBFT_R_Get_a_grip() : PBFT_R_Message() {}

   PBFT_R_Get_a_grip(int cid, Request_id req_id, int replica, Seqno s, PBFT_R_Request *r);

   PBFT_R_Get_a_grip(PBFT_R_Get_a_grip_rep *contents);
   // Requires: "contents" contains a valid PBFT_R_Get_a_grip_rep.
   // Effects: Creates a message from "contents". No copy is made of
   // "contents" and the storage associated with "contents" is not
   // deallocated if the message is later deleted. Useful to create
   // messages from reps contained in other messages.

   int client_id() const;
   // Effects: Fetches the identifier of the client from the message.

   int id() const;
   // Effects: Fetches the identifier of the replica that sent the message.

   Request_id& request_id();
   // Effects: Fetches the request identifier from the message.

   Seqno seqno() { return rep().seqno; }

   char* stored_request();
   // Effects: Returns the reply that is contained in this PBFT_R_Get_a_grip message

   PBFT_R_Get_a_grip* copy(int id) const;
   // Effects: Creates a new object with the same state as this but
   // with replica identifier "id"

   static bool convert(PBFT_R_Message *m, PBFT_R_Get_a_grip *&r);
   // Converts the incomming message to order-request message

private:
   PBFT_R_Get_a_grip_rep &rep() const;
};

inline PBFT_R_Get_a_grip_rep& PBFT_R_Get_a_grip::rep() const
{
   th_assert(ALIGNED(msg), "Improperly aligned pointer");
   return *((PBFT_R_Get_a_grip_rep*)msg);
}

inline char* PBFT_R_Get_a_grip::stored_request()
{
   char *ret = contents()+sizeof(PBFT_R_Get_a_grip_rep);
   th_assert(ALIGNED(ret), "Improperly aligned pointer");
   return ret;
}

inline int PBFT_R_Get_a_grip::client_id() const
{
   return rep().cid;
}

inline int PBFT_R_Get_a_grip::id() const
{
   return rep().replica;
}

inline Request_id &PBFT_R_Get_a_grip::request_id()
{
   return rep().req_id;
}

#endif // _PBFT_R_Get_a_grip_h
