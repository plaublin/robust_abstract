#ifndef _C_Replica_h
#define _C_Replica_h 1

#include <pthread.h>

#include <map>
#include <vector>

#include "C_Cryptography.h"
#include "types.h"
#include "C_Rep_info.h"
#include "Digest.h"
#include "C_Node.h"
#include "libbyz.h"
#include "C_Checkpoint.h"
#include "C_Abort_certificate.h"
#include "C_ITimer.h"

#include "Request_history.h"
#include "Switcher.h"
#include "AbortHistoryElement.h"

#define MAX_CONNECTIONS 1000
#define ALIGNMENT_BYTES 2

class C_Request;
class C_Panic;
class C_Abort;
class C_Missing;
class C_Get_a_grip;
class C_Client_notification;

class CheckpointSet {
   public:
      CheckpointSet(int sz, int c, Seqno seqno) {
         size = sz;
         complete = c;
         n = 0;
         s = seqno;
         elems = new C_Checkpoint*[size];
         for (int i=0; i<size; i++) {
            elems[i] = NULL;
         }
      }

      ~CheckpointSet() {
         for (int i=0; i<size; i++) {
            if (elems[i] != NULL) {
               delete elems[i];
            }
         }
         delete [] elems;
      }

      bool add(int i, C_Checkpoint *c) {
         th_assert(i>=0 && i<size, "add out of range");
         if (elems[i] != NULL || c->get_seqno() != s) {
            fprintf(stderr, "Cannot add checkpoint %lld to cset[%d]=%p for seqno %lld\n", c->get_seqno(), i, elems[i], s);
            return false;
         } else {
            elems[i] = c;
            n++;
            return true;
         }
      }

      C_Checkpoint* get(int i) {
         th_assert(i>=0 && i<size, "get out of range");
         return elems[i];
      }

      C_Checkpoint** fetch(int *sz) {
         *sz = size;
         return elems;
      }

      bool is_complete(void) {
        return (n == complete);
      }

      Seqno get_seqno(void) {
         return s;
      }

      int count(void) {
         return n;
      }

   private:
      C_Checkpoint** elems;
      int size;
      int n;
      Seqno s;
      int complete;
};

class C_Replica : public C_Node
{
   public:

		C_Replica(FILE *config_file, FILE *config_priv, char* host_name,short port=0);

		// Effects: Create a new server replica using the information in
		// "config_file". The replica's state is set has
		// a total of "num_objs" objects. The abstraction function is
		// "get_segment" and its inverse is "put_segments". The procedures
		// invoked before and after recovery to save and restore extra
		// state information are "shutdown_proc" and "restart_proc".

		virtual ~C_Replica();
		// Effects: Kill server replica and deallocate associated storage.

		// Methods to register service specific functions. The expected
		// specifications for the functions are defined below.
		//void register_exec(int (*e)(Byz_req *, Byz_rep *, Byz_buffer *, int, bool));
		// Effects: Registers "e" as the exec_command function.

		// Methods to register service specific functions. The expected
		// specifications for the functions are defined below.
		void register_exec(int (*e)(Byz_req *, Byz_rep *, Byz_buffer *, int, bool));
		// Effects: Registers "e" as the exec_command function.

		void do_recv_from_queue();
		//   virtual void recv();
		// Effects: Loops receiving messages and calling the appropriate
		// handlers.

		void requests_from_predecessor_handler();
		void c_client_requests_handler();
		void robust_handler();
      void handle(C_Message* m);

      void enable_replica(bool state);

      void no_checkpoint_timeout();
      // If there has not been a checkpoint for the last n secs, then
      // panic

      void no_request_timeout();
      // If there has been a request feedback but no request for the last n secs, then
      // panic

		template <class T> inline void gen_handle(C_Message *m)
		{
			T *n;
			if (T::convert(m, n))
			{
				handle(n);
			} else
			{
        fprintf(stderr, "Cannot convert message...\n");
				delete m;
			}
		}

      long long nb_sent_replies;
      int max_history_size;

      C_Mes_queue robust_messages_queue;
      pthread_mutex_t robust_message_queue_mutex;
      pthread_cond_t robust_message_queue_cond;

	private:
		void handle(C_Request* m, bool handle_pending = false);
		void handle(C_Checkpoint* m);
		void handle(C_Panic* m);
      void handle(C_Abort *m);
      void handle(C_Missing *m);
      void handle(C_Get_a_grip *m);
		void handle(C_Client_notification *m);

		void handle_new_connection();

      void broadcast_abort(Request_id out_rid);
		// Effects: Sends abort message to all

		Seqno seqno; // Sequence number to attribute to next protocol message,
			     // we'll use this counter to index messages in history 
		Seqno last_seqno;
		unsigned long req_count_switch;

		// Last replies sent to each principal.
		C_Rep_info replies;

		int in_socket;
		int in_socket_for_clients;
		int out_socket;
		int all_replicas_socket;

		// Threads
		pthread_t requests_from_predecessor_handler_thread;
		pthread_t C_client_requests_handler_thread;
		pthread_t C_robust_handler_thread;

		/* Array of connected sockets so we know who we are talking to */
		int connectlist[MAX_CONNECTIONS];

		// Pointers to the function to be executed when receiving a message
		int (*exec_command)(Byz_req *, Byz_rep *, Byz_buffer *, int, bool);

		// Request handling helper methods
		bool is_old(C_Request *req);
		bool is_seen(C_Request *req);
		void execute_request(C_Request *req);

		// Store for requests for batching
		C_Mes_queue batching_queue;

		// request history
		Req_history_log<C_Request> *rh;

      C_ITimer *ctimer;       // No checkpoint timer
      C_ITimer *ftimer;       // No request but feedback timer

		// keeps replica's state
		enum replica_state cur_state;

		// for keeping received aborts
		C_Abort_certificate aborts;
		// keep the list of missing request
		AbortHistory *ah_2;
		AbortHistory *missing;
		// missing mask keeps the track of requests we're still missing
		// while num_missing is represents how many requests we still need
		// invariant: num_missing = sum(missing_mask[i]==true);
		unsigned int num_missing;
		Array<bool> missing_mask;
		Array<C_Request*> missing_store;
		Array<Seqno> missing_store_seq;

		bool checkpoint_in_progress;
		int current_nb_requests_while_checkpointing;
		std::map<Seqno, CheckpointSet*> current_checkpoints;
		std::map<Seqno, C_Request*> pending_requests;
      std::vector<C_Request*> primary_pending_requests;
		Seqno last_chkp_seqno;

		pthread_mutex_t checkpointing_mutex;
};


// Pointer to global replica object.
extern C_Replica *C_replica;

#endif //_C_Replica_h
