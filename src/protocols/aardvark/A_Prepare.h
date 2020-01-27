#ifndef _A_Prepare_h
#define _A_Prepare_h 1

#include "types.h"
#include "Digest.h"
#include "A_Message.h"
class A_Principal;

// 
// A_Prepare messages have the following format:
//
struct A_Prepare_rep: public A_Message_rep
{
  View view;
  Seqno seqno;
  Digest digest;
  int id; // id of the A_replica that generated the message.
  int padding;
  // Followed by a variable-sized signature.
};

class A_Prepare: public A_Message
{
  // 
  // A_Prepare messages
  //
public:
  A_Prepare(View v, Seqno s, Digest &d, A_Principal* dst = 0);
  // Effects: Creates a new signed A_Prepare message with view number
  // "v", sequence number "s" and digest "d". "dst" should be non-null
  // iff prepare is sent to a single A_replica "dst" as proof of
  // authenticity for a request.

  void re_authenticate(A_Principal *p = 0);
  // Effects: Recomputes the authenticator in the message using the
  // most recent keys. If "p" is not null, may only update "p"'s
  // entry.


  View view() const;
  // Effects: Fetches the view number from the message.

  Seqno seqno() const;
  // Effects: Fetches the sequence number from the message.

  int id() const;
  // Effects: Fetches the identifier of the A_replica from the message.

  Digest &digest() const;
  // Effects: Fetches the digest from the message.

  bool is_proof() const;
  // Effects: Returns true iff this was sent as proof of authenticity
  // for a request.

  bool match(const A_Prepare *p) const;
  // Effects: Returns true iff "p" and "this" match.

  bool verify();
  // Effects: Verifies if the message is signed by the A_replica rep().id.

  static bool convert(A_Message *m1, A_Prepare *&m2);
  // Effects: If "m1" has the right size and tag, casts "m1" to a
  // "A_Prepare" pointer, returns the pointer in "m2" and returns
  // true. Otherwise, it returns false. 

private:
  A_Prepare_rep &rep() const;
  // Effects: Casts contents to a A_Prepare_rep&

};

inline A_Prepare_rep &A_Prepare::rep() const
{
  return *((A_Prepare_rep*) msg);
}

inline View A_Prepare::view() const
{
  return rep().view;
}

inline Seqno A_Prepare::seqno() const
{
  return rep().seqno;
}

inline int A_Prepare::id() const
{
  return rep().id;
}

inline Digest& A_Prepare::digest() const
{
  return rep().digest;
}

inline bool A_Prepare::is_proof() const
{
  return rep().extra != 0;
}

inline bool A_Prepare::match(const A_Prepare *p) const
{
  th_assert(view() == p->view() && seqno() == p->seqno(), "Invalid argument");
  return digest() == p->digest();
}

#endif // _Prepare_h
