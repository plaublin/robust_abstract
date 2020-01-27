#include <strings.h>
#include "th_assert.h"
#include "C_Message_tags.h"
#include "C_Reply.h"
#include "C_Node.h"
#include "C_Principal.h"

C_Reply::C_Reply() :
	C_Message(C_Reply_tag, C_Max_message_size)
{
}

C_Reply::C_Reply(C_Reply_rep *r) :
	C_Message(r)
{
}

C_Reply::C_Reply(View view, Request_id req, int replica, Digest &d,
    C_Principal *p, int cid) :
  C_Message(C_Reply_tag, sizeof(C_Reply_rep)+C_MAC_size)
{
  rep().v = view;
  rep().rid = req;
  rep().cid = cid;
  rep().replica = replica;
  rep().reply_size = -1;
  rep().digest = d;
  rep().rh_digest.zero();
  rep().should_switch = false;
  rep().instance_id = C_node->instance_id();
}

C_Reply::~C_Reply()
{
}

char *C_Reply::store_reply(int &max_len)
{
	max_len = msize()-sizeof(C_Reply_rep)- (C_node->f() + 1) * (C_UNonce_size
			+ C_UMAC_size);

	return contents()+sizeof(C_Reply_rep);
}

bool C_Reply::verify()
{
	// Replies must be sent by replicas.
	if (!C_node->is_replica(id()))
	{
		return false;
	}

	// Check sizes
	int rep_size = rep().reply_size;
	if (size()-(int)sizeof(C_Reply_rep)-rep_size < (C_node->f() + 1)
			* (C_UNonce_size + C_UMAC_size))
	{
		return false;
	}

	// Check reply
	Digest d(contents()+sizeof(C_Reply_rep), rep_size);
	if (d != rep().digest)
	{
		fprintf(stderr, "C_Reply::verify: Digest does not match\n");
		return false;
	}

	// Check signature.
	int size_wo_MAC = sizeof(C_Reply_rep) + rep_size;

	int unused=0;
	bool ret = C_node->verify_auth(C_node->n(), id(), contents(),
			sizeof(C_Reply_rep), contents() +size_wo_MAC, &unused);

	//TODO these if condition must be removed
	if (rep().cid !=-1)
	{
		return true;
	}
	//return ret;
	return true;
}

void C_Reply::authenticate(C_Principal *p, int act_len)
{
   th_assert((unsigned)act_len <= msize()-sizeof(C_Reply_rep)-C_MAC_size,
         "Invalid reply size");

   rep().reply_size = act_len;
   rep().digest = Digest(contents()+sizeof(C_Reply_rep), act_len);
   int old_size = sizeof(C_Reply_rep)+act_len;
   set_size(old_size+C_MAC_size);

   long long unonce = C_Principal::new_umac_nonce();

   C_my_principal->gen_mac(contents(), sizeof(C_Reply_rep), contents()
         +old_size, p->pid(), (char*)&unonce);

   trim();
}

bool C_Reply::convert(C_Message *m1, C_Reply *&m2)
{
	if (!m1->has_tag(C_Reply_tag, sizeof(C_Reply_rep)))
	{
		return false;
	}
	//  m1->trim();
	// m2 = (C_Reply*)m1;
	// m2 = new C_Reply((C_Reply_rep *) m1->contents());
	m2=new C_Reply();
	memcpy(m2->contents(), m1->contents(), m1->size());
	//free(m1->contents());
	delete m1;
	m2->trim();
	return true;
}

void C_Reply::reset_switch()
{
  rep().should_switch = false;
  rep().instance_id = C_node->instance_id();
}

