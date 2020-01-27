#ifndef _R_Replica_h
#define _R_Replica_h 1

#include <pthread.h>
#include <sys/epoll.h>
#include <poll.h>

#include <google/sparse_hash_map>
#include <google/dense_hash_map>

using google::dense_hash_map;

#include <map>
#include <list>
#include <queue>
#include <vector>

#include "R_Cryptography.h"
#include "types.h"
#include "R_Rep_info.h"
#include "Digest.h"
#include "R_Node.h"
#include "libbyz.h"
#include "Set.h"
#include "Array.h"

#include "R_Types.h"
#include "R_Request_history.h"
#include "R_Checkpoint.h"

#include "SuperFastHash.h"

class R_Request;
class R_ACK;

#define MAX_CONNECTIONS 4096
#define ALIGNMENT_BYTES 2

enum Execution { execution_ok = 0, execution_wait = 1, execution_old = 2 }; // wait means still not executed, old means request has been executed previously. ok means it was executed now.

typedef std::list<R_Message_State> R_Message_List;
//typedef google::dense_hash_map<R_Message_Id, R_Message_State, SuperFastHasher<R_Message_Id> > R_Message_Map;
typedef std::map<R_Message_Id, R_Message_State> R_Message_Map;
typedef std::pair<Seqno, R_Message_Id> R_Exec_Context;

class mycomparison
{
    public:
    bool operator() (const R_Exec_Context& lhs, const R_Exec_Context& rhs) const
    {
	return (lhs.first>rhs.first);
    }
};

typedef std::priority_queue<R_Exec_Context, std::vector<R_Exec_Context>, mycomparison> R_Exec_Queue;

typedef Set<R_Checkpoint> R_CheckpointSet;

class R_Checkpoint_Store {
    public:
	R_Checkpoint_Store() : by_chk_id(), by_set(), size(0), lower(0), upper(0), last_removed(0) {};
	~R_Checkpoint_Store() { for (int i=lower; i<upper; i++) delete by_set[i]; };

	void add(Seqno seq, R_CheckpointSet *set) {
	    by_chk_id.append(seq);
	    by_set.append(set);
	    size++;
	    upper++;
	};

	bool find(Seqno seq, R_CheckpointSet** set) {
	    int pos = lower;
	    *set = NULL;
	    while (pos < upper) {
		if (by_chk_id[pos] == seq) {
		    *set = by_set[pos];
		    return true;
		}
		pos++;
	    }
	    return false;
	};

	void remove(Seqno seq) {
	    int pos = lower;
	    while (pos < upper) {
		if (by_chk_id[pos] == seq) {
		    last_removed = seq;
		    size -= (pos-lower+1);
		    int oldlower = lower;
		    lower = pos+1;
		    if (lower > TRUNCATE_SIZE) {
			for (int i=oldlower; i<lower; i++)
			    if (by_set[i] != NULL)
				delete by_set[i];

			// need to truncate the file
			for (int i=0; i<size; i++) {
			    by_chk_id[i] = by_chk_id[i+lower];
			    by_set[i] = by_set[i+lower];
			}
			upper -= lower;
			lower = 0;
			by_chk_id.enlarge_to(size);
			by_set.enlarge_to(size);
		    }
		    return;
		}
		pos++;
	    }
	}

	Seqno last() const { return last_removed; }

    private:
	Array<Seqno> by_chk_id;
	Array<R_CheckpointSet*> by_set;
	int size;
	int lower;
	int upper;
	Seqno last_removed;
	static const int TRUNCATE_SIZE = 16;
};

class R_Replica : public R_Node
{
	public:

		R_Replica(FILE *config_file, FILE *config_priv, char* host_name,short port=0);

		// Effects: Create a new server replica using the information in
		// "config_file". The replica's state is set has
		// a total of "num_objs" objects. The abstraction function is
		// "get_segment" and its inverse is "put_segments". The procedures
		// invoked before and after recovery to save and restore extra
		// state information are "shutdown_proc" and "restart_proc".

		virtual ~R_Replica();
		// Effects: Kill server replica and deallocate associated storage.

		// Methods to register service specific functions. The expected
		// specifications for the functions are defined below.
		//void register_exec(int (*e)(Byz_req *, Byz_rep *, Byz_buffer *, int, bool));
		// Effects: Registers "e" as the exec_command function.

