#ifndef _A_Principal_h
#define _A_Principal_h 1

#include <string.h>
#include <sys/time.h>
#include "types.h"
#include "A_Time.h"

//#define USE_SECRET_SUFFIX_MD5
#ifdef USE_SECRET_SUFFIX_MD5
#include "MD5.h"
#else
extern "C"
{
#include "umac.h"
}
#endif // USE_SECRET_SUFFIX_MD5
class A_Reply;
class rabin_pub;

// Sizes in bytes.
#ifdef USE_SECRET_SUFFIX_MD5
const int MAC_size = 10;
#else
const int UMAC_size = 8;
const int UNonce_size = sizeof(long long);
const int MAC_size = UMAC_size + UNonce_size;
#endif

const int Nonce_size = 16;
const int Nonce_size_u = Nonce_size / sizeof(unsigned);
const int Key_size = 16;
const int Key_size_u = Key_size / sizeof(unsigned);

class A_Principal
{
public:
  A_Principal(int i, Addr a, char *pkey = 0);
  // Requires: "pkey" points to a null-terminated ascii encoding of
  // an integer in base-16 or is null (in which case no public-key is
  // associated with the principal.)
  // Effects: Creates a new A_Principal object.

  ~A_Principal();
  // Effects: Deallocates all the storage associated with principal.

  int pid() const;
  // Effects: Returns the principal identifier.

  const Addr *address() const;
  // Effects: Returns a pointer to the principal's address.

  Addr *addressNotConst();
  // Effects: Returns a (not constant) pointer to the principal's address.

  short get_main_thread_bind_port();
  void set_main_thread_bind_port(short port);
  //
  // Cryptography:
  //
  void set_in_key(const unsigned *k);
  // Effects: Sets the session key for incoming messages, in-key, from
  // this principal.

  bool verify_mac_in(const char *src, unsigned src_len, const char *mac,
      bool print = false);
  // Effects: Returns true iff "mac" is a valid MAC generated by
  // in-key for "src_len" bytes starting at "src".

#ifndef USE_SECRET_SUFFIX_MD5
  bool verify_mac_in(const char *src, unsigned src_len, const char *mac,
      const char *unonce, bool print = false);
  // Effects: Returns true iff "mac" is a valid MAC generated by
  // in-key for "src_len" bytes starting at "src".

  void gen_mac_in(const char *src, unsigned src_len, char *dst,
      const char *unonce, bool to_print = false);
#endif

  void gen_mac_in(const char *src, unsigned src_len, char *dst, bool to_print =
      false);
  // Requires: "dst" can hold at least "MAC_size" bytes. 
  // Effects: Generates a MAC (with MAC_size bytes) using in-key and
  // places it in "dst".  The MAC authenticates "src_len" bytes
  // starting at "src".


  bool verify_mac_out(const char *src, unsigned src_len, const char *mac,
      bool print = false);
  // Effects: Returns true iff "mac" is a valid MAC generated by
  // out-key for "src_len" bytes starting at "src".

#ifndef USE_SECRET_SUFFIX_MD5
  bool verify_mac_out(const char *src, unsigned src_len, const char *mac,
      const char *unonce, bool print = false);
  // Effects: Returns true iff "mac" is a valid MAC generated by
  // out-key for "src_len" bytes starting at "src".

  void gen_mac_out(const char *src, unsigned src_len, char* dst,
      const char *unonce, bool to_print = false);

  inline static long long new_umac_nonce()
  {
    return ++umac_nonce;
  }
#endif

  void gen_mac_out(const char *src, unsigned src_len, char *dst, bool to_print =
      false);
  // Requires: "dst" can hold at least "MAC_size" bytes.
  // Effects: Generates a MAC (with MAC_size bytes) and
  // out-key and places it in "dst".  The MAC authenticates "src_len"
  // bytes starting at "src".

  ULong last_tstamp() const;
  // Effects: Returns the last timestamp in a new-key message from
  // this principal.

  void set_out_key(unsigned *k);
  // Effects: Sets the key for outgoing messages to "k" provided "t"
  // is greater than the last value of "t" in a "set_out_key" call.

  bool is_stale(A_Time *tv) const;
  // Effects: Returns true iff tv is less than my_tstamp

  int sig_size() const;
  // Effects: Returns the size of signatures generated by this principal.

  bool verify_signature(const char *src, unsigned src_len, const char *sig,
      bool allow_self = false);
  // Requires: "sig" is at least sig_size() bytes.
  // Effects: Checks a signature "sig" (from this principal) for
  // "src_len" bytes starting at "src". If "allow_self" is false, it
  // always returns false if "this->id == A_node->id()"; otherwise,
  // returns true if signature is valid.

  unsigned encrypt(const char *src, unsigned src_len, char *dst,
      unsigned dst_len);
  // Effects: Encrypts "src_len" bytes starting at "src" using this
  // principal's public-key and places up to "dst_len" of the result in "dst".
  // Returns the number of bytes placed in "dst".


