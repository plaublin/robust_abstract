#ifndef _C_Missing_h
#define _C_Missing_h 1

#include "types.h"
#include "Digest.h"
#include "Request_history.h"

#include "C_Message.h"
#include "C_Request.h"

#include "C_Smasher.h"

class C_Principal;

// 
// C_Missing messages have the following format.
//

struct C_Missing_rep : public C_Message_rep
{
   int replica; // replica which generated the abort
   Digest digest;
   short hist_size;
   short unused;
   int unused2;
};

// Followed by a hist_size tuples [cid, rid, digest]

class C_Missing : public C_Message
{
   //
   // C_Missing messages
   //
public:

   C_Missing(int p_replica, AbortHistory *missing);

   C_Missing(C_Missing_rep *contents);
   // Requires: "contents" contains a valid C_Missing_rep.
   // Effects: Creates a message from "contents". No copy is made of
   // "contents" and the storage associated with "contents" is not
   // deallocated if the message is later deleted. Useful to create
   // messages from reps contained in other messages.

   Digest& digest() const;

   int id() const;
   // Effects: Fetches the identifier of the replica that sent the message.

   unsigned short hist_size() const;

   void sign();

   bool verify();

   // bool verify_by_abstract();

   static bool convert(C_Message *m, C_Missing *&r);
   // Converts the incomming message to order-request message

private:
   C_Missing_rep &rep() const;
};

inline C_Missing_rep& C_Missing::rep() const
{
   th_assert(ALIGNED(msg), "Improperly aligned pointer");
   return *((C_Missing_rep*)msg);
}

inline unsigned short C_Missing::hist_size() const
{
   return rep().hist_size;
}

inline Digest &C_Missing::digest() const
{
   return rep().digest;
}

inline int C_Missing::id() const
{
   return rep().replica;
}

#endif // _C_Missing_h