		// Methods to register service specific functions. The expected
		// specifications for the functions are defined below.
		void register_exec(int (*e)(Byz_req *, Byz_rep *, Byz_buffer *, int, bool));
		// Effects: Registers "e" as the exec_command function.

		Execution execute_request(R_Request *req);
		void execute_request_main(R_Request *req);
		void send_reply_to_client(R_BaseRequest *req);

		void do_recv_from_queue();
		//   virtual void recv();
		// Effects: Loops receiving messages and calling the appropriate
		// handlers.

		void request_queue_handler();
		void requests_from_predecessor_handler();
		void c_client_requests_handler_epoll();
#ifdef USE_POLL_EVENT_LOOP
		void c_client_requests_handler_poll();
#endif
#ifdef USE_SELECT_EVENT_LOOP
		void c_client_requests_handler_select();
#endif
		void handle_new_connection();

		bool get_request_state(R_Message_Id rmid, R_Message_State &rms) const;

		template <class T> inline void gen_handle(R_Message *m)
		{
			T *n;
			if (T::convert(m, n))
			{
				handle(n);
			} else
			{
				delete m;
			}
		}

	private:
		friend class R_Req_history_log;

		void handle(R_Message* m);
		void handle(R_Request* m);
		void handle(R_ACK * m);
		void handle(R_Checkpoint* m);

		bool upper_handle(R_Request **oreq);
		bool transform_handle(R_Request *req);
		bool transform_handle(R_ACK *ack);
		bool forward_handle(R_Request *req);
		bool forward_handle(R_ACK *ack);

		void assign_sequence(R_Request *req, bool iterate=true);

		bool has_request_state(R_BaseRequest *req) const;
		bool get_request_state(R_BaseRequest *ack, R_Message_State &rms) const;
		void create_state_for_request(R_Request *req);
		void split_and_install_requests(R_Message_Id rmid, R_Request *nreq);

		R_Request* get_new_client_request();
		void get_and_batch_requests(R_Request **oreq);
		Execution do_execute(R_Request *areq);
		bool process_piggybacked(R_ACK *ack, int *status);
		void rms_remove(R_Message_State rms);
		Seqno get_seqno_for_request(R_Request *req);
		void neuter_older_requests(R_Request *req);

		Seqno seqno; // Sequence number to attribute to next protocol message,
			     // we'll use this counter to index messages in history 
		Seqno last_executed;

		// Last replies sent to each principal.
		R_Rep_info replies;

		int in_socket;
		int in_socket_for_clients;
		int out_socket;
		int all_replicas_socket;
		int epoll_fd;

		// Threads
		pthread_t request_queue_handler_thread;
		pthread_t requests_from_predecessor_handler_thread;
		pthread_t R_client_requests_handler_thread;
		pthread_t R_Replica_throughput_reporter_thread;
		pthread_t R_Replica_listener_thread;

		// Queues for holding messages from clients
		R_Mes_queue incoming_queue_clients;
		int incoming_queue_signaler[2];

		/* Array of connected sockets so we know who we are talking to */
		google::dense_hash_map<int,int> connectmap;
		google::dense_hash_map<int,struct epoll_event*> epoll_connectmap;
#ifdef USE_POLL_EVENT_LOOP
		google::dense_hash_map<int,struct pollfd*> poll_connectmap;
		struct pollfd pollfds[MAX_CONNECTIONS];
		size_t num_of_pollfds;
#endif
#ifdef USE_SELECT_EVENT_LOOP
		google::dense_hash_map<int,int> select_connectmap;
#endif

		// Pointers to the function to be executed when receiving a message
		int (*exec_command)(Byz_req *, Byz_rep *, Byz_buffer *, int, bool);

		// Logical vector clocks
		Array<Seqno> vector_clock;

		google::dense_hash_map<int,Request_id> latest_seen_rid;
		R_Message_Map	requests;

		// in this queue we'll keep the list of requests which are eligible
		// for execution. These requests are sequenced, yet we didn't have all
		// predecessors at the time
		R_Exec_Queue	execution_queue;
		void process_execution_queue();
		Execution process_execution_queue_helper(R_Request *areq);

		// request history
		R_Req_history_log rh;
		R_Checkpoint_Store checkpoint_store;
		void add_request_to_history(R_Message_Id rmid, R_BaseRequest *req);
};

// Pointer to global replica object.
extern R_Replica *R_replica;

#endif //_R_Replica_h
