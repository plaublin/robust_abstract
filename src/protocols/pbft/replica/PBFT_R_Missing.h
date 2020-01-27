#ifndef _PBFT_R_Missing_h
#define _PBFT_R_Missing_h 1

#include "types.h"
#include "Digest.h"
#include "Request_history.h"

#include "PBFT_R_Message.h"
#include "PBFT_R_Request.h"

#include "PBFT_R_Smasher.h"

class PBFT_R_Principal;

// 
// PBFT_R_Missing messages have the following format.
//

struct PBFT_R_Missing_rep : public PBFT_R_Message_rep
{
   int replica; // replica which generated the abort
   Digest digest;
   short hist_size;
   short unused;
   int unused2;
};

// Followed by a hist_size tuples [cid, rid, digest]

class PBFT_R_Missing : public PBFT_R_Message
{
   //
   // PBFT_R_Missing messages
   //
public:

   PBFT_R_Missing(int p_replica, AbortHistory *missing);

   PBFT_R_Missing(PBFT_R_Missing_rep *contents);
   // Requires: "contents" contains a valid PBFT_R_Missing_rep.
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

   static bool convert(PBFT_R_Message *m, PBFT_R_Missing *&r);
   // Converts the incomming message to order-request message

private:
   PBFT_R_Missing_rep &rep() const;
};

inline PBFT_R_Missing_rep& PBFT_R_Missing::rep() const
{
   th_assert(ALIGNED(msg), "Improperly aligned pointer");
   return *((PBFT_R_Missing_rep*)msg);
}

inline unsigned short PBFT_R_Missing::hist_size() const
{
   return rep().hist_size;
}

inline Digest &PBFT_R_Missing::digest() const
{
   return rep().digest;
}

inline int PBFT_R_Missing::id() const
{
   return rep().replica;
}

#endif // _PBFT_R_Missing_h
