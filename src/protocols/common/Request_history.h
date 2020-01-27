#ifndef _Request_history_h
#define _Request_history_h 1

#include <pthread.h>

#include "Array.h"
#include "MD5.h"
#include "Digest.h"
#include "types.h"
#include "DSum.h"

#define CHECKPOINTING_TIMEOUT 10000
//panic if has not received a checkpoint during the last <this value> ms

template <class AReq>
class Rh_entry
{
public:
   inline Rh_entry()
   {
   }

   inline Rh_entry(AReq *r, Seqno s, Digest d)
   {
      req = r;
      seq = s;
      d_h = d;
   }

   inline ~Rh_entry()
   {
   }
   AReq *req;
   Seqno seq;
   Digest d_h;

   inline Request_id& request_id() const
   {
       return req->request_id();
   }

   inline Seqno seqno() const
   {
       return seq;
   }

   inline Digest& digest()
   {
       return d_h;
   }
};

template <class AReq>
class Req_history_log
{

public:
   // Overview : This is a request history log with request entries ordered
   // by the assigned sequence numbers.

   // How many requests will we purge
   static const int TRUNCATE_SIZE = 1024;
   static const int MAX_NB_REQS_CHKPT = 256;

   Req_history_log();
   // Effects: Creates an empty table.

   ~Req_history_log();
   // Effects: Deletes table

   bool add_request(AReq *req, Seqno seq, Digest &d);
   // Creates an entry for the request in the request history log
   // Returns true if the request has been correctly added.

   bool truncate_history(Seqno seq);
   // purges the history for amount entries up to rid
   bool truncate_history(Seqno seq, void (*request_callback)(AReq*));

   bool should_checkpoint();
   // returns true if truncation threshold is reached, and we're not waiting for the confirmation

   static int max_nb_requests_while_checkpointing();

   Seqno get_top_seqno() const;
   // returns the Request_id at the top

   Digest rh_digest();

   int size() const;

   Array<Rh_entry<AReq> >& array();

   Rh_entry<AReq>* find(int cid, Request_id rid);

private:
   Array<Rh_entry<AReq> > rh;
   DSum rh_d; // Adhash sum of the history
   DSum* others;
   DSum* my_dsum;
   pthread_mutex_t rh_mutex;
};

template <class AReq>
Req_history_log<AReq>::Req_history_log() :
   rh(512)
{
   // The random modulus for computing sums in AdHASH.
   others = DSum::M;
   //fprintf(stderr, "Req_History_log(): others = %p, DSum::M = %p\n", others, DSum::M);
   DSum::M = my_dsum = new DSum;
   //fprintf(stderr, "Req_History_log()[1]: DSum::M = %p\n", DSum::M);
   mpn_set_str(
         DSum::M->sum,
         (unsigned char*)"d2a10a09a80bc599b4d60bbec06c05d5e9f9c369954940145b63a1e2",
         DSum::nbytes, 16);

   DSum::M = others;
   //fprintf(stderr, "Req_History_log()[2]: DSum::M = %p\n", DSum::M);
   if (sizeof(Digest)%sizeof(mp_limb_t) != 0)
   {
      th_fail("Invalid assumption: sizeof(Digest)%sizeof(mp_limb_t)");
   }
   pthread_mutex_init(&rh_mutex, NULL);
}

template <class AReq>
Req_history_log<AReq>::~Req_history_log()
{
    pthread_mutex_lock(&rh_mutex);
    for (int i=0; i<rh.size(); i++)
	if (rh[i].req != NULL)
	    delete rh[i].req;
    rh.clear();
    pthread_mutex_unlock(&rh_mutex);
    delete my_dsum;
    DSum::M = others;
    //fprintf(stderr, "~Req_History_log(): DSum::M = %p\n", DSum::M);
}

template <class AReq>
Digest Req_history_log<AReq>::rh_digest()
{
   // MD5(i, last modification seqno, (data,size)
   Digest d_h;

   MD5_CTX ctx;
   MD5Init(&ctx);
   MD5Update(&ctx, (char*)&rh_d.sum, DSum::nbytes);
   MD5Final(d_h.udigest(), &ctx);
   return d_h;
}

