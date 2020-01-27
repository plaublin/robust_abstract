#ifndef _A_Pre_Prepare_h
#define _A_Pre_Prepare_h 1

#include "types.h"
#include "Digest.h"
#include "A_Prepare.h"
#include "A_Message.h"
#include "A_parameters.h"

class A_Req_queue;
class A_Principal;
class A_Request;

// 
// A_Pre_prepare messages have the following format:
//
struct A_Pre_prepare_rep : public A_Message_rep {
  View view;       
  Seqno seqno;
  Digest digest;       // digest of request set concatenated with 
                       // big reqs and non-deterministic choices
  int rset_size;       // size in bytes of request set
  short n_big_reqs;    // number of big requests
  short non_det_size;  // size in bytes of non-deterministic choices
  
  // Followed by "rset_size" bytes of the request set, "n_big_reqs"
  // Digest's, "non_det_size" bytes of non-deterministic choices, and
  // a variable length signature in the above order.
};

class A_Prepare;

class A_Pre_prepare : public A_Message {
  // 
  // A_Pre_prepare messages
  //
public:
  A_Pre_prepare() : A_Message() {}

  A_Pre_prepare(View v, Seqno s, A_Req_queue &reqs);
  // Effects: Creates a new signed A_Pre_prepare message with view
  // number "v", sequence number "s", the requests in "reqs" (up to a
  // maximum size) and appropriate non-deterministic choices.  It
  // removes the elements of "reqs" that are included in the message
  // from "reqs" and deletes them.

#ifdef RRBFT_ATTACK
  // create a fake PP. Used for the RRBFT_ATTACK
  A_Pre_prepare(bool fake);

  // assess the size of the PP, given a A_Req_queue
  int assess_size(A_Req_queue &reqs);
#endif

  char* choices(int &len);
  // Effects: Returns a buffer that can be filled with non-deterministic choices

  A_Pre_prepare* clone(View v) const;
  // Effects: Creates a new object with the same state as this but view v.

  void re_authenticate(A_Principal *p=0);
  // Effects: Recomputes the authenticator in the message using the most
  // recent keys. If "p" is not null, may only update "p"'s
  // entry.

  View view() const;
  // Effects: Fetches the view number from the message.

  Seqno seqno() const;
  // Effects: Fetches the sequence number from the message.

  int id() const;
  // Effects: Returns the identifier of the primary for view() (which is
  // the A_replica that sent the message if the message is correct.)

  bool match(const A_Prepare *p) const;
  // Effects: Returns true iff "p" and "this" match.

  Digest& digest() const;
  // Effects: Fetches the digest from the message.

  class Requests_iter {
    // An iterator for yielding the Requests in a A_Pre_prepare message.
    // Requires: A A_Pre_prepare message cannot be modified while it is
    // being iterated on and all the big requests referred to by "m"
    // must be cached.
  public:
    Requests_iter(A_Pre_prepare* m);
    // Requires: A_Pre_prepare is known to be valid
    // Effects: Return an iterator for the requests in "m"
	
    bool get(A_Request& req);
    // Effects: Updates "req" to "point" to the next request in the
    // A_Pre_prepare message and returns true. If there are no more
    // requests, it returns false.

  private:
    A_Pre_prepare* msg; 
    char* next_req;
    int big_req;
  };
  friend  class Requests_iter;

  // Maximum number of big reqs in pre-prepares.
  static const int big_req_max = sizeof(BR_map)*8;  
  int num_big_reqs() const;
  // Effects: Returns the number of big request digests in this
  
  Digest& big_req_digest(int i);
  // Requires: 0 <= "i" < "num_big_reqs()"
  // Effects: Returns the digest of the i-th big request in this

  static const int NAC = 1;
  static const int NRC = 2;
  bool verify(int mode=0);
  // Effects: If "mode == 0", verifies if the message is authenticated
  // by the A_replica "id()", if the digest is correct, and if the
  // requests are authentic. If "mode == NAC", it performs all checks
  // except that it does not check if the message is authenticated by
  // the A_replica "id()". If "mode == NRC", it performs all checks
  // except that it does not verify the authenticity of the requests.

  bool check_digest();
  // Effects: Verifies if the digest is correct.

  static bool convert(A_Message* m1, A_Pre_prepare*& m2);
  // Effects: If "m1" has the right size and tag, casts "m1" to a
  // "A_Pre_prepare" pointer, returns the pointer in "m2" and returns
  // true. Otherwise, it returns false. 

private:
  A_Pre_prepare_rep& rep() const;
  // Effects: Casts contents to a A_Pre_prepare_rep&

  char* requests();
  // Effects: Returns a pointer to the first request contents.

  Digest* big_reqs();
  // Effects: Returns a pointer to the first digest of a big request
  // in this.

  char* non_det_choices();
  // Effects: Returns a pointer to the buffer with non-deterministic
  // choices.
};

inline A_Pre_prepare_rep& A_Pre_prepare::rep() const { 
  return *((A_Pre_prepare_rep*)msg); 
}

inline char* A_Pre_prepare::requests() { 
  char *ret = contents()+sizeof(A_Pre_prepare_rep);
  return ret; 
}

inline  Digest* A_Pre_prepare::big_reqs() {
  char *ret = requests()+rep().rset_size;
  return (Digest*)ret;
}

inline char* A_Pre_prepare::non_det_choices() {
  char *ret = ((char*)big_reqs())+rep().n_big_reqs*sizeof(Digest);
  return ret;
}

inline char* A_Pre_prepare::choices(int &len) {
  len = rep().non_det_size;
  return non_det_choices();
}

inline View A_Pre_prepare::view() const { return rep().view; }

inline Seqno A_Pre_prepare::seqno() const { return rep().seqno; }

inline bool A_Pre_prepare::match(const A_Prepare* p) const {
  th_assert(view() == p->view() && seqno() == p->seqno(), "Invalid argument");
  return digest() == p->digest();
}

inline Digest& A_Pre_prepare::digest() const { return rep().digest; }

inline int A_Pre_prepare::num_big_reqs() const { return rep().n_big_reqs; }
 
inline Digest& A_Pre_prepare::big_req_digest(int i) {
  th_assert(i >= 0 && i < num_big_reqs(), "Invalid argument");
  return *(big_reqs()+i);
}

#endif // _Pre_prepare_h
