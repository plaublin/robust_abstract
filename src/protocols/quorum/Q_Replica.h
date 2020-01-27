#ifndef _Q_Replica_h
#define _Q_Replica_h 1

#include <pthread.h>

#include <map>
#include <list>

#include "Q_Cryptography.h"
#include "types.h"
#include "Q_Rep_info.h"
#include "Digest.h"
#include "Q_Node.h"
#include "libbyz.h"
#include "Q_Checkpoint.h"
#include "Q_Panic.h"
#include "Q_Abort.h"
#include "Q_Abort_certificate.h"
#include "Q_Smasher.h"
#include "Q_ITimer.h"

#include "Switcher.h"
#include "Request_history.h"

class Q_Request;
class Q_Reply;
class Q_Missing;
class Q_Get_a_grip;

#define ALIGNMENT_BYTES 2

extern void Q_abort_timeout_handler();

class CheckpointSet {
   public:
      CheckpointSet(int sz, int c, Seqno seqno) {
         size = sz;
         complete = c;
         n = 0;
         s = seqno;
         elems = new Q_Checkpoint*[size];
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

      bool add(int i, Q_Checkpoint *c) {
         th_assert(i>=0 && i<size, "add out of range");
         if (elems[i] != NULL || c->get_seqno() != s) {
            //fprintf(stderr, "Cannot add checkpoint %lld to cset[%d]=%p for seqno %lld\n", c->get_seqno(), i, elems[i], s);
            return false;
         } else {
            elems[i] = c;
            n++;
            return true;
         }
      }

      Q_Checkpoint* get(int i) {
         th_assert(i>=0 && i<size, "get out of range");
         return elems[i];
      }

      Q_Checkpoint** fetch(int *sz) {
         *sz = size;
         return elems;
      }

      bool is_complete(void) {
        return (n == complete);
      }

      int count(void) {
         return n;
      }

      Seqno get_seqno(void) {
         return s;
      }

   private:
      Q_Checkpoint** elems;
      int size;
      int n;
      Seqno s;
      int complete;
};

class Q_Replica : public Q_Node
{
	public:

		Q_Replica(FILE *config_file, FILE *config_priv, char *host_name, short port=0);
		// Effects: Create a new server replica using the information in
		// "config_file".

		virtual ~Q_Replica();
		// Effects: Kill server replica and deallocate associated storage.

		void register_exec(int (*e)(Byz_req *, Byz_rep *, Byz_buffer *, int, bool));
		// Effects: Registers "e" as the exec_command function.

		void register_perform_checkpoint(int (*p)());
		// Effects: Registers "p" as the perform_checkpoint function.

		void handle_incoming_messages();
		// Effects: Loops receiving messages and calling the appropriate
		// handlers.
		void handle(Q_Message *msg);

		void do_recv_from_network();

		template <class T> inline void gen_handle(Q_Message *m)
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

		void enable_replica(bool state);
		// if state == true, enable, else disable

		void retransmit_panic();

		void join_mcast_group();
		// Effects: Enables receipt of messages sent to replica group

		void leave_mcast_group();
		// Effects: Disables receipt of messages sent to replica group

      void no_checkpoint_timeout();
      // If there has not been a checkpoint for the last n secs, then
      // panic

	private:
		void handle(Q_Request *m);
		void handle(Q_Checkpoint *c);
		void handle(Q_Panic *c);
		void handle(Q_Abort *m);
		void handle(Q_Missing *m);
		void handle(Q_Get_a_grip *m);
		// Effects: Handles the checkpoint message

		Seqno seqno; // Sequence number to attribute to next protocol message,
			     // we'll use this counter to index messages in history 
    unsigned long req_count_switch;

		int (*exec_command)(Byz_req *, Byz_rep *, Byz_buffer *, int, bool);

		int (*perform_checkpoint)();

		bool execute_request(Q_Request *m);

		void broadcast_abort(Request_id out_rid);
		// Effects: Sends abort message to all

		void notify_outstanding();
		// Effects: notifies all outstanding clients to abort

		// Last replies sent to each principal.x
		Q_Rep_info *replies;

		// Used to generate the id of the next replier
		int replier;
		int nb_retransmissions;

		// Request history
		Req_history_log<Q_Request> *rh;

    bool checkpoint_in_progress;
    int current_nb_requests_while_checkpointing;
    std::map<Seqno, CheckpointSet*> current_checkpoints;
    Seqno last_chkp_seqno;

    pthread_mutex_t checkpointing_mutex;
    pthread_mutex_t switching_mutex;

		// keeps replica's state
		enum replica_state cur_state;

		// for keeping received aborts
		Q_Abort_certificate aborts;
		// keep the list of missing request
		AbortHistory *ah_2;
		AbortHistory *missing;
		// missing mask keeps the track of requests we're still missing
		// while num_missing is represents how many requests we still need
		// invariant: num_missing = sum(missing_mask[i]==true);
		unsigned int num_missing;
		Array<bool> missing_mask;
		Array<Q_Request*> missing_store;
		Array<Seqno> missing_store_seq;

		// timouts
		int n_retrans;        // Number of retransmissions of out_req
		int rtimeout;         // Q_Timeout period in msecs
		Q_ITimer *rtimer;       // Retransmission timer
		Q_ITimer *ctimer;       // No checkpoint timer

		std::list<OutstandingRequests> outstanding;
};

// Pointer to global replica object.
extern Q_Replica *Q_replica;

#endif //_Q_Replica_h
