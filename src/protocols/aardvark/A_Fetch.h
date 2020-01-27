#ifndef _Fetch_h
#define _Fetch_h 1

#include "types.h"
#include "Digest.h"
#include "A_Message.h"
#include "A_State_defs.h"

class A_Principal;

// 
// A_Fetch messages have the following format:
//
struct A_Fetch_rep : public A_Message_rep {
  Request_id rid;     // sequence number to prevent replays
  int level;          // level of partition
  int index;          // index of partition within level
  Seqno lu;           // information for partition is up-to-date till seqno lu
  Seqno rc;           // specific checkpoint requested (-1) if none
  int repid;          // id of designated replier (valid if c >= 0)
  int id;             // id of the A_replica that generated the message.

  // Followed by an authenticator.
};

class A_Fetch : public A_Message {
  // 
  // A_Fetch messages
  //
public:
  A_Fetch(Request_id rid, Seqno lu, int level, int index,
	Seqno rc=-1, int repid=-1);
  // Effects: Creates a new authenticated A_Fetch message.

  void re_authenticate(A_Principal *p=0);
  // Effects: Recomputes the authenticator in the message using the
  // most recent keys. If "p" is not null, may only update "p"'s
  // entry.

  Request_id request_id() const;
  // Effects: Fetches the request identifier from the message.

  Seqno last_uptodate() const;
  // Effects: Fetches the last up-to-date sequence number from the message.

  int level() const;
  // Effects: Returns the level of the partition  

  int index() const;
  // Effects: Returns the index of the partition within its level

  int id() const;
  // Effects: Fetches the identifier of the A_replica from the message.



  Seqno checkpoint() const;
  // Effects: Returns the specific checkpoint requested or -1

  int replier() const;
  // Effects: If checkpoint() > 0, returns the designated replier. Otherwise,
  // returns -1;

  bool verify();
  // Effects: Verifies if the message is correctly authenticated by
  // the A_replica id().

  static bool convert(A_Message *m1, A_Fetch *&m2);
  // Effects: If "m1" has the right size and tag, casts "m1" to a
  // "A_Fetch" pointer, returns the pointer in "m2" and returns
  // true. Otherwise, it returns false. 
 
private:
  A_Fetch_rep &rep() const;
  // Effects: Casts contents to a A_Fetch_rep&

};


inline A_Fetch_rep &A_Fetch::rep() const { 
  return *((A_Fetch_rep*)msg); 
}

inline Request_id A_Fetch::request_id() const { return rep().rid; }

inline  Seqno  A_Fetch::last_uptodate() const { return rep().lu; }

inline int A_Fetch::level() const { return rep().level; }

inline int A_Fetch::index() const { return rep().index; }

inline int A_Fetch::id() const { return rep().id; }


inline Seqno A_Fetch::checkpoint() const { return rep().rc; }

inline int A_Fetch::replier() const { return rep().repid; }



#endif // _Fetch_h
