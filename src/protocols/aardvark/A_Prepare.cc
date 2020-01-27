#include "th_assert.h"
#include "A_Message_tags.h"
#include "A_Prepare.h"
#include "A_Node.h"
#include "A_Replica.h"
#include "A_Principal.h"

A_Prepare::A_Prepare(View v, Seqno s, Digest &d, A_Principal* dst) :
  A_Message(A_Prepare_tag, sizeof(A_Prepare_rep)
#ifndef USE_PKEY
      + ((dst) ? MAC_size : A_node->auth_size()))
{
#else
  + ((dst) ? MAC_size : A_node->sig_size()))
  {
#endif
  rep().extra = (dst) ? 1 : 0;
  rep().view = v;
  rep().seqno = s;
  rep().digest = d;
  rep().id = A_node->id();
  rep().padding = 0;
  if (!dst)
  {
#ifndef USE_PKEY
    A_node->gen_auth_out(contents(), sizeof(A_Prepare_rep));
#else
    A_node->gen_signature(contents(), sizeof(A_Prepare_rep),
        contents()+sizeof(A_Prepare_rep));
#endif
  }
  else
  {
    dst->gen_mac_out(contents(), sizeof(A_Prepare_rep), contents()
        + sizeof(A_Prepare_rep));
  }
}

void A_Prepare::re_authenticate(A_Principal *p)
{
  if (rep().extra == 0)
  {
#ifndef USE_PKEY
    A_node->gen_auth_out(contents(), sizeof(A_Prepare_rep));
#endif
  }
  else
    p->gen_mac_out(contents(), sizeof(A_Prepare_rep), contents()
        + sizeof(A_Prepare_rep));
}

bool A_Prepare::verify()
{
  // This type of message should only be sent by a A_replica other than me
  // and different from the primary
  if (!A_node->is_replica(id()) || id() == A_node->id())
    return false;

  if (rep().extra == 0)
  {
    // Check signature size.
#ifndef USE_PKEY
    if (A_replica->primary(view()) == id() || size() - (int) sizeof(A_Prepare_rep)
        < A_node->auth_size(id()))
      return false;

    return A_node->verify_auth_in(id(), contents(), sizeof(A_Prepare_rep));
#else
    if (A_replica->primary(view()) == id() ||
        size()-(int)sizeof(A_Prepare_rep) < A_node->sig_size(id()))
    return false;

    return A_node->i_to_p(id())->verify_signature(contents(), sizeof(A_Prepare_rep),
        contents()+sizeof(A_Prepare_rep));
#endif

  }
  else
  {
    if (size() - (int) sizeof(A_Prepare_rep) < MAC_size)
      return false;

    return A_node->i_to_p(id())->verify_mac_in(contents(), sizeof(A_Prepare_rep),
        contents() + sizeof(A_Prepare_rep));
  }

  return false;
}

bool A_Prepare::convert(A_Message *m1, A_Prepare *&m2)
{
  if (!m1->has_tag(A_Prepare_tag, sizeof(A_Prepare_rep)))
    return false;

  m2 = (A_Prepare*) m1;
  m2->trim();
  return true;
}