template <class AReq>
Seqno Req_history_log<AReq>::get_top_seqno() const
{
    if (rh.size() == 0)
	return 0;
    return rh.high().seqno();
}

template <class AReq>
bool Req_history_log<AReq>::add_request(AReq *req, Seqno s, Digest &d)
{
   others = DSum::M;
   DSum::M = my_dsum;
   //fprintf(stderr, "add_request()[1]: DSum::M = %p, others = %p\n", DSum::M, others);
   rh_d.add(req->digest());
   d = rh_digest();

   //fprintf(stderr, "Will add req (%lld) at %d [%p], digest: ", s, rh.size(), req);
   //d.print();
   //fprintf(stderr, "\n");
   Rh_entry<AReq> rhe(req, s, d);

   pthread_mutex_lock(&rh_mutex);
   rh.append(rhe);
   pthread_mutex_unlock(&rh_mutex);

   DSum::M = others;
   //fprintf(stderr, "add_request()[2]: DSum::M = %p, others = %p\n", DSum::M, others);
   return true;
}

template <class AReq>
bool Req_history_log<AReq>::truncate_history(Seqno seq)
{
   pthread_mutex_lock(&rh_mutex);

   // return false when rid is not in the list
   bool found = false;
   int pos = 0;
   //fprintf(stderr, "Size is %d\n", rh.size());
   for (int i=0; i<rh.size(); i++) {
       if (rh[i].seqno() == seq) {
	   found = true;
	   pos = i;
	   break;
       }
   }
   if (!found) {
   pthread_mutex_unlock(&rh_mutex);
       return false;
}

   //fprintf(stderr, "Here\n");
   for (int i=0; i<=pos; i++) {
	   if ((rh[i].req != NULL) && (rh[i].seqno() != 0)) {
		   //fprintf(stderr, "Will delete pos i=%d, %p\n", i, rh[i].req);
		   delete rh[i].req;
       }
       rh[i].req = NULL;
       rh[i].seq = 0;
       //fprintf(stderr, "Nullify pos i=%d\n", i);
   }

   //fprintf(stderr, "Will move %d elements\n", pos+1);
   for (int i=pos+1; i<rh.size(); i++)
       rh[i-pos-1] = rh[i];
   for (int i=rh.size()-pos; i<rh.size(); i++) {
		  rh[i].req = NULL;
		  rh[i].seq = 0;
   }

   //fprintf(stderr, "Will enlarge\n");
   rh.enlarge_to(rh.size()-pos-1);
   pthread_mutex_unlock(&rh_mutex);
   //fprintf(stderr, "Will exit\n");
   return true;
}

template <class AReq>
bool Req_history_log<AReq>::truncate_history(Seqno seq, void (*request_callback)(AReq*))
{
   // return false when rid is not in the list
   bool found = false;
   int pos = 0;
   for (int i=0; i<rh.size(); i++) {
       if (rh[i].seqno() == seq) {
	   found = true;
	   pos = i;
	   break;
       }
   }
   if (!found)
       return false;

   for (int i=0; i<=pos; i++) {
       if (rh[i].req != NULL) {
	   request_callback(rh[i].req);
       }
       rh[i].req = NULL;
   }

   for (int i=pos+1; i<rh.size(); i++)
       rh[i-pos-1] = rh[i];

   rh.enlarge_to(rh.size()-pos-1);
   return true;
}

template <class AReq>
int Req_history_log<AReq>::size() const
{
   return rh.size();
}

template <class AReq>
Array<Rh_entry<AReq> >& Req_history_log<AReq>::array()
{
   return rh;
}

template <class AReq>
bool Req_history_log<AReq>::should_checkpoint()
{
    if (size() && (size() % TRUNCATE_SIZE) == 0)
		return true;

    return false;
}

template <class AReq>
int Req_history_log<AReq>::max_nb_requests_while_checkpointing()
{
return MAX_NB_REQS_CHKPT;
}


template <class AReq>
Rh_entry<AReq> *Req_history_log<AReq>::find(int cid, Request_id rid)
{
    for(int i=0; i<rh.size(); i++) {
	if (rh.slot(i).req
		&& rh.slot(i).req->client_id() == cid
		&& rh.slot(i).req->request_id() == rid) {
	    return &rh.slot(i);
	}
    }
    return NULL;
}
#endif
