#ifndef _New_key_h
#define _New_key_h 1

#include "types.h"
#include "Digest.h"
#include "A_Message.h"
#include "A_Principal.h"

// 
// A_New_key messages have the following format:
//
struct A_New_key_rep : public A_Message_rep {
  Request_id rid;
  int id; // id of the A_replica that generated the message.
  int padding;

  // Followed by keys for all replicas except "id" in order of
  // increasing identifiers.  Each key has size Nonce_size bytes and
  // is encrypted with the public-key of the corresponding
  // A_replica. This is all followed by a signature from principal id
};

class A_New_key : public A_Message {
  // 
  //  A_New_key messages
  //
public:
  A_New_key();
  // Effects: Creates a new signed A_New_key message and updates "A_node"
  // accordingly (i.e., updates the in-keys for all principals.) 

  int id() const;
  // Effects: Fetches the identifier of the A_replica from the message.

  bool verify();
  // Effects: Verifies if the message is signed by the principal
  // rep().id. If the message is correct updates the entry for
  // rep().id accordingly (i.e., out-key, tstamp.)

  static bool convert(A_Message *m1, A_New_key *&m2);
  // Effects: If "m1" has the right size and tag of a "A_New_key",
  // casts "m1" to a "A_New_key" pointer, returns the pointer in
  // "m2" and returns true. Otherwise, it returns false.
  // If the conversion is successful it trims excess allocation.

private:
  A_New_key_rep &rep() const;
  // Effects: Casts "msg" to a A_New_key_rep&
};


inline A_New_key_rep& A_New_key::rep() const { 
  return *((A_New_key_rep*)msg); 
}

inline int A_New_key::id() const { return rep().id; }

#endif // _New_key_h
