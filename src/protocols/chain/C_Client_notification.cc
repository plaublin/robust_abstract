#include <string.h>
#include "th_assert.h"
#include "C_Message_tags.h"
#include "C_Client_notification.h"

C_Client_notification::C_Client_notification() :
	C_Message(C_Client_notification_tag, sizeof(C_Client_notification_rep))
{
}

C_Client_notification::C_Client_notification(short op, int cid, Request_id rid) :
	C_Message(C_Client_notification_tag, sizeof(C_Client_notification_rep))
{
	rep().op_type = op;
	rep().cid = cid;
	rep().rid = rid;
}

C_Client_notification::~C_Client_notification()
{
}

bool C_Client_notification::convert(C_Message *m1, C_Client_notification *&m2)
{
	if (!m1->has_tag(C_Client_notification_tag, sizeof(C_Client_notification_rep)))
	{
		return false;
	}

	m2 = new C_Client_notification();
	memcpy(m2->contents(), m1->contents(), m1->size());
	delete m1;
	m2->trim();
	return true;
}
