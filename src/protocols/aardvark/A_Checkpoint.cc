#include "th_assert.h"
#include "A_Message_tags.h"
#include "A_Checkpoint.h"
#include "A_Replica.h"
#include "A_Principal.h"
 
A_Checkpoint::A_Checkpoint(Seqno s, Digest &d, bool stable) : 
#ifndef USE_PKEY
  A_Message(A_Checkpoint_tag, sizeof(A_Checkpoint_rep) + A_node->auth_size()) {
#else
  A_Message(A_Checkpoint_tag, sizeof(A_Checkpoint_rep) + A_node->sig_size()) {
#endif
    rep().extra = (stable) ? 1 : 0;
    rep().seqno = s;
    rep().digest = d;
    rep().id = A_node->id();
    rep().padding = 0;

#ifndef USE_PKEY
    A_node->gen_auth_out(contents(), sizeof(A_Checkpoint_rep));
#else
    A_node->gen_signature(contents(), sizeof(A_Checkpoint_rep), 
		      contents()+sizeof(A_Checkpoint_rep));
#endif
}

void A_Checkpoint::re_authenticate(A_Principal *p, bool stable) {
#ifndef USE_PKEY
  if (stable) rep().extra = 1;
  A_node->gen_auth_out(contents(), sizeof(A_Checkpoint_rep));
#else
  if (rep().extra != 1 && stable) {
    rep().extra = 1;
    A_node->gen_signature(contents(), sizeof(A_Checkpoint_rep), 
			contents()+sizeof(A_Checkpoint_rep));
  }
#endif
}

bool A_Checkpoint::verify() {
  // Checkpoints must be sent by replicas.
  if (!A_node->is_replica(id())) return false;
  
  // Check signature size.
#ifndef USE_PKEY
  if (size()-(int)sizeof(A_Checkpoint_rep) < A_node->auth_size(id())) 
    return false;

  return A_node->verify_auth_in(id(), contents(), sizeof(A_Checkpoint_rep));
#else
  if (size()-(int)sizeof(A_Checkpoint_rep) < A_node->sig_size(id())) 
    return false;

  return A_node->i_to_p(id())->verify_signature(contents(), sizeof(A_Checkpoint_rep),
					      contents()+sizeof(A_Checkpoint_rep));
#endif
}

bool A_Checkpoint::convert(A_Message *m1, A_Checkpoint  *&m2) {
  if (!m1->has_tag(A_Checkpoint_tag, sizeof(A_Checkpoint_rep)))
    return false;
  m1->trim();
  m2 = (A_Checkpoint*)m1;
  return true;
}
