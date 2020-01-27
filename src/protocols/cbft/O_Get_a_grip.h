#ifndef _Q_Get_a_grip_h
#define _Q_Get_a_grip_h 1

#include "types.h"
#include "O_Message.h"
#include "Digest.h"
#include "O_Request.h"
#include "O_Reply.h"

class O_Principal;

//
// O_Get_a_grip messages have the following format.
//
//

struct O_Get_a_grip_rep : public O_Message_rep
{
   Request_id req_id;
   int cid;
   int replica;
   Seqno seqno;
};

class O_Get_a_grip : public O_Message
{
   //
   // O_Get_a_grip messages
   //
public:
   O_Get_a_grip() : O_Message() {}

   O_Get_a_grip(int cid, Request_id req_id, int replica, Seqno s, O_Request *r);

   O_Get_a_grip(O_Get_a_grip_rep *contents);
   // Requires: "contents" contains a valid O_Get_a_grip_rep.
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
   // Effects: Returns the reply that is contained in this O_Get_a_grip message

   O_Get_a_grip* copy(int id) const;
   // Effects: Creates a new object with the same state as this but
   // with replica identifier "id"

   static bool convert(O_Message *m, O_Get_a_grip *&r);
   // Converts the incomming message to order-request message

private:
   O_Get_a_grip_rep &rep() const;
};

inline O_Get_a_grip_rep& O_Get_a_grip::rep() const
{
   th_assert(ALIGNED(msg), "Improperly aligned pointer");
   return *((O_Get_a_grip_rep*)msg);
}

inline char* O_Get_a_grip::stored_request()
{
   char *ret = contents()+sizeof(O_Get_a_grip_rep);
   th_assert(ALIGNED(ret), "Improperly aligned pointer");
   return ret;
}

inline int O_Get_a_grip::client_id() const
{
   return rep().cid;
}

inline int O_Get_a_grip::id() const
{
   return rep().replica;
}

inline Request_id &O_Get_a_grip::request_id()
{
   return rep().req_id;
}

#endif // _Q_Get_a_grip_h
