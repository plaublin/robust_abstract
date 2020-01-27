#ifndef _A_Reply_h
#define _A_Reply_h 1

#include "types.h"
#include "libbyz.h"
#include "A_Message.h"
#include "Digest.h"

class A_Principal;
class A_Rep_info;

// 
// A_Reply messages have the following format.
//
struct A_Reply_rep : public A_Message_rep {
  View v;                // current view
  Request_id rid;        // unique request identifier
  Digest digest;         // digest of reply.
  int replica;           // id of A_replica sending the reply
  int reply_size;        // if negative, reply is not full.
  bool should_switch; // if true, should switch to protocol with instance_id
  enum protocols_e instance_id; // id of the instance to which should switch
  // Followed by a reply that is "reply_size" bytes long and
  // a MAC authenticating the reply to the client. The MAC is computed
  // only over the A_Reply_rep. Replies can be empty or full. Full replies
  // contain the actual reply and have reply_size >= 0. Empty replies
  // do not contain the actual reply and have reply_size < 0.
};

class A_Reply : public A_Message {
  // 
  // A_Reply messages
  //
public:
  A_Reply() : A_Message() {}

  A_Reply(A_Reply_rep *r);

  A_Reply(View view, Request_id req, int replica);
  // Effects: Creates a new (full) A_Reply message with an empty reply and no
  // authentication. The method store_reply and authenticate should
  // be used to finish message construction.

  A_Reply* copy(int id) const;
  // Effects: Creates a new object with the same state as this but
  // with A_replica identifier "id"

  char *store_reply(int &max_len);
  // Effects: Returns a pointer to the location within the message
  // where the reply should be stored and sets "max_len" to the number of
  // bytes available to store the reply. The caller can copy any reply
  // with length less than "max_len" into the returned buffer. 

  void authenticate(A_Principal *p, int act_len, bool tentative);
  // Effects: Terminates the construction of a reply message by
  // setting the length of the reply to "act_len", appending a MAC,
  // and trimming any surplus storage.

  void re_authenticate(A_Principal *p);
  // Effects: Recomputes the authenticator in the reply using the most
  // recent key.

  A_Reply(View view, Request_id req, int replica, Digest &d, 
	A_Principal *p, bool tentative);
  // Effects: Creates a new empty A_Reply message and appends a MAC for principal
  // "p".

  void commit(A_Principal *p);
  // Effects: If this is tentative converts this into an identical
  // committed message authenticated for principal "p".  Otherwise, it
  // does nothing.

  View view() const;
  // Effects: Fetches the view from the message

  Request_id request_id() const;
  // Effects: Fetches the request identifier from the message.

  int id() const;
  // Effects: Fetches the reply identifier from the message.

  Digest &digest() const;
  // Effects: Fetches the digest from the message.

  char *reply(int &len);
  // Effects: Returns a pointer to the reply and sets len to the
  // reply size.
  
  bool full() const;
  // Effects: Returns true iff "this" is a full reply.

  bool verify();
  // Effects: Verifies if the message is authenticated by rep().A_replica.

  bool match(A_Reply *r);
  // Effects: Returns true if the replies match.
  
  bool is_tentative() const;
  // Effects: Returns true iff the reply is tentative.

  static bool convert(A_Message *m1, A_Reply *&m2);
  // Effects: If "m1" has the right size and tag of a "A_Reply", casts
  // "m1" to a "A_Reply" pointer, returns the pointer in "m2" and
  // returns true. Otherwise, it returns false. Convert also trims any
  // surplus storage from "m1" when the conversion is successfull.

  bool should_switch() const;
  enum protocols_e next_instance_id() const;
  void set_instance_id(enum protocols_e nextp);

private:
  A_Reply_rep &rep() const;
  // Effects: Casts "msg" to a A_Reply_rep&
  
  friend class A_Rep_info;
};

inline A_Reply_rep& A_Reply::rep() const { 
  return *((A_Reply_rep*)msg); 
}
   
inline View A_Reply::view() const { return rep().v; }

inline Request_id A_Reply::request_id() const { return rep().rid; }

inline int A_Reply::id() const { return rep().replica; }

inline Digest& A_Reply::digest() const { return rep().digest; }

inline char* A_Reply::reply(int &len) { 
  len = rep().reply_size;
  return contents()+sizeof(A_Reply_rep);
}
  
inline bool A_Reply::full() const { return rep().reply_size >= 0; }

inline bool A_Reply::is_tentative() const { return rep().extra; }

inline bool A_Reply::match(A_Reply *r) {
  return (rep().digest == r->rep().digest) & 
    (!is_tentative() | r->is_tentative() | (view() == r->view()));
}

inline bool A_Reply::should_switch() const
{
  return rep().should_switch;
}

inline enum protocols_e A_Reply::next_instance_id() const
{
  return rep().instance_id;
}

inline void A_Reply::set_instance_id(enum protocols_e nextp)
{
  rep().should_switch = true;
  rep().instance_id = nextp;
}

#endif // _Reply_h
