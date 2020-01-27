#ifndef _Checkpoint_h
#define _Checkpoint_h 1

#include "types.h"
#include "Digest.h"
#include "A_Message.h"
class A_Principal;

// 
// A_Checkpoint messages have the following format:
//
struct A_Checkpoint_rep : public A_Message_rep {
  Seqno seqno;
  Digest digest;
  int id;         // id of the A_replica that generated the message.
  int padding;
  // Followed by a variable-sized signature.
};

class A_Checkpoint : public A_Message {
  // 
  //  A_Checkpoint messages
  //
public:
  A_Checkpoint(Seqno s, Digest &d, bool stable=false);
  // Effects: Creates a new signed A_Checkpoint message with sequence
  // number "s" and digest "d". "stable" should be true iff the checkpoint
  // is known to be stable.

  void re_authenticate(A_Principal *p=0, bool stable=false);
  // Effects: Recomputes the authenticator in the message using the
  // most recent keys. "stable" should be true iff the checkpoint is
  // known to be stable.  If "p" is not null, may only update "p"'s
  // entry. XXXX two default args is dangerous try to avoid it

  Seqno seqno() const;
  // Effects: Fetches the sequence number from the message.

  int id() const;
  // Effects: Fetches the identifier of the A_replica from the message.

  Digest &digest() const;
  // Effects: Fetches the digest from the message.

  bool stable() const;
  // Effects: Returns true iff the sender of the message believes the
  // checkpoint is stable.

  bool match(const A_Checkpoint *c) const;
  // Effects: Returns true iff "c" and "this" have the same digest

  bool verify();
  // Effects: Verifies if the message is signed by the A_replica rep().id.

  static bool convert(A_Message *m1, A_Checkpoint *&m2);
  // Effects: If "m1" has the right size and tag of a "A_Checkpoint",
  // casts "m1" to a "A_Checkpoint" pointer, returns the pointer in
  // "m2" and returns true. Otherwise, it returns false. Convert also
  // trims any surplus storage from "m1" when the conversion is
  // successfull.
 
private:
  A_Checkpoint_rep& rep() const;
  // Effects: Casts "msg" to a A_Checkpoint_rep&
};

inline A_Checkpoint_rep& A_Checkpoint::rep() const { 
  return *((A_Checkpoint_rep*)msg); 
}

inline Seqno A_Checkpoint::seqno() const { return rep().seqno; }

inline int A_Checkpoint::id() const { return rep().id; }

inline Digest& A_Checkpoint::digest() const { return rep().digest; }

inline bool A_Checkpoint::stable() const { return rep().extra == 1; }

inline bool A_Checkpoint::match(const A_Checkpoint *c) const { 
  th_assert(seqno() == c->seqno(), "Invalid argument");
  return digest() == c->digest(); 
}

#endif // _A_Checkpoint_h
