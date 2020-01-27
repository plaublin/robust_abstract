#include <stdlib.h>
#include <strings.h>
#include "th_assert.h"
#include "R_Message_tags.h"
#include "R_BaseRequest.h"
#include "R_Node.h"
#include "R_Principal.h"
#include "MD5.h"

#include "R_Request.h"
#include "R_ACK.h"

#include "R_Rep_info.h"

// extra & 1 = read only --> NOT USED YET IN OUR IMPLEMENTATION
// extra & 4 = INIT request

R_BaseRequest::R_BaseRequest(unsigned sz) :
	R_Message(sz)
{
}

R_BaseRequest::R_BaseRequest(int t, unsigned sz) :
	R_Message(t, sz)
{
}

R_BaseRequest::R_BaseRequest(R_Message_rep *rep)
    : R_Message(rep)
{
}

R_BaseRequest::~R_BaseRequest()
{
}

inline void R_BaseRequest::set_digest(Digest& d)
{
	//  fprintf(stderr, "Computing MD5 digest (inputLen = %d)\n", sizeof(int)
	//        +sizeof(Request_id)+ rep().command_size);INCR_OP(num_digests);START_CC(digest_cycles);
	rep().od = d;
}
