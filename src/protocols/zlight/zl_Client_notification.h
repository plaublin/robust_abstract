#ifndef _zl_Client_notification_h
#define _zl_Client_notification_h 1

#include "zl_Message.h"
#include "types.h"

const int zl_Client_notification_request = 0;
const int zl_Client_notification_reply = 1;

//
// zl_Client_notification messages have the following format.
//
struct zl_Client_notification_rep: public zl_Message_rep {
   short op_type; // is this for a request or a reply?
	int cid; // id of the client
	Request_id rid; // id of the request
};

class zl_Client_notification: public zl_Message {
public:
	zl_Client_notification();
	zl_Client_notification(short op, int cid, Request_id rid);
	~zl_Client_notification();

	int get_cid() const;
	Request_id get_rid() const;
	short get_op_type() const;

	static bool convert(zl_Message *m1, zl_Client_notification *&m2);
	// Effects: If "m1" has the right size and tag of a "zl_Client_notification",
	// casts "m1" to a "zl_Client_notification" pointer, returns the pointer in
	// "m2" and returns true. Otherwise, it returns false.

private:
	zl_Client_notification_rep &rep() const;
	// Effects: Casts "msg" to a zl_Client_notification_rep&
};

inline int zl_Client_notification::get_cid() const { return rep().cid; }
inline Request_id zl_Client_notification::get_rid() const { return rep().rid; }
inline short zl_Client_notification::get_op_type() const { return rep().op_type; }

inline zl_Client_notification_rep& zl_Client_notification::rep() const {
	th_assert(ALIGNED(msg), "Improperly aligned pointer");
	return *((zl_Client_notification_rep*) msg);
}

#endif // _zl_Client_notification_h
