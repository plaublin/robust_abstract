#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include "R_Request_history.h"
#include "R_Request.h"
#include "R_Replica.h"
#include "Array.h"
//DSum* DSum::M = 0;

R_Req_history_log::R_Req_history_log(R_Replica *rp) :
   rh(512), replica(rp)
{
   track_index = 0;
   totally_added = 0;
    last_reported = 0;

   for (unsigned int i=0; i<TRACK_HISTORY_SIZE; i++) {
       track_seqnos[i] = 0;
   }

   // The random modulus for computing sums in AdHASH.
   others = DSum::M;
   DSum::M = my_dsum = new DSum;
   mpn_set_str(
         DSum::M->sum,
         (unsigned char*)"d2a10a09a80bc599b4d60bbec06c05d5e9f9c369954940145b63a1e2",
         DSum::nbytes, 16);

   DSum::M = others;
   if (sizeof(Digest)%sizeof(mp_limb_t) != 0)
   {
      th_fail("Invalid assumption: sizeof(Digest)%sizeof(mp_limb_t)");
   }
}

R_Req_history_log::~R_Req_history_log()
{
    //for (int i=0; i<rh.size(); i++)
	//if (rh[i].rmid != NULL)
	    //delete rh[i].rmid;
    rh.clear();
    delete my_dsum;
    DSum::M = others;
}

Digest R_Req_history_log::rh_digest()
{
   // MD5(i, last modification seqno, (data,size)
   Digest d_h;

   MD5_CTX ctx;
   MD5Init(&ctx);
   MD5Update(&ctx, (char*)&rh_d.sum, DSum::nbytes);
   MD5Final(d_h.udigest(), &ctx);
   return d_h;
}

Seqno R_Req_history_log::get_top_seqno() const
{
    if (rh.size() == 0)
	return 0;
    return rh.high().seqno();
}

int R_Req_history_log::size() const
{
   return rh.size();
}

Array<R_Rh_entry >& R_Req_history_log::array()
{
   return rh;
}

bool R_Req_history_log::should_checkpoint()
{
    //if (size() && ((size() % TRUNCATE_SIZE) == 0))
    if (size() && (totally_added >= totally_checkpointed + TRUNCATE_SIZE)) {
	int last_index = (track_index-1+TRACK_HISTORY_SIZE)%TRACK_HISTORY_SIZE;
	if (track_seqnos[last_index] == last_reported)
	    return false;
	return true;
    }

    return false;
}

bool R_Req_history_log::add_request(R_BaseRequest *req, R_Message_Id rmid, Seqno s, Digest &d)
{
   others = DSum::M;
   DSum::M = my_dsum;
   rh_d.add(req->digest());
   d = rh_digest();

   R_Rh_entry rhe(rmid, s, d);

   rh.append(rhe);

   totally_added++;

   //fprintf(stderr, "add-request: Adding request %lld at pos %d\n", s, size());
   if (totally_added && (totally_added % TRUNCATE_SIZE == 0)) {
       int last_index = (track_index-1+TRACK_HISTORY_SIZE)%TRACK_HISTORY_SIZE;
       int pick_up_index = (rh.high().seqno()-track_seqnos[last_index]);
       pick_up_index = pick_up_index - pick_up_index%TRUNCATE_SIZE;
       pick_up_index--;
       //fprintf(stderr, "Tracking at position %d, values <%lld/%lld>, track id %d, <%lld>\n", pick_up_index, rh[pick_up_index].seqno(), rh.high().seqno(), track_index, last_checkpointed);
       track_seqnos[track_index] = rh[pick_up_index].seqno();
       track_digests[track_index] = rh[pick_up_index].digest();
       track_ta[track_index] = totally_added;
       track_index = (track_index+1) % TRACK_HISTORY_SIZE;
   }

   DSum::M = others;
   return true;
}

bool R_Req_history_log::truncate_history(Seqno seq)
{
    // return false when rid is not in the list
    bool found = false;
    int pos = 0;
    for (int i=0; i<rh.size(); i++) {
	if (rh[i].seqno() > seq)
	    break;
	if (rh[i].seqno() == seq) {
	    found = true;
	    pos = i;
	    break;
	}
    }
    if (!found)
	return false;

    for (int i=0; i<=pos; i++) {
	R_Message_State rms;
	if (!replica->get_request_state(rh[i].rmid, rms)) {
	    continue;
	}

	replica->rms_remove(rms);
	rh[i].rmid = R_Message_Id(0,0);
    }
   for (int i=pos+1; i<rh.size(); i++)
       rh[i-pos-1] = rh[i];

   rh.enlarge_to(rh.size()-pos-1);
   return true;
}

bool R_Req_history_log::get_next_checkpoint_data(Seqno &seqno, Digest &digest)
{
    //fprintf(stderr, "Looking for %lld, high pos = %d\n", last_checkpointed+TRUNCATE_SIZE, track_index);
    if (size() < TRACK_HISTORY_SIZE)
	return false;
    unsigned last_index = (track_index-1+TRACK_HISTORY_SIZE)%TRACK_HISTORY_SIZE;
    seqno = last_reported = track_seqnos[last_index];
    digest = track_digests[last_index];
    return true;
}
