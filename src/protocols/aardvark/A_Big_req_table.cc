#include "A_Request.h"
#include "A_Pre_prepare.h"
#include "A_Big_req_table.h"
#include "A_Replica.h"

#include "Array.h"
#include "bhash.t"
#include "buckets.t"


struct A_Waiting_pp {
  Seqno n;
  int i;
};

class A_BR_entry {
public:
  inline A_BR_entry() : r(0), maxn(-1), maxv(-1) {}
  inline ~A_BR_entry() { delete r; }

  Digest rd;            // A_Request's digest
  A_Request *r;           // A_Request or 0 is request not received
  Array<A_Waiting_pp> waiting; // if r=0, Seqnos of pre-prepares waiting for request
  Seqno maxn;           // Maximum seqno of pre-prepare referencing request 
  View maxv;            // Maximum view in which this entry was marked useful
};


A_Big_req_table::A_Big_req_table() : breqs(max_out), unmatched((A_Request*)0, A_node->np()) {
  max_entries = max_out*A_Pre_prepare::big_req_max;
}


A_Big_req_table::~A_Big_req_table() {
  MapGenerator<Digest,A_BR_entry*> g(breqs);
  Digest d;
  A_BR_entry* bre;
  while (g.get(d, bre)) {
    delete bre;
  }
  breqs.clear();
} 

inline void A_Big_req_table::remove_unmatched(A_BR_entry* bre) {
  if (bre->maxn < 0) {
    th_assert(bre->r != 0, "Invalid state");
    unmatched[bre->r->client_id()] = 0;      
  }
}

bool A_Big_req_table::add_pre_prepare(Digest& rd, int i, Seqno n, View v) {
  A_BR_entry* bre;
  if (breqs.find(rd, bre)) {
    remove_unmatched(bre);

    if (n > bre->maxn)
      bre->maxn = n;

    if (v > bre->maxv)
      bre->maxv = v;

    if (bre->r) {
      return true;
    } else {
      A_Waiting_pp wp;
      wp.i = i;
      wp.n = n;
      bre->waiting.append(wp);
    }
  } else {
    // No entry in breqs for rd
    bre = new A_BR_entry;
    bre->rd = rd;
    A_Waiting_pp wp;
    wp.i = i;
    wp.n = n;
    bre->waiting.append(wp);
    bre->maxn = n;
    bre->maxv = v;
    breqs.add(rd, bre);
  }

  return false;
}


void A_Big_req_table::add_pre_prepare(A_Request* r, Seqno n, View v) {
  A_BR_entry* bre;
  Digest rd = r->digest();
  if (breqs.find(rd, bre)) {
    remove_unmatched(bre);

    if (n > bre->maxn)
      bre->maxn = n;

    if (v > bre->maxv)
      bre->maxv = v;

    if (bre->r == 0) {
      bre->r = r;
    } else {
      delete r;
    } 
  } else {
    // No entry in breqs for rd
    bre = new A_BR_entry;
    bre->rd = rd;
    bre->r = r;
    bre->maxn = n;
    bre->maxv = v;
    breqs.add(rd, bre);
  }
}


bool A_Big_req_table::check_pcerts(A_BR_entry* bre) {
  th_assert(A_replica->has_new_view(), "Invalid state");

  for (int i=0; i < bre->waiting.size(); i++) {
    A_Waiting_pp wp = bre->waiting[i];
    if (A_replica->plog.within_range(wp.n)) {
      A_Prepared_cert& pc = A_replica->plog.fetch(wp.n);
      if (pc.is_pp_correct())
	return true;
    }
  }
  return false;
}


bool A_Big_req_table::add_request(A_Request* r, bool verified) {
  th_assert(r->size() > A_Request::big_req_thresh && !r->is_read_only(),
	    "Invalid Argument");
  A_BR_entry* bre;
  Digest rd = r->digest();
  if (breqs.find(rd, bre)) {
    if (bre->r == 0 
	&& (verified || !A_replica->has_new_view() || check_pcerts(bre))) {
      bre->r = r;
      while (bre->waiting.size()) {
	A_Waiting_pp wp = bre->waiting.high();
	Seqno n = wp.n;
	int i = wp.i;

	if (A_replica->has_new_view()) {
	  // Missing pre-prepare is in A_replica's plog.
	  if (A_replica->plog.within_range(n)) {
	    A_Prepared_cert& pc = A_replica->plog.fetch(n);
	    pc.add(bre->rd, i);
	    A_replica->send_prepare(pc);
	    if (pc.is_complete())
	      A_replica->send_commit(n);
	  }
	} else {
	  // Missing pre-prepare is in A_replica's view-info
	  A_replica->vi.add_missing(bre->rd, n, i);
	}

	bre->waiting.remove();				       
      }
      return true;
    }
  } else if (verified) {
    // Buffer the request with largest timestamp from client.
    int cid = r->client_id();
    Request_id rid = r->request_id();
    A_Request* old_req = unmatched[cid];

    if (old_req == 0 || old_req->request_id() < rid) {
      bre = new A_BR_entry;
      bre->rd = rd;
      bre->r = r;
      breqs.add(rd, bre);

      if (old_req) {
	breqs.remove_fast(old_req->digest());
	delete old_req;
      }

      unmatched[cid] = r;
      return true;
    } 
  }
  return false;
}


A_Request* A_Big_req_table::lookup(Digest& rd) {
  A_BR_entry* bre;
  if (breqs.find(rd, bre)) {
    return bre->r;
  }
  return 0;
}


void A_Big_req_table::mark_stable(Seqno ls) {
  if (breqs.size() > 0) {
    MapGenerator<Digest,A_BR_entry*> g(breqs);
    Digest d;
    A_BR_entry* bre;
    while (g.get(d, bre)) {
      if (bre->maxn <= ls && bre->maxv >= 0) {
	remove_unmatched(bre);
	delete bre;
	g.remove();
      }
    }
  }
}


void A_Big_req_table::view_change(View v) {
  if (breqs.size() > 0) {
    MapGenerator<Digest,A_BR_entry*> g(breqs);
    Digest d;
    A_BR_entry* bre;
    while (g.get(d, bre)) {
      if (bre->maxv < v) {
	remove_unmatched(bre);
	delete bre;
	g.remove();
      }
    }
  }
}

