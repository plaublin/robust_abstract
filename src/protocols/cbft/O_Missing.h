#ifndef _Q_Missing_h
#define _Q_Missing_h 1

#include "types.h"
#include "Digest.h"
#include "Request_history.h"

#include "O_Message.h"
#include "O_Request.h"

#include "O_Smasher.h"

class O_Principal;

// 
// O_Missing messages have the following format.
//

struct O_Missing_rep : public O_Message_rep
{
   int replica; // replica which generated the abort
   Digest digest;
   short hist_size;
   short unused;
   int unused2;
};

// Followed by a hist_size tuples [cid, rid, digest]

class O_Missing : public O_Message
{
   //
   // O_Missing messages
   //
public:

   O_Missing(int p_replica, AbortHistory *missing);

   O_Missing(O_Missing_rep *contents);
   // Requires: "contents" contains a valid O_Missing_rep.
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

   static bool convert(O_Message *m, O_Missing *&r);
   // Converts the incomming message to order-request message

private:
   O_Missing_rep &rep() const;
};

inline O_Missing_rep& O_Missing::rep() const
{
   th_assert(ALIGNED(msg), "Improperly aligned pointer");
   return *((O_Missing_rep*)msg);
}

inline unsigned short O_Missing::hist_size() const
{
   return rep().hist_size;
}

inline Digest &O_Missing::digest() const
{
   return rep().digest;
}

inline int O_Missing::id() const
{
   return rep().replica;
}

#endif // _Q_Missing_h
