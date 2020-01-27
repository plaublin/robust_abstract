#ifndef _A_Commit_h
#define _A_Commit_h 1

#include "types.h"
#include "A_Message.h"
class A_Principal;

// 
// A_Commit messages have the following format.
//
struct A_Commit_rep : public A_Message_rep {
  View view;       
  Seqno seqno;
  int id;         // id of the A_replica that generated the message.
  int padding;
  // Followed by a variable-sized signature.
};

class A_Commit : public A_Message {
  // 
  // A_Commit messages
  //
public:
  A_Commit(View v, Seqno s);
  // Effects: Creates a new A_Commit message with view number "v"
  // and sequence number "s".

  A_Commit(A_Commit_rep *contents);
  // Requires: "contents" contains a valid A_Commit_rep. If
  // contents may not be a valid A_Commit_rep use the static
  // method convert.
  // Effects: Creates a A_Commit message from "contents". No copy
  // is made of "contents" and the storage associated with "contents"
  // is not deallocated if the message is later deleted.

  void re_authenticate(A_Principal *p=0);
  // Effects: Recomputes the authenticator in the message using the
  // most recent keys. If "p" is not null, may only update "p"'s
  // entry.

  View view() const;
  // Effects: Fetches the view number from the message.

  Seqno seqno() const;
  // Effects: Fetches the sequence number from the message.

  int id() const;
  // Effects: Fetches the identifier of the A_replica from the message.

  bool match(const A_Commit *c) const;
  // Effects: Returns true iff this and c match.

  bool verify();
  // Effects: Verifies if the message is signed by the A_replica rep().id.

  static bool convert(A_Message *m1, A_Commit *&m2);
  // Effects: If "m1" has the right size and tag of a "A_Commit",
  // casts "m1" to a "A_Commit" pointer, returns the pointer in
  // "m2" and returns true. Otherwise, it returns false. 

  static bool convert(char *m1, unsigned max_len, A_Commit &m2);
  // Requires: convert can safely read up to "max_len" bytes starting
  // at "m1".
  // Effects: If "m1" has the right size and tag of a
  // "A_Commit_rep" assigns the corresponding A_Commit to m2 and
  // returns true.  Otherwise, it returns false.  No copy is made of
  // m1 and the storage associated with "contents" is not deallocated
  // if "m2" is later deleted.

 
private:
  A_Commit_rep &rep() const;
  // Effects: Casts "msg" to a A_Commit_rep&
};


inline A_Commit_rep& A_Commit::rep() const { 
  return *((A_Commit_rep*)msg); 
}

inline View A_Commit::view() const { return rep().view; }

inline Seqno A_Commit::seqno() const { return rep().seqno; }

inline int A_Commit::id() const { return rep().id; }

inline bool A_Commit::match(const A_Commit *c) const {
  th_assert(view() == c->view() && seqno() == c->seqno(), "Invalid argument");
  return true; 
}

#endif // _A_Commit_h
