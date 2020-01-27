#ifndef _C_Panic_h
#define _C_Panic_h 1

#include "C_Message.h"
#include "types.h"
#include "Digest.h"

class C_Request;
class C_Principal;

// 
// C_Panic messages have the following format.
//
// C_Panic_rep.extra = 4 if commit opt is on
// 

struct C_Panic_rep : public C_Message_rep
{
   Request_id req_id;
   int cid;
   int unused;
};

class C_Panic : public C_Message
{
   // 
   // C_Panic messages
   //
public:
   C_Panic() :
      C_Message()
   {};

   C_Panic(C_Request *req);
   C_Panic(int cid, Request_id rid);

   C_Panic(C_Panic_rep *contents);
   // Requires: "contents" contains a valid C_Panic_rep.
   // Effects: Creates a message from "contents". No copy is made of
   // "contents" and the storage associated with "contents" is not
   // deallocated if the message is later deleted. Useful to create
   // messages from reps contained in other messages.

   int client_id() const;
   // Effects: Fetches the identifier of the client from the message.

   Request_id& request_id();
   // Effects: Fetches the request identifier from the message.

   Digest& panic_request_digest();
   // Effects: Fetches the request identifier from the message.

   C_Panic* copy(int id) const;
   // Effects: Creates a new object with the same state as this but
   // with replica identifier "id"

   static bool convert(C_Message *m, C_Panic *&r);
   // Converts the incomming message to order-request message

private:
   C_Panic_rep &rep() const;
};

inline C_Panic::C_Panic(int cid, Request_id rid)
    : C_Message(C_Panic_tag, C_Max_message_size)
{
    rep().cid = cid;
    rep().req_id = rid;
    set_size(sizeof(C_Panic_rep));
};

inline C_Panic_rep& C_Panic::rep() const
{
   th_assert(ALIGNED(msg), "Improperly aligned pointer");
   return *((C_Panic_rep*)msg);
}

inline int C_Panic::client_id() const
{
   return rep().cid;
}

inline Request_id &C_Panic::request_id()
{
   return rep().req_id;
}

#endif // _C_Panic_h
