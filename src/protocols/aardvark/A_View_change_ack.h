#ifndef _A_View_change_ack_h
#define _A_View_change_ack_h 1

#include "types.h"
#include "Digest.h"
#include "A_Message.h"
class A_Principal;

// 
// A_View_change_ack messages have the following format:
//
struct A_View_change_ack_rep : public A_Message_rep {
  View v;
  int id;
  int vcid;
  Digest vcd;
  // Followed by a MAC for the intended recipient
};

class A_View_change_ack : public A_Message {
  // 
  // A_View_change_ack messages
  //
public:
  A_View_change_ack(View v, int id, int vcid, Digest const &vcd);
  // Effects: Creates a new authenticated A_View_change_ack message for
  // A_replica "id" stating that A_replica "vcid" sent out a view-change
  // message for view "v" with digest "vcd". The MAC is for the primary
  // of "v".

  void re_authenticate(A_Principal *p=0);
  // Effects: Recomputes the MAC in the message using the
  // most recent keys. If "p" is not null, computes a MAC for "p"
  // rather than for the primary of "view()".

  View view() const;
  // Effects: Fetches the view number from the message.

  int id() const;
  // Effects: Fetches the identifier of the A_replica from the message.
  
  int vc_id() const;
  // Effects: Fetches the identifier of the A_replica whose view-change
  // message is being acked.
 
  Digest& vc_digest() const;
  // Effects: Fetches the digest of the view-change message that is
  // being acked.

  bool match(const A_View_change_ack* p) const;
  // Effects: Returns true iff "p" and "this" match.

  bool verify();
  // Effects: Verifies if the message is signed by the A_replica rep().id.

  static bool convert(A_Message *m1, A_View_change_ack *&m2);
  // Effects: If "m1" has the right size and tag, casts "m1" to a
  // "A_View_change_ack" pointer, returns the pointer in "m2" and returns
  // true. Otherwise, it returns false. 

private:
  A_View_change_ack_rep& rep() const;
  // Effects: Casts contents to a A_View_change_ack_rep&

};


inline A_View_change_ack_rep& A_View_change_ack::rep() const { 
  return *((A_View_change_ack_rep*)msg); 
}

inline View A_View_change_ack::view() const { return rep().v; }

inline int A_View_change_ack::id() const { return rep().id; }

inline int A_View_change_ack::vc_id() const { return rep().vcid; }

inline Digest& A_View_change_ack::vc_digest() const { return rep().vcd; }

inline bool A_View_change_ack::match(const A_View_change_ack *p) const { 
  th_assert(view() == p->view(), "Invalid argument");
  return vc_id() == p->vc_id() && vc_digest() == p->vc_digest();
}

#endif // _View_change_ack_h
