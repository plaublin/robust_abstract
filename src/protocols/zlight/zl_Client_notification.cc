#include <string.h>
#include "th_assert.h"
#include "zl_Message_tags.h"
#include "zl_Client_notification.h"

zl_Client_notification::zl_Client_notification() :
	zl_Message(zl_Client_notification_tag, sizeof(zl_Client_notification_rep))
{
}

zl_Client_notification::zl_Client_notification(short op, int cid, Request_id rid) :
	zl_Message(zl_Client_notification_tag, sizeof(zl_Client_notification_rep))
{
	rep().op_type = op;
	rep().cid = cid;
	rep().rid = rid;
}

zl_Client_notification::~zl_Client_notification()
{
}

bool zl_Client_notification::convert(zl_Message *m1, zl_Client_notification *&m2)
{
	if (!m1->has_tag(zl_Client_notification_tag, sizeof(zl_Client_notification_rep)))
	{
		return false;
	}

	m2 = new zl_Client_notification();
	memcpy(m2->contents(), m1->contents(), m1->size());
	delete m1;
	m2->trim();
	return true;
}
