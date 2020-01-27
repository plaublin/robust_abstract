#include "th_assert.h"
#include "A_Message_tags.h"
#include "A_View_change_ack.h"
#include "A_Node.h"
#include "A_Principal.h"

A_View_change_ack::A_View_change_ack(View v, int id, int vcid, Digest const &vcd) :
  A_Message(A_View_change_ack_tag, sizeof(A_View_change_ack_rep) + MAC_size) {
    rep().v = v;
    rep().id = A_node->id();
    rep().vcid = vcid;
    rep().vcd = vcd;
    
    int old_size = sizeof(A_View_change_ack_rep);
    set_size(old_size+MAC_size);
    A_Principal *p = A_node->i_to_p(A_node->primary(v));
    p->gen_mac_out(contents(), old_size, contents()+old_size);
}

void A_View_change_ack::re_authenticate(A_Principal *p) {
  p->gen_mac_out(contents(), sizeof(A_View_change_ack_rep), contents()+sizeof(A_View_change_ack_rep));
}

bool A_View_change_ack::verify() {
  // These messages must be sent by replicas other than me, the A_replica that sent the 
  // corresponding view-change, or the primary.
  if (!A_node->is_replica(id()) || id() == A_node->id() 
      || id() == vc_id() || A_node->primary(view()) == id())
    return false;

  if (view() <= 0 || !A_node->is_replica(vc_id()))
    return false;

  // Check sizes
  if (size()-(int)sizeof(A_View_change_ack) < MAC_size) 
    return false;

  // Check MAC.
  A_Principal *p = A_node->i_to_p(id());
  int old_size = sizeof(A_View_change_ack_rep);
  if (!p->verify_mac_in(contents(), old_size, contents()+old_size))
    return false;

  return true;
}


bool A_View_change_ack::convert(A_Message *m1, A_View_change_ack  *&m2) {
  if (!m1->has_tag(A_View_change_ack_tag, sizeof(A_View_change_ack_rep)))
    return false;

  m2 = (A_View_change_ack*)m1;
  m2->trim();
  return true;
}
