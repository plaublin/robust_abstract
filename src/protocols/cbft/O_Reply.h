#ifndef _Q_Reply_h
#define _Q_Reply_h 1

#include "types.h"
#include "libbyz.h"
#include "O_Message.h"
#include "Digest.h"

class O_Principal;
class O_Rep_info;

//
// O_Reply messages have the following format.
//
struct O_Reply_rep : public O_Message_rep
{
		//Added by Maysam Yabandeh
		int order;

		View v; // current view
		Request_id rid; // unique request identifier
		Digest rh_digest; // digest of reply.
		Digest digest; // digest of reply.
		int replica; // id of replica sending the reply
		int reply_size; // if negative, reply is not full.
		int cid;
		bool should_switch; // if true, should switch to protocol with instance_id
		enum protocols_e instance_id; // id of the instance to which should switch
		int unused;
		// Followed by a reply that is "reply_size" bytes long and
		// a MAC authenticating the reply to the client. The MAC is computed
		// only over the Reply_rep. Replies can be empty or full. Full replies
		// contain the actual reply and have reply_size >= 0. Empty replies
		// do not contain the actual reply and have reply_size < 0.
};

class O_Reply : public O_Message
{
		//
		// O_Reply messages
		//
	public:
		O_Reply();

		O_Reply(O_Reply_rep *r);

		O_Reply(View view, Request_id req, int replica, Digest &d,
		      O_Principal *p, int cid);

		virtual ~O_Reply();

		void authenticate(O_Principal *p, int act_len);
		// Effects: Terminates the construction of a reply message by
		// setting the length of the reply to "act_len", appending a MAC,
		// and trimming any surplus storage.

		View view() const;
		// Effects: Fetches the view from the message

		Request_id request_id() const;
		// Effects: Fetches the request identifier from the message.

		int cid() const;
		// Effects: Fetches the identifier of the client who issued the request this message is in response to.

		Digest &digest() const;
		// Effects: Fetches the digest from the message.

		Digest &request_history_digest() const;
		// Effects : Fetches the request-history digest from the message

		int& reply_size();
		// Return the reply size

		bool full() const;
		// Effects: Returns true iff "this" is a full reply.

		int id() const;
		// Effects: Fetches the reply identifier from the message.

		void set_id(int id);
		// Effects: Sets the reply identifier to id.

		bool should_switch() const;
		// Effects: returns true if reply was notifying about switching

		enum protocols_e next_instance_id() const;
		// Effects: if should_switch == true, instance_id of the next abstract

		void set_instance_id(enum protocols_e nextp);
		// Effects: sets should_switch = true, and instance_id

		bool verify();
		// Effects: Verifies if the message is authenticated by rep().replica.

		bool match(O_Reply *r);
		// Effects: Returns true if the replies match.

		static bool convert(O_Message *m1, O_Reply *&m2);
		// Effects: If "m1" has the right size and tag of a "O_Reply", casts
		// "m1" to a "O_Reply" pointer, returns the pointer in "m2" and
		// returns true. Otherwise, it returns false. Convert also trims any
		// surplus storage from "m1" when the conversion is successfull.

		char *reply(int &len);

		char *store_reply(int &max_len);
		   // Effects: Returns a pointer to the location within the message
		   // where the reply should be stored and sets "max_len" to the number of
		   // bytes available to store the reply. The caller can copy any reply
		   // with length less than "max_len" into the returned buffer.
		
		//Added by Maysam Yabandeh
		void setOrder(int);
		int getOrder();

	private:
		O_Reply_rep &rep() const;
		// Effects: Casts "msg" to a O_Reply_rep&

		friend class O_Rep_info;
		//Added by Maysam Yabandeh
		//TODO: Clean this dirty imprivization
		friend class O_Replica;
};

inline O_Reply_rep& O_Reply::rep() const
{
	th_assert(ALIGNED(msg), "Improperly aligned pointer");
	return *((O_Reply_rep*)msg);
}

inline View O_Reply::view() const
{
	return rep().v;
}

inline Request_id O_Reply::request_id() const
{
	return rep().rid;
}

inline int O_Reply::id() const
{
	return rep().replica;
}
inline void O_Reply::set_id(int id)
{
	rep().replica = id;
}
inline int O_Reply::cid() const
{
	return rep().cid;
}

inline Digest& O_Reply::digest() const
{
	return rep().digest;
}

inline Digest& O_Reply::request_history_digest() const
{
	return rep().rh_digest;
}

inline int& O_Reply::reply_size()
{
	return rep().reply_size;
}

inline bool O_Reply::full() const
{
	return rep().reply_size >= 0;
}

inline bool O_Reply::should_switch() const
{
	return rep().should_switch;
}

inline enum protocols_e O_Reply::next_instance_id() const
{
	return rep().instance_id;
}

inline void O_Reply::set_instance_id(enum protocols_e nextp)
{
	rep().should_switch = true;
	rep().instance_id = nextp;
}

//Added by Maysam Yabandeh
inline void O_Reply::setOrder(int _order) 
{
	rep().order = _order;
}
inline int O_Reply::getOrder()
{
	return rep().order;
}

inline bool O_Reply::match(O_Reply *r)
{
	 //Digest &d1 = digest();
	 //Digest &d2 = r->digest();
	 //Digest &d3 = request_history_digest();
	 //Digest &d4 = r->request_history_digest();
	// TODO this is an ACK that is temporary because empty messages do not have a request_history_digest
	// TODO reproduce the bug using reply size = 4096
	if ((r->reply_size() < 0) || (reply_size() < 0))
	{
		return true;
	}

	bool toRet = ( (request_history_digest() == r->request_history_digest())
			&& (rep().v == r->rep().v) && (rep().cid == r->rep().cid) && (rep().rid == r->rep().rid) && ((r->reply_size() < 0) || (reply_size() < 0) || (digest()
			== r->digest())));
	 //fprintf(stderr, "Reply::match [%u,%u,%u,%u]==[%u,%u,%u,%u] && [%u,%u,%u,%u]==[%u,%u,%u,%u]\n", d1.udigest()[0], d1.udigest()[1], d1.udigest()[2], d1.udigest()[3]
	 //, d2.udigest()[0], d2.udigest()[1], d2.udigest()[2], d2.udigest()[3]
	 //, d3.udigest()[0], d3.udigest()[1], d3.udigest()[2], d3.udigest()[3]
	 //, d4.udigest()[0], d4.udigest()[1], d4.udigest()[2], d4.udigest()[3]);
	if (toRet)
	{
		//      fprintf(stderr, "reply_match returning true\n");
		return true;
	}
	//   fprintf(stderr, "reply_match returning false\n");
	return false;
}


//Added by Maysam Yabandeh
//Ack for receiving the request for order o in a non-primary replica
class O_Ack : public O_Message
{
	public:
		int order;
		int id;
		O_Ack() {}
		O_Ack(int _order, int _id) {order=_order;id=_id;}
};
#endif // _Reply_h
