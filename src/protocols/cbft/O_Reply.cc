#include <strings.h>
#include "th_assert.h"
#include "O_Message_tags.h"
#include "O_Reply.h"
#include "O_Node.h"
#include "O_Principal.h"

O_Reply::O_Reply() :
	O_Message(O_Reply_tag, O_Max_message_size)
{
}

O_Reply::O_Reply(View view, Request_id req, int replica, Digest &d,
		O_Principal *p, int cid) :
	O_Message(O_Reply_tag, sizeof(O_Reply_rep)+O_MAC_size)
{
	rep().v = view;
	rep().rid = req;
	rep().cid = cid;
	rep().replica = replica;
	rep().reply_size = -1;
	rep().digest = d;
	rep().rh_digest.zero();
	rep().should_switch = false;
	rep().instance_id = O_node->instance_id();

	O_my_principal->gen_mac(contents(), sizeof(O_Reply_rep), contents()
			+sizeof(O_Reply_rep), p->pid());
}

O_Reply::O_Reply(O_Reply_rep *r) :
	O_Message(r)
{
}

O_Reply::~O_Reply()
{
}

char* O_Reply::reply(int &len)
{
	len = rep().reply_size;
	return contents()+sizeof(O_Reply_rep);
}

char *O_Reply::store_reply(int &max_len)
{
	max_len = msize()-sizeof(O_Reply_rep)-O_MAC_size;

	return contents()+sizeof(O_Reply_rep);
}

void O_Reply::authenticate(O_Principal *p, int act_len)
{
	th_assert((unsigned)act_len <= msize()-sizeof(O_Reply_rep)-O_MAC_size,
			"Invalid reply size");

	rep().reply_size = act_len;
	rep().digest = Digest(contents()+sizeof(O_Reply_rep), act_len);
	int old_size = sizeof(O_Reply_rep)+act_len;
	set_size(old_size+O_MAC_size);

	O_my_principal->gen_mac(contents(), sizeof(O_Reply_rep), contents()
			+old_size, p->pid());

	trim();
}

bool O_Reply::verify()
{
	// Replies must be sent by replicas.
	if (!O_node->is_replica(id()))
	{
		fprintf(stderr, "Does not come from a replica\n");
		return false;
	}

	// Check sizes
	int rep_size = (full()) ? rep().reply_size : 0;
	if (size()-(int)sizeof(O_Reply_rep)-rep_size < O_MAC_size)
	{
		fprintf(stderr, "Its bigger than MAC\n");
		return false;
	}

	// Check reply
	if (full())
	{
		Digest d(contents()+sizeof(O_Reply_rep), rep_size);
		if (d != rep().digest)
		{
			fprintf(stderr, "Digest is not equal...\n");
			return false;
		}
	}

	// Check MAC
	O_Principal *sender = O_node->i_to_p(id());
	int size_wo_MAC = sizeof(O_Reply_rep)+rep_size;

	return sender->verify_mac(contents(), sizeof(O_Reply_rep), contents()
			+size_wo_MAC);
}

bool O_Reply::convert(O_Message *m1, O_Reply *&m2)
{
	if (!m1->has_tag(O_Reply_tag, sizeof(O_Reply_rep)))
	{
		return false;
	}
	m2= new O_Reply();
	memcpy(m2->contents(), m1->contents(), m1->size());
	delete m1;
	m2->trim();
	return true;
}
