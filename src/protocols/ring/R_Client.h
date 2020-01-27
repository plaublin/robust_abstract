#ifndef _R_Client_h
#define _R_Client_h 1

#include <pthread.h>

#include <stdio.h>
#include "types.h"
#include "R_Node.h"
#include "R_Request.h"
#include "R_ITimer.h"

extern void R_rtime_handler();

extern "C"
{
#include "TCP_network.h"
}

class R_Reply;
class R_Request;

class R_Client : public R_Node
{
	public:
		R_Client(FILE *config_file, FILE *config_priv, char *host_name, short port=0);
		// Effects: Creates a new R_Client object using the information in
		// "config_file". The line of config assigned to
		// this client is the first one with the right host address (if
		// port==0) or the first with the right host address and port equal
		// to "port".

		virtual ~R_Client();
		// Effects: Deallocates all storage associated with this.

		void close_connections();

		bool send_request(R_Request *req, int size, bool ro);
		// Effects: Sends request m to the service. Returns FALSE iff two
		// consecutive request were made without waiting for a reply between
		// them.

		R_Reply *recv_reply();
		// Effects: Blocks until it receives enough reply messages for
		// the previous request. returns a pointer to the reply. The caller is
		// responsible for deallocating the request and reply messages.

		Request_id get_rid() const;
		// Effects: Returns the current outstanding request identifier. The request
		// identifier is updated to a new value when the previous message is
		// delivered to the user.

		void retransmit();
		// Effects: Retransmits any outstanding request

		int entry_replica_id() const;
		// Effects: returns the id of the entry replica, ie. the replica to which requests are sent.

		int nb_received_requests;
		R_Node *node;
		int timedout;

		// Threads
		pthread_t receive_resp_thread_id;
		pthread_t replies_handler_thread;
		pthread_t itimer_thread;
		void replies_handler();

		int socket_to_my_primary;
		int socket_to_my_last;
		// Each node will connect to primary which has the id equal to
		// client id % n

		void set_malicious();
		bool is_malicious() const;

	protected:
		Request_id out_rid; // Identifier of the outstanding request
		R_Request *out_req; // Outstanding request
		bool servicing_read_only;

		int max_socket_num;
	private:
		pthread_mutex_t outgoing_req_mutex;

		int n_retrans;        // Number of retransmissions of out_req
		int rtimeout;         // PBFT_R_Timeout period in msecs
		R_ITimer *rtimer;       // Retransmission timer
		int entry_replica;      // id of the entry replica

		bool malicious; // true if client should act maliciously

#ifdef ADAPTIVE_RTIMEOUT
		// Maximum retransmission timeout in msecs
		static const int Max_rtimeout = 100;
		// Minimum retransmission timeout after retransmission in msecs
		static const int Min_rtimeout = 2;

		struct timeval ad_start;
		struct timeval ad_stop;

		double rtimeout_avg;
		size_t seen_reqs;
#endif
};

inline Request_id R_Client::get_rid() const
{
	return out_rid;
}

inline int R_Client::entry_replica_id() const
{
    return entry_replica;
}

inline void R_Client::set_malicious()
{
    malicious = true;
}

inline bool R_Client::is_malicious() const
{
    return malicious;
}
#endif // _Client_h
