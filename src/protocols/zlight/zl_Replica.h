#ifndef _zl_Replica_h
#define _zl_Replica_h 1

#include <pthread.h>

#include <map>
#include <list>

#include "zl_Cryptography.h"
#include "types.h"
#include "zl_Rep_info.h"
#include "Digest.h"
#include "zl_Node.h"
#include "libbyz.h"
#include "Set.h"
#include "zl_Checkpoint.h"
#include "zl_Panic.h"
#include "zl_Abort.h"
#include "zl_Abort_certificate.h"
#include "zl_Smasher.h"
#include "zl_ITimer.h"

#include "Switcher.h"
#include "Request_history.h"

class zl_Request;
class zl_Reply;
class zl_Missing;
class zl_Get_a_grip;
class zl_Order_request;
class zl_Client_notification;

#define ALIGNMENT_BYTES 2

typedef Set<zl_Checkpoint> zl_CheckpointSet;
typedef std::pair<int, Request_id> zl_Message_id;
typedef std::pair<zl_Request*, Seqno> zl_Pending_Request;

extern void zl_abort_timeout_handler();

class CheckpointSet {
   public:
      CheckpointSet(int sz, int c, Seqno seqno) {
         size = sz;
         complete = c;
         n = 0;
         s = seqno;
         elems = new zl_Checkpoint*[size];
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

      bool add(int i, zl_Checkpoint *c) {
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

      zl_Checkpoint* get(int i) {
         th_assert(i>=0 && i<size, "get out of range");
         return elems[i];
      }

      zl_Checkpoint** fetch(int *sz) {
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
      zl_Checkpoint** elems;
      int size;
      int n;
      Seqno s;
      int complete;
};

class zl_Replica : public zl_Node
{
	public:

		zl_Replica(FILE *config_file, FILE *config_priv, char *host_name, short port=0);
		// Effects: Create a new server replica using the information in
		// "config_file".

		virtual ~zl_Replica();
		// Effects: Kill server replica and deallocate associated storage.

		void register_exec(int (*e)(Byz_req *, Byz_rep *, Byz_buffer *, int, bool));
		// Effects: Registers "e" as the exec_command function.

		void register_perform_checkpoint(int (*p)());
		// Effects: Registers "p" as the perform_checkpoint function.

		void handle_incoming_messages_from_queue();
		// Effects: Loops receiving messages and calling the appropriate
		// handlers.
		//
		void zl_receive_group_requests_handler();
		void zl_receive_requests_handler();
        void handle(zl_Message* msg);

		template <class T> inline void gen_handle(zl_Message *m)
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

        long long nb_sent_replies;
        int max_history_size;

    private:
		void handle(zl_Request *m);
		void handle(zl_Checkpoint *c);
		void handle(zl_Panic *c);
		void handle(zl_Abort *m);
		void handle(zl_Missing *m);
		void handle(zl_Get_a_grip *m);
		void handle(zl_Order_request *m);
		void handle(zl_Client_notification *m);
		// Effects: Handles the checkpoint message

		Seqno seqno; // Sequence number to attribute to next protocol message,
			     // we'll use this counter to index messages in history
		Seqno last_seqno; // Sequence number of last executed message
        unsigned long req_count_switch;

		int (*exec_command)(Byz_req *, Byz_rep *, Byz_buffer *, int, bool);

		int (*perform_checkpoint)();

		bool execute_request(zl_Request *m);

		void broadcast_abort(Request_id out_rid);
		// Effects: Sends abort message to all

		void notify_outstanding();
		// Effects: notifies all outstanding clients to abort

		// Last replies sent to each principal.x
		zl_Rep_info *replies;

		// Threads
		pthread_t request_queue_handler_thread;
		pthread_t zl_receive_requests_handler_thread;
		pthread_t zl_receive_group_requests_handler_thread;

		// Used to generate the id of the next replier
		int replier;
        int nb_retransmissions;

        // Request history
        Req_history_log<zl_Request> *rh;

        bool checkpoint_in_progress;
        int current_nb_requests_while_checkpointing;
        std::map<Seqno, CheckpointSet*> current_checkpoints;
        Seqno last_chkp_seqno;

        pthread_mutex_t checkpointing_mutex;
        pthread_mutex_t switching_mutex;

        // keeps replica's state
        enum replica_state cur_state;

		// for keeping received aborts
		zl_Abort_certificate aborts;
		// keep the list of missing request
		AbortHistory *ah_2;
		AbortHistory *missing;
		// missing mask keeps the track of requests we're still missing
		// while num_missing is represents how many requests we still need
		// invariant: num_missing = sum(missing_mask[i]==true);
		unsigned int num_missing;
		Array<bool> missing_mask;
		Array<zl_Request*> missing_store;
		Array<Seqno> missing_store_seq;

		// timouts
		int n_retrans;        // Number of retransmissions of out_req
		int rtimeout;         // zl_Timeout period in msecs
		zl_ITimer *rtimer;       // Retransmission timer
    zl_ITimer *ctimer;       // No checkpoint timer

      std::list<OutstandingRequests> outstanding;
      std::map<zl_Message_id, zl_Pending_Request> pending_requests;

      int mcast_sock;
};

// Pointer to global replica object.
extern zl_Replica *zl_replica;

#endif //_zl_Replica_h
