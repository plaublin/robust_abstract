#ifndef _PBFT_R_Panic_h
#define _PBFT_R_Panic_h 1

#include "PBFT_R_Message.h"
#include "types.h"
#include "Digest.h"

class PBFT_R_Request;
class PBFT_R_Principal;

// 
// PBFT_R_Panic messages have the following format.
//
// PBFT_R_Panic_rep.extra = 4 if commit opt is on
// 

struct PBFT_R_Panic_rep : public PBFT_R_Message_rep
{
   Request_id req_id;
   int cid;
   int unused;
};

class PBFT_R_Panic : public PBFT_R_Message
{
   // 
   // PBFT_R_Panic messages
   //
public:
   PBFT_R_Panic() :
      PBFT_R_Message()
   {};

   PBFT_R_Panic(PBFT_R_Request *req);
   PBFT_R_Panic(int cid, Request_id rid);

   PBFT_R_Panic(PBFT_R_Panic_rep *contents);
   // Requires: "contents" contains a valid PBFT_R_Panic_rep.
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

   PBFT_R_Panic* copy(int id) const;
   // Effects: Creates a new object with the same state as this but
   // with replica identifier "id"

   static bool convert(PBFT_R_Message *m, PBFT_R_Panic *&r);
   // Converts the incomming message to order-request message

private:
   PBFT_R_Panic_rep &rep() const;
};

inline PBFT_R_Panic::PBFT_R_Panic(int cid, Request_id rid)
    : PBFT_R_Message(PBFT_R_Panic_tag, Max_message_size)
{
    rep().cid = cid;
    rep().req_id = rid;
    set_size(sizeof(PBFT_R_Panic_rep));
};

inline PBFT_R_Panic_rep& PBFT_R_Panic::rep() const
{
   th_assert(ALIGNED(msg), "Improperly aligned pointer");
   return *((PBFT_R_Panic_rep*)msg);
}

inline int PBFT_R_Panic::client_id() const
{
   return rep().cid;
}

inline Request_id &PBFT_R_Panic::request_id()
{
   return rep().req_id;
}

#endif // _PBFT_R_Panic_h
