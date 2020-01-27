#include <stdlib.h>
#include <string.h>

#include "A_Rep_info.h"
#include "A_Replica.h"
#include "A_Reply.h"
#include "A_Req_queue.h"

#include "A_Statistics.h"
#include "A_State_defs.h"

#include "Array.h"


A_Rep_info::A_Rep_info(char *m, int sz, int n) {
  th_assert(n != 0, "Invalid argument");

  nps = n;
  mem = m;

  if (sz < (nps+1)*Max_rep_size)
    th_fail("Memory is too small to hold replies for all principals");
 
  
  int old_nps = *((Long*)mem);
  if (old_nps != 0) {
    // Memory has already been initialized.
    if (nps != old_nps)
      th_fail("Changing number of principals. Not implemented yet");
  } else {
    // Initialize memory.
    bzero(mem, (nps+1)*Max_rep_size);
    for (int i=0; i < nps; i++) {
      // Wasting first page just to store the number of principals.
      A_Reply_rep* rr = (A_Reply_rep*)(mem+(i+1)*Max_rep_size);
      rr->tag = A_Reply_tag;
      rr->reply_size = -1;
      rr->rid = 0;
    }
    *((Long*)mem) = nps;
  }

  struct Rinfo ri;
  ri.tentative = true;
  ri.lsent = A_zeroTime();

  for (int i=0; i < nps; i++) {
    A_Reply_rep *rr = (A_Reply_rep*)(mem+(i+1)*Max_rep_size);
    th_assert(rr->tag == A_Reply_tag, "Corrupt memory");
    reps.append(new A_Reply(rr));
    ireps.append(ri);
  }
}


A_Rep_info::~A_Rep_info() {
  for (int i=0; i < nps; i++) 
    delete reps[i];
}


char* A_Rep_info::new_reply(int pid, int &sz) {
  A_Reply* r = reps[pid];


  A_replica->modify(r->contents(), Max_rep_size);

  ireps[pid].tentative = true;
  ireps[pid].lsent = A_zeroTime();
  r->rep().reply_size = -1;
  sz = Max_rep_size-sizeof(A_Reply_rep)-MAC_size;
  return r->contents()+sizeof(A_Reply_rep);
}


void A_Rep_info::end_reply(int pid, Request_id rid, int sz) {
  A_Reply* r = reps[pid];
  th_assert(r->rep().reply_size == -1, "Invalid state");

  A_Reply_rep& rr = r->rep();
  rr.rid = rid;
  rr.reply_size = sz;
  rr.digest = Digest(r->contents()+sizeof(A_Reply_rep), sz);

  int old_size = sizeof(A_Reply_rep)+rr.reply_size;
  r->set_size(old_size+MAC_size);
  bzero(r->contents()+old_size, MAC_size);
}

void A_Rep_info::send_reply(int pid, View v, int id, bool tentative) {
  A_Reply *r = reps[pid];
  A_Reply_rep& rr = r->rep();
  int old_size = sizeof(A_Reply_rep)+rr.reply_size;

  th_assert(rr.reply_size != -1, "Invalid state");
  th_assert(rr.extra == 0 && rr.v == 0 && rr.replica == 0, "Invalid state");

  if (!tentative && ireps[pid].tentative) {
    ireps[pid].tentative = false;
    ireps[pid].lsent = A_zeroTime();
  }

  A_Time cur;
  A_Time& lsent = ireps[pid].lsent;
#ifdef USE_GETTIMEOFDAY
  if ( (lsent.tv_sec != 0) || (lsent.tv_usec != 0) ) {
#else
  if (lsent != 0) {
#endif
    cur = A_currentTime();
    if (A_diffTime(cur, lsent) <= 10000) 
      return;

    lsent = cur;
  }
  
  if (ireps[pid].tentative) rr.extra = 1;
  rr.v = v;
  rr.replica = id;
  A_Principal *p = A_node->i_to_p(pid);

  //INCR_OP(reply_auth);
  //START_CC(reply_auth_cycles);
  p->gen_mac_out(r->contents(), sizeof(A_Reply_rep), r->contents()+old_size);
  //STOP_CC(reply_auth_cycles);

  //A_node->send(r, pid);
  A_replica->send(r,pid);
  // Undo changes. To ensure state matches across all replicas.
  rr.extra = 0;
  rr.v = 0;
  rr.replica = 0;
  bzero(r->contents()+old_size, MAC_size);
}


bool A_Rep_info::new_state(A_Req_queue *rset) {
  bool first=false;
  for (int i=0; i < nps; i++) {
    commit_reply(i);

    // Remove requests from rset with stale timestamps.
    if (rset->remove(i, req_id(i)))
      first = true;
  }
  return first;
}
