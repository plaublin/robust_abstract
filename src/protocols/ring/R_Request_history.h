#ifndef _R_Request_history_h
#define _R_Request_history_h 1

#include "Array.h"
#include "MD5.h"
#include "Digest.h"
#include "types.h"
#include "DSum.h"

#include "R_Types.h"
class R_Replica;
class R_BaseRequest;

class R_Rh_entry
{
public:
   inline R_Rh_entry()
   {
   }

   inline R_Rh_entry(R_Message_Id r, Seqno s, Digest d)
   {
      rmid = r;
      seq = s;
      d_h = d;
   }

   inline ~R_Rh_entry()
   {
   }

   // we only keep identifiers in the history, and make sure to delete requests in <requests> map
   R_Message_Id rmid;
   Seqno seq;
   Digest d_h;

   inline Seqno seqno() const
   {
       return seq;
   }

   inline Digest& digest()
   {
       return d_h;
   }
};

class R_Req_history_log
{

public:
   // Overview : This is a request history log with request entries ordered
   // by the assigned sequence numbers.

   // How many requests will we purge
   static const int TRUNCATE_SIZE = 1024;

   R_Req_history_log(R_Replica *rp);
   // Effects: Creates an empty table.

   ~R_Req_history_log();
   // Effects: Deletes table

   bool add_request(R_BaseRequest *req, R_Message_Id rmid, Seqno seq, Digest &d);
   // Creates an entry for the request in the request history log
   // Returns true if the request has been correctly added.

   bool truncate_history(Seqno seq);

   bool should_checkpoint();
   // returns true if truncation threshold is reached, and we're not waiting for the confirmation

   bool get_next_checkpoint_data(Seqno &seqno, Digest &digest);
   // returns if there is any last data to be included in checkpoint, and sets values acordingly

   Seqno get_top_seqno() const;
   // returns the Request_id at the top

   Digest rh_digest();

   int size() const;

   Array<R_Rh_entry >& array();

private:
   Array<R_Rh_entry > rh;
   DSum rh_d; // Adhash sum of the history
   DSum* others;
   DSum* my_dsum;
   R_Replica *replica;

   // for administrative things, like keeping track of digests...
   static const unsigned int TRACK_HISTORY_SIZE = 1024; // means keeps seqnos and digests of last x checkpoints
   unsigned int track_index;
   Seqno track_seqnos[TRACK_HISTORY_SIZE];
   Digest track_digests[TRACK_HISTORY_SIZE];
   long long track_ta[TRACK_HISTORY_SIZE];

   long long totally_added;
   long long totally_checkpointed;

   Seqno last_reported;
};

#endif
