#include "th_assert.h"
#include "A_Message_tags.h"
#include "A_New_key.h"
#include "A_Node.h"
#include "A_Principal.h"

A_New_key::A_New_key() :
  A_Message(A_New_key_tag, A_Max_message_size)
{
  unsigned k[Nonce_size_u];

  rep().rid = A_node->new_rid();
  rep().padding = 0;
  A_node->principal()->set_out_key(k);
  rep().id = A_node->id();

  // Get new keys and encrypt them
  A_Principal *p;
  char *dst = contents() + sizeof(A_New_key_rep);
  int dst_len = A_Max_message_size - sizeof(A_New_key_rep);
  for (int i = 0; i < A_node->n(); i++)
  {
    // Skip myself.
    if (i == A_node->id())
      continue;

    A_random_nonce(k);
    p = A_node->i_to_p(i);
    p->set_in_key(k);
    unsigned ssize = p->encrypt((char *) k, Nonce_size, dst, dst_len);
    th_assert(ssize != 0, "Message is too small");
    dst += ssize;
    dst_len -= ssize;
  }
  // set my size to reflect the amount of space in use
  set_size(A_Max_message_size - dst_len);

  // Compute signature and update size.
  p = A_node->principal();
  int old_size = size();
  th_assert(dst_len >= p->sig_size(), "Message is too small");
  set_size(size() + p->sig_size());
  A_node->gen_signature(contents(), old_size, contents() + old_size);
}

bool A_New_key::verify()
{
  // If bad principal or old message discard.
  A_Principal *p = A_node->i_to_p(id());
  if (p == 0 || p->last_tstamp() >= rep().rid)
  {
    return false;
  }

  char *dst = contents() + sizeof(A_New_key_rep);
  int dst_len = size() - sizeof(A_New_key_rep);
  unsigned k[Nonce_size_u];

  for (int i = 0; i < A_node->n(); i++)
  {
    // Skip principal that sent message
    if (i == id())
      continue;

    int ssize = cypher_size(dst, dst_len);
    if (ssize == 0)
      return false;

    if (i == A_node->id())
    {
      // found my key
      int ksize = A_node->decrypt(dst, dst_len, (char *) k, Nonce_size);
      if (ksize != Nonce_size)
        return false;
    }

    dst += ssize;
    dst_len -= ssize;
  }

  // Check signature    
  int aligned = dst - contents();
  if (dst_len < p->sig_size() || !p->verify_signature(contents(), aligned,
      contents() + aligned))
    return false;

  //PL: we no longer call set_XXX_key(): p->set_out_key(k);

  return true;
}

bool A_New_key::convert(A_Message *m1, A_New_key *&m2)
{
  if (!m1->has_tag(A_New_key_tag, sizeof(A_New_key_rep)))
    return false;

  m1->trim();
  m2 = (A_New_key*) m1;
  return true;
}
