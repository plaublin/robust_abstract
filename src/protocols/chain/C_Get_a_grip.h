#ifndef _C_Get_a_grip_h
#define _C_Get_a_grip_h 1

#include "types.h"
#include "C_Message.h"
#include "Digest.h"
#include "C_Request.h"
#include "C_Reply.h"

class C_Principal;

//
// C_Get_a_grip messages have the following format.
//
//

struct C_Get_a_grip_rep : public C_Message_rep
{
   Request_id req_id;
   int cid;
   int replica;
   Seqno seqno;
};

class C_Get_a_grip : public C_Message
{
   //
   // C_Get_a_grip messages
   //
public:
   C_Get_a_grip() : C_Message() {}

   C_Get_a_grip(int cid, Request_id req_id, int replica, Seqno s, C_Request *r);

   C_Get_a_grip(C_Get_a_grip_rep *contents);
   // Requires: "contents" contains a valid C_Get_a_grip_rep.
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
   // Effects: Returns the reply that is contained in this C_Get_a_grip message

   C_Get_a_grip* copy(int id) const;
   // Effects: Creates a new object with the same state as this but
   // with replica identifier "id"

   static bool convert(C_Message *m, C_Get_a_grip *&r);
   // Converts the incomming message to order-request message

private:
   C_Get_a_grip_rep &rep() const;
};

inline C_Get_a_grip_rep& C_Get_a_grip::rep() const
{
   th_assert(ALIGNED(msg), "Improperly aligned pointer");
   return *((C_Get_a_grip_rep*)msg);
}

inline char* C_Get_a_grip::stored_request()
{
   char *ret = contents()+sizeof(C_Get_a_grip_rep);
   th_assert(ALIGNED(ret), "Improperly aligned pointer");
   return ret;
}

inline int C_Get_a_grip::client_id() const
{
   return rep().cid;
}

inline int C_Get_a_grip::id() const
{
   return rep().replica;
}

inline Request_id &C_Get_a_grip::request_id()
{
   return rep().req_id;
}

#endif // _C_Get_a_grip_h
