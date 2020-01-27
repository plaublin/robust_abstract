#ifndef _C_Client_notification_h
#define _C_Client_notification_h 1

#include "C_Message.h"
#include "types.h"

const int C_Client_notification_request = 0;
const int C_Client_notification_reply = 1;

//
// C_Client_notification messages have the following format.
//
struct C_Client_notification_rep: public C_Message_rep {
   short op_type; // is this for a request or a reply?
	int cid; // id of the client
	Request_id rid; // id of the request
};

class C_Client_notification: public C_Message {
public:
	C_Client_notification();
	C_Client_notification(short op, int cid, Request_id rid);
	~C_Client_notification();

	int get_cid() const;
	Request_id get_rid() const;
	short get_op_type() const;

	static bool convert(C_Message *m1, C_Client_notification *&m2);
	// Effects: If "m1" has the right size and tag of a "C_Client_notification",
	// casts "m1" to a "C_Client_notification" pointer, returns the pointer in
	// "m2" and returns true. Otherwise, it returns false.

private:
	C_Client_notification_rep &rep() const;
	// Effects: Casts "msg" to a C_Client_notification_rep&
};

inline int C_Client_notification::get_cid() const { return rep().cid; }
inline Request_id C_Client_notification::get_rid() const { return rep().rid; }
inline short C_Client_notification::get_op_type() const { return rep().op_type; }

inline C_Client_notification_rep& C_Client_notification::rep() const {
	th_assert(ALIGNED(msg), "Improperly aligned pointer");
	return *((C_Client_notification_rep*) msg);
}

#endif // _C_Client_notification_h
