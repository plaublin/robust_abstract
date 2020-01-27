#include <stdlib.h>
#include <strings.h>
#include "A_Principal.h"
#include "A_Node.h"
#include "A_Reply.h"

#include "sfslite/crypt.h"
#include "sfslite/rabin.h"
#include "umac.h"

A_Principal::A_Principal(int i, Addr a, char *p)
{
  id = i;
  addr = a;
  last_fetch = 0;

  if (p == 0)
  {
    pkey = 0;
    ssize = 0;
  }
  else
  {
    bigint b(p, 16);
    ssize = (mpz_sizeinbase2(&b) >> 3) + 1 + sizeof(unsigned);
    pkey = new rabin_pub(b);
  }

  for (int j = 0; j < 4; j++)
  {
    kin[j] = 0;
    kout[j] = 0;
  }

#ifndef USE_SECRET_SUFFIX_MD5
  ctx_in = 0;
  ctx_out = umac_new((char*) kout);
#endif

  tstamp = 0;
  my_tstamp = A_zeroTime();
}

A_Principal::~A_Principal()
{
  delete pkey;
}

void A_Principal::set_in_key(const unsigned *k)
{
  /*fprintf(stderr, "Setting a new key_in for principal %i:", id);
   for (int i = 0; i < Key_size / 4; i++)
   {
   fprintf(stderr, " %x", k[i]);
   }
   fprintf(stderr, "\n");
   fflush(NULL);
   */

  memcpy(kin, k, Key_size);

#ifndef USE_SECRET_SUFFIX_MD5
  if (ctx_in)
    umac_delete(ctx_in);
  ctx_in = umac_new((char*) kin);
#endif

}

// display the content of the context
void A_Principal::print_context(umac_ctx_t ctx, char *header)
{

  fprintf(stderr, "[%s] content of ctx (size=%i):", header, sizeof(*ctx));

  int* ctx_as_int = (int*) ctx;
  for (int i = 0; i < sizeof(*ctx) / 4; i++)
  {
    fprintf(stderr, " %x", ctx_as_int[i]);
  }
  fprintf(stderr, "\n");

}

bool A_Principal::verify_mac(const char *src, unsigned src_len, const char *mac,
    const char *unonce, umac_ctx_t ctx, bool print)
{
  char tag[20];

  umac(ctx, (char *) src, src_len, tag, (char *) unonce);
  bool ret = !memcmp(tag, mac, UMAC_size);

  return A_Message::is_mac_valid((A_Message_rep*)src);
}

long long A_Principal::umac_nonce = 0;

void A_Principal::gen_mac(const char *src, unsigned src_len, char *dst,
    const char *unonce, umac_ctx_t ctx, bool to_print)
{
  umac(ctx, (char *) src, src_len, dst, (char *) unonce);

  A_Message::set_mac_valid((A_Message_rep*)src);
}

void A_Principal::set_out_key(unsigned *k)
{
  /*fprintf(stderr, "Setting a new key_out for principal %i:", id);
   for (int i = 0; i < Key_size / 4; i++)
   {
   fprintf(stderr, " %x", k[i]);
   }
   fprintf(stderr, "\n");
   fflush(NULL);
   */

  memcpy(kout, k, Key_size);

  if (ctx_out)
    umac_delete(ctx_out);
  ctx_out = umac_new((char*) kout);

  tstamp = A_currentTime();
  my_tstamp = A_currentTime();
}

bool A_Principal::verify_signature(const char *src, unsigned src_len,
    const char *sig, bool allow_self)
{
  // A_Principal never verifies its own authenticator.
  if (id == A_node->id() & !allow_self)
    return false;

  //INCR_OP(num_sig_ver);
  //START_CC(sig_ver_cycles);

  bigint bsig;
  int s_size;
  memcpy((char*)&s_size, sig, sizeof(int));
  sig += sizeof(int);
  if (s_size + (int) sizeof(int) > sig_size())
  {
    //STOP_CC(sig_ver_cycles);
    return false;
  }

  mpz_set_raw(&bsig, sig, s_size);
  bool ret = pkey->verify(str(src, src_len), bsig);

  //STOP_CC(sig_ver_cycles);
#ifdef NO_CLIENT_SIGNATURES
  return true;
#else
  return ret;
#endif
}

unsigned A_Principal::encrypt(const char *src, unsigned src_len, char *dst,
    unsigned dst_len)
{
  // This is rather inefficient if message is big but messages will
  // be small.
  bigint ctext = pkey->encrypt(str(src, src_len));
  unsigned size = mpz_rawsize(&ctext);
  if (dst_len < size + 2 * sizeof(unsigned))
    return 0;

  memcpy(dst, (char*)&src_len, sizeof(unsigned));
  dst += sizeof(unsigned);
  memcpy(dst, (char*)&size, sizeof(unsigned));
  dst += sizeof(unsigned);

  mpz_get_raw(dst, size, &ctext);
  return size + 2 * sizeof(unsigned);
}

void A_random_nonce(unsigned *n)
{
  bigint n1 = random_bigint(Nonce_size * 8);
  mpz_get_raw((char*) n, Nonce_size, &n1);
}

int A_random_int()
{
  bigint n1 = random_bigint(sizeof(int) * 8);
  int i;
  mpz_get_raw((char*) &i, sizeof(int), &n1);
  return i;
}

