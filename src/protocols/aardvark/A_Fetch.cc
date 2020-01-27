#include "th_assert.h"
#include "A_Message_tags.h"
#include "Partition.h"
#include "A_Fetch.h"
#include "A_Node.h"
#include "A_Replica.h"
#include "A_Principal.h"
#include "A_State_defs.h"

A_Fetch::A_Fetch(Request_id rid, Seqno lu, int level, int index,
	     Seqno rc, int repid) :
  A_Message(A_Fetch_tag, sizeof(A_Fetch_rep) + A_node->auth_size()) {
  rep().rid = rid;
  rep().lu = lu;
  rep().level = level;
  rep().index = index;
  rep().rc = rc;
  rep().repid = repid;
  rep().id = A_node->id();

  A_node->gen_auth_in(contents(), sizeof(A_Fetch_rep));
}

void A_Fetch::re_authenticate(A_Principal *p) {
  A_node->gen_auth_in(contents(), sizeof(A_Fetch_rep));
}

bool A_Fetch::verify() {
  if (!A_node->is_replica(id())) 
    return false;
  
  if (level() < 0 || level() >= PLevels)
    return false;
  
  if (index() < 0 || index() >=  PLevelSize[level()])
    return false;
  
  if (checkpoint() == -1 && replier() != -1)
    return false; 


  // Check signature size.
  if (size()-(int)sizeof(A_Fetch_rep) < A_node->auth_size(id())) 
    return false;

  return A_node->verify_auth_out(id(), contents(), sizeof(A_Fetch_rep));
}


bool A_Fetch::convert(A_Message *m1, A_Fetch  *&m2) {
  if (!m1->has_tag(A_Fetch_tag, sizeof(A_Fetch_rep)))
    return false;

  m2 = (A_Fetch*)m1;
  m2->trim();
  return true;
}
 


