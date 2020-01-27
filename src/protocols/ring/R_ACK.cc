#include <stdlib.h>
#include <strings.h>
#include "th_assert.h"
#include "R_Message_tags.h"
#include "R_ACK.h"
#include "R_Node.h"
#include "R_Principal.h"
#include "R_BaseRequest.h"
#include "R_Request.h"
#include "MD5.h"

#include "R_Rep_info.h"

// extra & 1 = read only --> NOT USED YET IN OUR IMPLEMENTATION
// extra & 4 = INIT request

R_ACK::R_ACK() :
	R_BaseRequest(R_ACK_tag, R_Max_message_size)
{
	just_created = false;
}

R_ACK::R_ACK(int cid, int replica, Seqno& seqno, Request_id& r, Digest& d) :
	R_BaseRequest(R_ACK_tag, R_Max_message_size)
{
	rep().cid = cid;
	rep().replica_id = replica;
	rep().rid = r;
	rep().seqno = seqno;
	rep().od = d;
	rep().pb_index = rep().pb_offset = 0;
	set_size(sizeof(R_ACK_rep)+R_node->auth_size());
	just_created = false;
}

R_ACK::R_ACK(R_ACK_rep *rep) :
	R_BaseRequest((R_Message_rep*)rep)
{
	set_size(sizeof(R_ACK_rep)+R_node->auth_size());
	just_created = false;
}

R_ACK::~R_ACK()
{
}

bool R_ACK::convert(R_Message *m1, R_ACK *&m2)
{

	if (!m1->has_tag(R_ACK_tag, sizeof(R_ACK_rep)))
	{
		return false;
	}

	m2 = new R_ACK();
	memcpy(m2->contents(), m1->contents(), m1->size());
	delete m1;
	m2->trim();
	return true;
}

void R_ACK::authenticate(R_Rep_info *replies)
{
	R_node->gen_auth(this, replies);
}

void R_ACK::take_authenticators(R_Request *r)
{
    memcpy(toauth_pos(), r->toauth_pos(), R_node->auth_size());
}

bool R_ACK::verify(R_Request *r)
{
	Digest d;
	r->comp_digest(d);

	if (d != rep().od)
	{
		fprintf(stderr, "Digest is different\n");
		return false;
	}

	return R_node->verify_auth(this);
}


