#ifndef _Q_Panic_h
#define _Q_Panic_h 1

#include "O_Message.h"
#include "types.h"
#include "Digest.h"

class O_Request;
class O_Principal;

// 
// O_Panic messages have the following format.
//
// O_Panic_rep.extra = 4 if commit opt is on
// 

struct O_Panic_rep : public O_Message_rep
{
   Request_id req_id;
   int cid;
   int unused;
};

class O_Panic : public O_Message
{
   // 
   // O_Panic messages
   //
public:
   O_Panic() :
      O_Message()
   {};

   O_Panic(O_Request *req);
   O_Panic(int cid, Request_id rid);

   O_Panic(O_Panic_rep *contents);
   // Requires: "contents" contains a valid O_Panic_rep.
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

   O_Panic* copy(int id) const;
   // Effects: Creates a new object with the same state as this but
   // with replica identifier "id"

   static bool convert(O_Message *m, O_Panic *&r);
   // Converts the incomming message to order-request message

private:
   O_Panic_rep &rep() const;
};

inline O_Panic::O_Panic(int cid, Request_id rid)
    : O_Message(O_Panic_tag, O_Max_message_size)
{
    rep().cid = cid;
    rep().req_id = rid;
    set_size(sizeof(O_Panic_rep));
};

inline O_Panic_rep& O_Panic::rep() const
{
   th_assert(ALIGNED(msg), "Improperly aligned pointer");
   return *((O_Panic_rep*)msg);
}

inline int O_Panic::client_id() const
{
   return rep().cid;
}

inline Request_id &O_Panic::request_id()
{
   return rep().req_id;
}

#endif // _Q_Panic_h
