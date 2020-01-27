#include "th_assert.h"
#include "A_Message_tags.h"
#include "A_Commit.h"
#include "A_Node.h"
#include "A_Replica.h"
#include "A_Principal.h"

A_Commit::A_Commit(View v, Seqno s) : 
  A_Message(A_Commit_tag, sizeof(A_Commit_rep) + A_node->auth_size()) {
    rep().view = v;
    rep().seqno = s;
    rep().id = A_node->id(); 
    rep().padding = 0;
    A_node->gen_auth_out(contents(), sizeof(A_Commit_rep));
}


A_Commit::A_Commit(A_Commit_rep *contents) : A_Message(contents) {}

void A_Commit::re_authenticate(A_Principal *p) {
  A_node->gen_auth_out(contents(), sizeof(A_Commit_rep));
}

bool A_Commit::verify() {
  // Commits must be sent by replicas.
  if (!A_node->is_replica(id()) || id() == A_node->id()) return false;

  // Check signature size.
  if (size()-(int)sizeof(A_Commit_rep) < A_node->auth_size(id())) 
    return false;

  return A_node->verify_auth_in(id(), contents(), sizeof(A_Commit_rep));
}


bool A_Commit::convert(A_Message *m1, A_Commit  *&m2) {
  if (!m1->has_tag(A_Commit_tag, sizeof(A_Commit_rep)))
    return false;

  m2 = (A_Commit*)m1;
  m2->trim();
  return true;
}

bool A_Commit::convert(char *m1, unsigned max_len, A_Commit &m2) {
  // First check if we can use m1 to create a A_Commit.
  if (!A_Message::convert(m1, max_len, A_Commit_tag, sizeof(A_Commit_rep),m2)) 
    return false;
  return true;
}
 
