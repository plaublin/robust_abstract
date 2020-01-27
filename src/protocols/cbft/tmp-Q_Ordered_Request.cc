//Added by Maysam Yabandeh
#include <stdlib.h>
#include <strings.h>
#include "th_assert.h"
#include "O_Message_tags.h"
#include "O_Ordered_Request.h"
#include "O_Node.h"
#include "O_Principal.h"
#include "MD5.h"

// extra & 1 = read only

O_Ordered_Request::O_Ordered_Request(O_Ordered_Request_rep *r) :
    O_Message(r) {}

O_Ordered_Request::O_Ordered_Request(Request_id r, short rr) :
	O_Message(O_Request_tag, O_Max_message_size) {
	rep().cid = O_node->id();
	rep().rid = r;
	rep().replier = rr;
	rep().command_size = 0;
	rep().unused = 0;
	set_size(sizeof(O_Ordered_Request_rep));
}

//Added by Maysam Yabandeh
O_Ordered_Request::O_Ordered_Request(O_Request *r) :
	O_Message(O_Request_tag, O_Max_message_size) {
	rep().od = r->rep().od;
	rep().replier = r->rep().replier;
	rep().command_size = r->rep().command_size;
	rep().cid = r->rep().cid;
	rep().rid = r->rep().rid;
	rep().unused = r->rep().unused;
	set_size(sizeof(O_Ordered_Request_rep));
}

O_Ordered_Request::~O_Ordered_Request() {
}

char *O_Ordered_Request::store_command(int &max_len) {
	max_len = msize() - sizeof(O_Ordered_Request_rep) - O_node->auth_size();
	return contents() + sizeof(O_Ordered_Request_rep);
}

inline void O_Ordered_Request::comp_digest(Digest& d) {
	MD5_CTX context;
	MD5Init(&context);
	MD5Update(&context, (char*) &(rep().cid), sizeof(int) + sizeof(Request_id)
			+ rep().command_size);
	MD5Final(d.udigest(), &context);
}

void O_Ordered_Request::authenticate(int act_len, bool read_only) {
	th_assert((unsigned)act_len <= msize() - sizeof(O_Ordered_Request_rep)
			- O_node->auth_size(), "Invalid request size");

	// rep().extra = ((read_only) ? 1 : 0);
	rep().extra &= ~1;
	if (read_only) {
		rep().extra = rep().extra | 1;
	}

	rep().command_size = act_len;
	if (rep().replier == -1) {
		rep().replier = lrand48() % O_node->n();
	}
	comp_digest(rep().od);

	int old_size = sizeof(O_Ordered_Request_rep) + act_len;
	set_size(old_size + O_node->auth_size());
	O_node->gen_auth(contents(), sizeof(O_Ordered_Request_rep), contents() + old_size);
}

bool O_Ordered_Request::verify() {
	const int nid = O_node->id();
	const int cid = client_id();
	const int old_size = sizeof(O_Ordered_Request_rep) + rep().command_size;
	O_Principal* p = O_node->i_to_p(cid);
	Digest d;

	comp_digest(d);
	if (p != 0 && d == rep().od) {
		// Verifying the authenticator.
		if (cid != nid && cid >= O_node->n() && size() - old_size
				>= O_node->auth_size(cid)) {
			return O_node->verify_auth(cid, contents(), sizeof(O_Ordered_Request_rep),
					contents() + old_size);
		}
	}
	return false;
}

bool O_Ordered_Request::convert(O_Message *m1, O_Ordered_Request *&m2) {
	if (!m1->has_tag(O_Request_tag, sizeof(O_Ordered_Request_rep))) {
		return false;
	}

	m2 = new O_Ordered_Request(true);
	memcpy(m2->contents(), m1->contents(), m1->size());
	delete m1;
	m2->trim();
	return true;
}

inline O_Ordered_Request_rep& O_Ordered_Request::rep() const {
	th_assert(ALIGNED(msg), "Improperly aligned pointer");
	return *((O_Ordered_Request_rep*) msg);
}

void O_Ordered_Request::display() const {
	fprintf(stderr, "Ordered_Request display: (client_id = %d)\n", rep().cid);
}
