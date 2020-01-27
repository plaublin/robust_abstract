#include <strings.h>
#include "th_assert.h"
#include "A_Message_tags.h"
#include "A_Reply.h"
#include "A_Node.h"
#include "A_Principal.h"

#include "A_Statistics.h"

A_Reply::A_Reply(View view, Request_id req, int replica) :
  A_Message(A_Reply_tag, A_Max_message_size) {
    rep().v = view;
    rep().rid = req;
    rep().replica = replica;
    rep().reply_size = 0;
    set_size(sizeof(A_Reply_rep));
}


A_Reply::A_Reply(A_Reply_rep *r) : A_Message(r) {}


A_Reply::A_Reply(View view, Request_id req, int replica, Digest &d,
       A_Principal *p, bool tentative) :
  A_Message(A_Reply_tag, sizeof(A_Reply_rep)+MAC_size) {

  if (tentative) rep().extra = 1;
  else rep().extra = 0;

    rep().v = view;
    rep().rid = req;
    rep().replica = replica;
    rep().reply_size = -1;
    rep().digest = d;
    rep().should_switch = false;

    //INCR_OP(reply_auth);
    //START_CC(reply_auth_cycles);
    p->gen_mac_out(contents(), sizeof(A_Reply_rep), contents()+sizeof(A_Reply_rep));
    //STOP_CC(reply_auth_cycles);
}


A_Reply* A_Reply::copy(int id) const {
  A_Reply* ret = (A_Reply*)new A_Message(msg->size);
  memcpy(ret->msg, msg, msg->size);
  ret->rep().replica = id;
  return ret;
}


char *A_Reply::store_reply(int &max_len) {
  max_len = msize()-sizeof(A_Reply_rep)-MAC_size;
  return contents()+sizeof(A_Reply_rep);
}


void A_Reply::authenticate(A_Principal *p, int act_len, bool tentative) {
  th_assert((unsigned)act_len <= msize()-sizeof(A_Reply_rep)-MAC_size,
      "Invalid reply size");

  if (tentative) rep().extra = 1;

  rep().reply_size = act_len;
  rep().digest = Digest(contents()+sizeof(A_Reply_rep), act_len);
  int old_size = sizeof(A_Reply_rep)+act_len;
  set_size(old_size+MAC_size);

  //INCR_OP(reply_auth);
  //START_CC(reply_auth_cycles);
  p->gen_mac_out(contents(), sizeof(A_Reply_rep), contents()+old_size);
  //STOP_CC(reply_auth_cycles);

  trim();
}


void A_Reply::re_authenticate(A_Principal *p) {
  int old_size = sizeof(A_Reply_rep)+rep().reply_size;

  //INCR_OP(reply_auth);
  //START_CC(reply_auth_cycles);
  p->gen_mac_out(contents(), sizeof(A_Reply_rep), contents()+old_size);
  //STOP_CC(reply_auth_cycles);
}


void A_Reply::commit(A_Principal *p) {
  if (rep().extra == 0) return; // A_Reply is already committed.

  rep().extra = 0;
  int old_size = sizeof(A_Reply_rep)+rep().reply_size;
  p->gen_mac_out(contents(), sizeof(A_Reply_rep), contents()+old_size);
}


bool A_Reply::verify() {
  // Replies must be sent by replicas.
  if (!A_node->is_replica(id()))
    return false;

  // Check sizes
  int rep_size = (full()) ? rep().reply_size : 0;
  if (size()-(int)sizeof(A_Reply_rep)-rep_size < MAC_size)
    return false;

  // Check reply
  if (full()) {
    Digest d(contents()+sizeof(A_Reply_rep), rep_size);
    if (d != rep().digest)
      return false;
  }

  // Check signature.
  A_Principal *replica = A_node->i_to_p(rep().replica);
  int size_wo_MAC = sizeof(A_Reply_rep)+rep_size;

  //INCR_OP(reply_auth_ver);
  //START_CC(reply_auth_ver_cycles);

  bool ret = replica->verify_mac_in(contents(), sizeof(A_Reply_rep), contents()+size_wo_MAC);

  //STOP_CC(reply_auth_ver_cycles);

  return ret;
}


bool A_Reply::convert(A_Message *m1, A_Reply *&m2) {
  if (!m1->has_tag(A_Reply_tag, sizeof(A_Reply_rep)))
    return false;

  m1->trim();
  m2 = (A_Reply*)m1;
  return true;
}