  Request_id last_fetch_rid() const;
  void set_last_fetch_rid(Request_id r);
  // Effects: Gets and sets the last request identifier in a fetch
  // message from this principal.

private:
  int id;
  Addr addr;
  rabin_pub *pkey;
  int ssize; // signature size
  unsigned kin[Key_size_u]; // session key for incoming messages from this principal
  unsigned kout[Key_size_u];// session key for outgoing messages to this principal
  ULong tstamp; // last timestamp in a new-key message from this principal
  A_Time my_tstamp; // my time when message was accepted
  short main_thread_bind_port;

  Request_id last_fetch; // Last request_id in a fetch message from this principal

  // UMAC contexts used to generate MACs for incoming and outgoing messages
  umac_ctx_t ctx_in;
  umac_ctx_t ctx_out;

  bool verify_mac(const char *src, unsigned src_len, const char *mac,
      const char *unonce, umac_ctx_t ctx, bool print = false);
  // Requires: "ctx" points to a initialized UMAC context
  // Effects: Returns true iff "mac" is a valid MAC generated by
  // key "k" for "src_len" bytes starting at "src".

  void gen_mac(const char *src, unsigned src_len, char *dst,
      const char *unonce, umac_ctx_t ctx, bool to_print = false);
  // Requires: "dst" can hold at least "MAC_size" bytes and ctx points to a 
  // initialized UMAC context.
  // Effects: Generates a UMAC and places it in "dst".  The MAC authenticates "src_len"
  // bytes starting at "src".

  // display the content of the context
  void print_context(umac_ctx_t ctx, char *header);

  static long long umac_nonce;

};

inline const Addr *A_Principal::address() const
{
  return &addr;
}

inline Addr *A_Principal::addressNotConst()
{
  return &addr;
}

inline short A_Principal::get_main_thread_bind_port()
{
  return main_thread_bind_port;
}

inline void A_Principal::set_main_thread_bind_port(short port)
{
  main_thread_bind_port = port;
}

inline int A_Principal::pid() const
{
  return id;
}

inline ULong A_Principal::last_tstamp() const
{
  return tstamp;
}

inline bool A_Principal::is_stale(A_Time *tv) const
{
  return A_lessThanTime(*tv, my_tstamp);
}

inline int A_Principal::sig_size() const
{
  return ssize;
}

inline bool A_Principal::verify_mac_in(const char *src, unsigned src_len,
    const char *mac, bool print)
{
  return verify_mac(src, src_len, mac + UNonce_size, mac, ctx_in, print);
}

inline bool A_Principal::verify_mac_in(const char *src, unsigned src_len,
    const char *mac, const char *unonce, bool print)
{
  return verify_mac(src, src_len, mac, unonce, ctx_in, print);
}

inline void A_Principal::gen_mac_in(const char *src, unsigned src_len, char *dst,
    bool to_print)
{
  ++umac_nonce;
  memcpy(dst, (char*) &umac_nonce, UNonce_size);
  dst += UNonce_size;
  gen_mac(src, src_len, dst, (char*) &umac_nonce, ctx_in, to_print);
}

inline void A_Principal::gen_mac_in(const char *src, unsigned src_len, char *dst,
    const char *unonce, bool to_print)
{
  gen_mac(src, src_len, dst, unonce, ctx_in, to_print);
}

inline bool A_Principal::verify_mac_out(const char *src, unsigned src_len,
    const char *mac, bool print)
{
  return verify_mac(src, src_len, mac + UNonce_size, mac, ctx_out, print);
}

inline bool A_Principal::verify_mac_out(const char *src, unsigned src_len,
    const char *mac, const char *unonce, bool print)
{
  return verify_mac(src, src_len, mac, unonce, ctx_out, print);
}

inline void A_Principal::gen_mac_out(const char *src, unsigned src_len,
    char *dst, bool to_print)
{
  ++umac_nonce;
  memcpy(dst, (char*) &umac_nonce, UNonce_size);
  dst += UNonce_size;
  gen_mac(src, src_len, dst, (char*) &umac_nonce, ctx_out, to_print);
}

inline void A_Principal::gen_mac_out(const char *src, unsigned src_len,
    char *dst, const char *unonce, bool to_print)
{
  gen_mac(src, src_len, dst, unonce, ctx_out, to_print);
}

inline Request_id A_Principal::last_fetch_rid() const
{
  return last_fetch;
}

inline void A_Principal::set_last_fetch_rid(Request_id r)
{
  last_fetch = r;
}

void A_random_nonce(unsigned *n);
// Requires: k is an array of at least Nonce_size bytes.  
// Effects: Places a new random nonce with size Nonce_size bytes in n.

int A_random_int();
// Effects: Returns a new random int.

#endif // _Principal_h
