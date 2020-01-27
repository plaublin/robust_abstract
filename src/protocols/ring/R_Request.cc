#include <stdlib.h>
#include <strings.h>
#include "th_assert.h"
#include "R_Message_tags.h"
#include "R_Request.h"
#include "R_Node.h"
#include "R_Principal.h"
#include "R_ACK.h"

#include "MD5.h"

// extra & 1 = read only --> NOT USED YET IN OUR IMPLEMENTATION
// extra & 4 = INIT request

R_Request::R_Request(int sz) :
	R_BaseRequest(R_Request_tag, sz)
{
}

R_Request::R_Request(Request_id r) :
	R_BaseRequest(R_Request_tag, R_Max_message_size)
{
	rep().cid = R_node->id();
	rep().rid = r;
	rep().command_size = 0;
	rep().seqno = 0;
	rep().type = 0;
	rep().pb_index = rep().pb_offset = 0;

	// rep().is_init = 0;
	set_size(sizeof(R_Request_rep));
}

R_Request::R_Request(R_Request_rep *rep)
    : R_BaseRequest((R_Message_rep*)rep)
{
}

R_Request::~R_Request()
{
}

char *R_Request::store_command(int &max_len)
{
	max_len = msize()-sizeof(R_Request_rep)-R_node->auth_size();
	return contents()+sizeof(R_Request_rep);
}

// comp_digest will allways generate digest for request as if seqno=0
void R_Request::comp_digest(Digest& d)
{
	//  fprintf(stderr, "Computing MD5 digest (inputLen = %d)\n", sizeof(int)
	//        +sizeof(Request_id)+ rep().command_size);INCR_OP(num_digests);START_CC(digest_cycles);
	MD5_CTX context;
	MD5Init(&context);
	MD5Update(&context, (char*)(&(rep().cid)),
		sizeof(int) + sizeof(Request_id)+sizeof(int)+sizeof(short)+ rep().command_size);
	MD5Final(d.udigest(), &context);
}

void R_Request::finalize(int act_len, bool read_only)
{
	th_assert((unsigned)act_len <= msize() - sizeof(R_Request_rep)
			- R_node->auth_size(), "Invalid request size");

	//  rep().extra = ((read_only) ? 1 : 0);
	rep().extra &= ~1;
	if (read_only)
	{
		rep().extra = rep().extra | 1;
	}
	rep().command_size = act_len;
	comp_digest(rep().od);

	set_size(sizeof(R_Request_rep) + act_len + R_node->auth_size());
}

void R_Request::authenticate(R_Rep_info *replies)
{
	R_node->gen_auth(this, replies);
}

bool R_Request::verify()
{
	Digest d;
	comp_digest(d);

	if (d != rep().od)
	{
		fprintf(stderr, "Digest is different\n");
		return false;
	}

	if (is_read_only())
	    return R_node->verify_auth_ro(this);

	return R_node->verify_auth(this);
}

bool R_Request::convert(R_Message *m1, R_Request *&m2)
{

	if (!m1->has_tag(R_Request_tag, sizeof(R_Request_rep)))
	{
		return false;
	}

	// m2 = (R_Request*)m1;
	//  m2 = new R_Request((R_Request_rep *)m1->contents());
	//   m2->trim();
	m2 = new R_Request(m1->size());
	memcpy(m2->contents(), m1->contents(), m1->size());
	//free(m1->contents());
	delete m1;
	m2->trim();
	return true;
}

char * const R_Request::tosign_pos() const {
    const int f = R_node->f();
    return contents()
	+ sizeof(R_Request_rep)+rep().command_size
	+ ALIGNED_SIZE(R_UMAC_size * ((f + 1) * (f + 1))
		+ R_UNonce_size * (f + 1));
}
