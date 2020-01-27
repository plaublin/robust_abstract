#ifndef _A_Replica_h
#define _A_Replica_h 1

#include "A_State_defs.h"
#include "types.h"
#include "A_Req_queue.h"
#include "Log.h"
#include "Set.h"
#include "A_Certificate.h"
#include "A_Prepared_cert.h"
#include "A_Big_req_table.h"
#include "A_View_info.h"
#include "A_Rep_info.h"
#include "Partition.h"
#include "Digest.h"
#include "A_Node.h"
#include "A_State.h"
#include "aardvark_libbyz.h"
#include "A_Request.h"
#include "A_Wrapped_request.h"
#include "A_parameters.h"
#include "A_Circular_buffer.h"
#include "Switcher.h"
#include "z2z_async_dlist.h"

//#ifdef ADAPTIVE
//#include "A_Time.h"i // why ?
//#endif

class A_Request;
class A_Reply;
class A_Pre_prepare;
class A_Prepare;
class A_Commit;
class A_Checkpoint;
class A_Status;
class A_View_change;
class A_New_view;
class A_New_key;
class A_Fetch;
class A_Data;
class A_Meta_data;
class A_Meta_data_d;
class A_Reply;
class Query_stable;
class Reply_stable;

extern void delay_pre_prepare_timer_handler();

#ifdef DELAY_ADAPTIVE
extern void pre_prepare_timer_handler();
//extern void call_pre_prepare_timer_handler();
#endif

#ifdef THROUGHPUT_ADAPTIVE
extern void throughput_timer_handler();
extern void increment_timer_handler();

#ifdef THROUGHPUT_ADAPTIVE_PERIODIC_COMPUTATION
// we now compute the expectations periodically
extern void compute_expectations_handler();
#endif
#endif

#ifdef PERIODICALLY_MEASURE_THROUGHPUT
extern void periodic_thr_measure_handler();
#endif

extern void A_vtimer_handler();
extern void A_stimer_handler();
extern void A_ntimer_handler();

#define ALIGNMENT_BYTES 2

class A_Replica: public A_Node
{
public:

  A_Replica(FILE *config_file, FILE *config_priv, char *mem, int nbytes,
      int byz_pre_prepare_delay = 0, int delay_every_p_pp = 1,
      bool small_batches = false, long int exec_command_delay = 0,
      int sliding_window_size = 0);
  // Requires: "mem" is vm page aligned and nbytes is a multiple of the
  // vm page size.
  // Effects: Create a new server A_replica using the information in
  // "config_file" and "config_priv". The A_replica's state is set to the
  // "nbytes" of memory starting at "mem". 

  virtual ~A_Replica();
  // Effects: Kill server A_replica and deallocate associated storage.

  void recv();
  // Effects: Loops receiving messages. if idle, may dequeue status and fetch messages and dispatch
  // them to the appropriate handlers

  void jump_table(A_Message *m);
  //dispatches m to the appropriate handler (or queue)

  void send(A_Message *m, int i);
  // used to send a message using the appropriate socket

  // Methods to register service specific functions. The expected
  // specifications for the functions are defined below.
  void register_exec(int(*e)(Byz_req *, Byz_rep *, Byz_buffer *, int, bool,
      long int));
  // Effects: Registers "e" as the exec_command function. 

  void enable_replica(bool state);

  void register_nondet_choices(void(*n)(Seqno, Byz_buffer *), int max_len);
  // Effects: Registers "n" as the non_det_choices function.

  void compute_non_det(Seqno n, char *b, int *b_len);
  // Requires: "b" points to "*b_len" bytes.  
  // Effects: Computes non-deterministic choices for sequence number
  // "n", places them in the array pointed to by "b" and returns their
  // size in "*b_len".

  int max_nd_bytes() const;
  // Effects: Returns the maximum length in bytes of the choices
  // computed by compute_non_det

  int used_state_bytes() const;
  // Effects: Returns the number of bytes used up to store protocol
  // information.

  void modify(char *mem, int size);
  // Effects: Informs the system that the memory region that starts at
  // "mem" and has length "size" bytes is about to be modified.


  void modify_index(int bindex);
  // Effects: Informs the system that the memory page with index
  // "bindex" is about to be modified.

  void process_new_view(Seqno min, Digest d, Seqno max, Seqno ms);
  // Effects: Update A_replica's state to reflect a new-view: "min" is
  // the sequence number of the checkpoint propagated by new-view
  // message; "d" is its digest; "max" is the maximum sequence number
  // of a propagated request +1; and "ms" is the maximum sequence
  // number known to be stable.

  void send_view_change();
  // Effects: Send view-change message.

  void send_status(bool force = false);
  // Effects: Sends a status message.
  // [elwong] Now takes a parameter that can force the message out
  //  regardless of when the last message was sent.

  bool has_req(int cid, const Digest &d);
  // Effects: Returns true iff there is a request from client "cid"
  // buffered with operation digest "d". XXXnot great

  bool delay_vc();
  // Effects: Returns true iff view change should be delayed.

  A_Big_req_table* big_reqs();
  // Effects: Returns the A_replica's big request table.

  bool has_new_view() const;
  // Effects: Returns true iff the A_replica has complete new-view
  // information for the current view.

#ifdef THROUGHPUT_ADAPTIVE
  // adapt the throughput
  void adapt_throughput(A_Time current);

  // display the expectations
  void display_expectations(A_Time current);
#endif

  void* run_verifier_thread(void);

  A_Circular_buffer *verifier_thr_to_replica_buffer;

#ifdef PERIODICALLY_MEASURE_THROUGHPUT
  A_ITimer *periodic_thr_measure;
  int nb_requests_4_periodic_thr_measure;
  long long start_cycle_4_periodic_thr_measure;
  float measured_throughput[MAX_NB_MEASURES];
  int next_measure_idx;

  long long first_request_time;
#endif

#ifdef E_AARDVARK
  float *sliding_window;
  int sliding_window_cur_idx;
  int sliding_window_nb_got; // number of empty values in the sliding window, at the beginning
  // not to compute the mean over a lot of non-valid values.
  int sliding_window_size;
#endif

  bool vc_already_triggered; // true if one of the adaptive algoriths have already triggered a view change
  unsigned long req_count; // the count of requests executed since the
  // last checkpoint

  unsigned long req_count_vc; // the count of requests executed since the
  // last view change

  unsigned long req_count_switch; // the count of requests executed since the
  // last switching

  int checkpoints_in_new_view;
#ifdef THROUGHPUT_ADAPTIVE
  A_Time last_view_time; // time the last checkpoint completed
  A_Time last_cp_time; // time the last checkpoint completed

  float *last_throughput_of_replica; // last throughput observed for each primary  
  float req_throughput; // the required throughput for the system
  float req_throughput_increment;
  bool time_to_increment;
  long long total_nb_checkpoints;
  bool saturated; // true if the system has remained
  // saturated since the last checkpoing,
  // false otherwise.
  bool first_checkpoint_after_view_change; // true if we are in the first checkpoint interval after a view change happened
  float highest_throughput_ever;

  float obs_throughput;
  float obs_throughput_in_checkpoint;
#endif

#ifdef RRBFT_ATTACK
  int sleeping_times[MAX_NB_SLEEPING_TIME_LOGGED];
  int next_sleeping_time_idx;

  unsigned long total_nb_sent_pp;
  unsigned long total_nb_delayed_pp;

  int nb_pp_since_checkpoint_start;
  int nb_req_in_pp_since_checkpoint_start;

#ifdef THROUGHPUT_ADAPTIVE_PERIODIC_COMPUTATION
  int nb_req_in_pp_since_last_expectations_computation;
#endif

  // we use last_cp_time for the time at which the checkpoint has started
  float next_expected_throughput;
  A_Pre_prepare *fake_pp; // fake pp, used to assess the size of the next PP
#endif

#ifdef THROUGHPUT_ADAPTIVE_PERIODIC_COMPUTATION
  A_ITimer *compute_expectations_timer;
  long long start_cycle_4_periodic_thr_adaptive_computation;
#endif

private:
  friend class A_State;
  friend class A_Verifier_thread;
  friend class A_Wrapped_request;
  //
  // A_Message handlers:
  //
  void handle(A_Request* m);
  void handle(A_Pre_prepare* m);
  void handle(A_Prepare* m);
  void handle(A_Commit* m);
  void handle(A_Checkpoint* m);
  void handle(A_View_change* m);
  void handle(A_New_view* m);
  void handle(A_View_change_ack* m);
  void handle(A_Status* m);
  void handle(A_New_key* m);
  void handle(A_Fetch* m);
  void handle(A_Data* m);
  void handle(A_Meta_data* m);
  void handle(A_Meta_data_d* m);
  void handle(A_Reply* m, bool mine = false);
  void handle(Query_stable* m);
  void handle(Reply_stable* m);
  // Effects: Execute the protocol steps associated with the arrival
  // of the argument message.  

  // verify a request coming from a A_Client
  A_Request* verify(A_Wrapped_request* m);

  friend void delay_pre_prepare_timer_handler();

#ifdef DELAY_ADAPTIVE
  friend void pre_prepare_timer_handler();
#endif

#ifdef THROUGHPUT_ADAPTIVE
  friend void throughput_timer_handler();
  friend void increment_timer_handler();
#endif

  friend void A_vtimer_handler();
  friend void A_stimer_handler();
  friend void A_ntimer_handler();
  // Effects: Handle timeouts of corresponding timers.

  int nb_retransmissions;
  int nb_executed;
  long long elapsed_sum1, elapsed_sum2, elapsed_sum3, elapsed_sum4;

  enum replica_state cur_state;

#ifdef NEW_BIG_MAC_ATTACK
  // send the PP to only 2f replicas
  // called by A_replica 0 when it is the primary
  void send_pp_to_2f_replicas(A_Pre_prepare *pp);
#endif

  //
  // Auxiliary methods used by primary to send messages to the A_replica
  // group:
  //
  void send_pre_prepare(bool force_send = false);
  // Effects: sends a pre_prepare message.  If force_send is true then
  // sends a pre_prepare message no matter what.  If force_send is
  // false then sends a pre_prepare message only the batch is full


  void send_prepare(A_Prepared_cert& pc);
  // Effects: Sends a prepare message if appropriate.

  void send_commit(Seqno s);

  void send_null();
  // Send a pre-prepare with a null request if the system is idle

  // 
  // Miscellaneous:
  //
  bool execute_read_only(A_Request *m);
  // Effects: If some request that was tentatively executed did not
  // commit yet (i.e. last_tentative_execute < last_executed), returns
  // false.  Otherwise, returns true, executes the command in request
  // "m" (provided it is really read-only and does not require
  // non-deterministic choices), and sends a reply to the client

  void execute_committed();
  // Effects: Executes as many commands as possible by calling
  // execute_prepared; sends A_Checkpoint messages when needed and
  // manipulates the wait timer.

  void execute_prepared(bool committed = false);
  // Effects: Tentatively executes as many commands as possible. It
  // extracts requests to execute commands from a message "m"; calls
  // exec_command for each command; and sends back replies to the
  // client. The replies are tentative unless "committed" is true.

  void mark_stable(Seqno seqno, bool have_state);
  // Requires: A_Checkpoint with sequence number "seqno" is stable.
  // Effects: Marks it as stable and garbage collects information.
  // "have_state" should be true iff the A_replica has a the stable
  // checkpoint.

  void new_state(Seqno seqno);
  // Effects: Updates this to reflect that the checkpoint with
  // sequence number "seqno" was fetch.

  A_Pre_prepare *prepared(Seqno s);
  // Effects: Returns non-zero iff there is a pre-prepare pp that prepared for
  // sequence number "s" (in this case it returns pp).

  A_Pre_prepare *committed(Seqno s);
  // Effects: Returns non-zero iff there is a pre-prepare pp that committed for
  // sequence number "s" (in this case it returns pp).

  template<class T> bool in_w(T *m);
  // Effects: Returns true iff the message "m" has a sequence number greater
  // than last_stable and less than or equal to last_stable+max_out.

  template<class T> bool in_wv(T *m);
  // Effects: Returns true iff "in_w(m)" and "m" has the current view.

  template<class T> void gen_handle(A_Message *m);
  // Effects: Handles generic messages.

  template<class T> void retransmit(T *m, A_Time &cur, A_Time *tsent, A_Principal *p);
  // Effects: Retransmits message m (and re-authenticates it) if
  // needed. cur should be the current time.

  bool retransmit_rep(A_Reply *m, A_Time &cur, A_Time *tsent, A_Principal *p);

  void send_new_key();
  // Effects: Calls A_Node's send_new_key, adjusts timer and cleans up
  // stale messages.

  void update_max_rec();
  // Effects: If max_rec_n is different from the maximum sequence
  // number for a recovery request in the state, updates it to have
  // that value and changes keys. Otherwise, does nothing.

  A_Message* pick_next_status();

  // old code which uses the original circular buffer
#if 0
  bool cb_write_message(A_Message*);
  // write A_Message* inside circualr_buffer
  // returns true if the message hase been written in the buffer
  // or false if the buffer is full

  A_Message* cb_read_message();
  // reads a message from the circular buffer
  // returns a pointer to the message if there is something ready to read
  // or NULL if the buffer is empty
#endif

  //
private:
  // Instance variables:
  //
  Seqno seqno; // Sequence number to attribute to next protocol message,
  // only valid if I am the primary.

  // old code which uses the original circular buffer
#if 0
  A_Message** circular_buffer; // circuler buffer used to pass messages from the A_verifier thread to the main thread
  int cb_write_index; // points to the first empty position in the buffer (ready to be written)
  int cb_read_index; // points at the oldest unread message stored in the buffer (ready to be read) 
  A_Message* circular_buffer_magic;
#endif

  /* the list of sockets for connections with incoming clients */
  int *clients_socks;

  int *snd_socks;
  int *rcv_socks;
  int bootstrap_socket;
  struct sockaddr_in bootstrap_sin;

  // Logging variables used to measure average batch size
  int nbreqs; // The number of requests executed in current interval
  int nbrounds; // The number of rounds of BFT executed in current interval

  Seqno last_stable; // Sequence number of last stable state.
  Seqno low_bound; // Low bound on request sequence numbers that may
  // be accepted in current view.

  Seqno last_prepared; // Sequence number of highest prepared request
  Seqno last_executed; // Sequence number of last executed message.
  Seqno last_tentative_execute; // Sequence number of last message tentatively
  // executed.

  long int exec_command_delay; //delay caused by a request execution.

  // Sets and logs to keep track of messages received. Their size
  // is equal to max_out.
  A_Req_queue rqueue; // For read-write requests.
  A_Req_queue ro_rqueue; // For read-only requests

  Log<A_Prepared_cert> plog;

  A_Big_req_table brt; // Table with big requests
  friend class A_Big_req_table;

  Log<A_Certificate<A_Commit> > clog;
  Log<A_Certificate<A_Checkpoint> > elog;

  // Set of stable checkpoint messages above my window.
  Set<A_Checkpoint> sset;

  // Last replies sent to each principal.
  A_Rep_info replies;

  // A_State abstraction manages state checkpointing and digesting
  A_State state;

  A_ITimer *stimer; // A_Timer to send status messages periodically.
  A_Time last_status; // A_Time when last status message was sent


  // 
  // View changes:
  //
  A_View_info vi; // View-info abstraction manages information about view changes
  A_ITimer *vtimer; // View change timer

  bool need_requests; // flag if we need to gather client requests

#ifdef DELAY_ADAPTIVE
  A_ITimer *pre_prepare_timer;
#endif

#ifdef THROUGHPUT_ADAPTIVE
  A_ITimer *throughput_timer; //Very short view change timer triggered by violations of the expected throughput (only in saturation periods)
  A_ITimer *increment_timer;
#endif

  // the following timer is there to delay sending of pre_prepares
  A_ITimer *delay_pre_prepare_timer;

  struct delayed_pp
  {
    A_Pre_prepare *pp;
    A_Time t; // time at which the PP has been created, in cycles
    struct list_head link;
  };

  struct list_head delayed_pps;

  bool limbo; // True iff moved to new view but did not start vtimer yet.
  bool has_nv_state; // True iff A_replica's last_stable is sufficient
  // to start processing requests in new view.
  bool status_messages_badly_needed;
  Seqno not_deprioritize_status_before_this;

#ifdef REPLICA_FLOOD_PROTECTION
  bool flood_protection_active;
  Seqno flood_protection_view;
  int rmcount[5];
#endif

  //
  // Recovery
  //
  bool rec_ready; // True iff A_replica is ready to recover
  bool recovering; // True iff A_replica is recovering.
  bool vc_recovering; // True iff A_replica exited limbo for a view after it started recovery
  bool corrupt; // True iff A_replica's data was found to be corrupt.

  A_ITimer* ntimer; // A_Timer to trigger transmission of null requests when system is idle
  fd_set file_descriptors; //set of file descriptors to listen to (only one socket, in this case)
  timeval listen_time; //max time to wait for something readable in the file descriptors
  // non-blocking socket listening
  // (return earlier if something becames readable)

  A_Message** status_pending; //array used to store pointers of peding status messages
  int status_to_process; //index to the next status to process (round robin)
  int s_identity; //just an index...

  long int byz_pre_prepare_delay;
  long int delay_every_p_pp;
  long int nb_pp_so_far;
  bool small_batches;

  // Estimation of the maximum stable checkpoint at any non-faulty A_replica

  A_Request *rr; // Outstanding recovery request or null if
  // there is no outstanding recovery request.
  A_Certificate<A_Reply> rr_reps; // A_Certificate with replies to recovery request.
  View *rr_views; // Views in recovery replies.

  Seqno recovery_point; // Seqno_max if not known  
  Seqno max_rec_n; // Maximum sequence number of a recovery request in state.

  //
  // Pointers to various functions.
  //
  int (*exec_command)(Byz_req *, Byz_rep *, Byz_buffer *, int, bool, long int);

  void (*non_det_choices)(Seqno, Byz_buffer *);
  int max_nondet_choice_len;

  bool print_stuff;

#ifdef FAIRNESS_ADAPTIVE
  Seqno send_view_change_now;
  int max_rqueue_size; // maximum size of rqueue during a checkpoint interval
  Seqno *executed_snapshot; //arrays of Seqnos used to check fairness
  //when a client add a request to rqueue, executed_snapshot[client_id] = last_executed
  //when the request is removed from the queue (in execute_committed) the fairness
  //is violated if the request seqno is higher than executed_snapshot[client_id]+fairness_bound
  int fairness_bound; // 2 * num_principals should be a good higher bound...
#endif


#ifdef DELAY_ADAPTIVE
  int received_pre_prepares; //pre_prepares received since the last check
#endif

  bool* excluded_replicas; //array of bool. if excluded_replicas[i] == true, A_replica i is not added to the socket i listen in select
  bool excluded_clients;

  int client_socket; //read client request from there
  // int client_port;
  //int interval;
  // char* message;
  bool* blacklisted; //this will point to an array of num_principals bools, initialized at 0
  //client with id client_id is blacklisted iff blacklisted[client_id]==true
  Addr client_address;

};

// Pointer to global A_replica object.
extern A_Replica *A_replica;

extern "C" void *start_verifier_thread(void *);

inline int A_Replica::max_nd_bytes() const
{
  return max_nondet_choice_len;
}

inline int A_Replica::used_state_bytes() const
{
  return replies.size();
}

inline void A_Replica::modify(char *mem, int size)
{
  state.cow(mem, size);
}

inline void A_Replica::modify_index(int bindex)
{
  state.cow_single(bindex);
}

inline bool A_Replica::has_new_view() const
{
  return v == 0 || (has_nv_state && vi.has_new_view(v));
}

template<class T> inline void A_Replica::gen_handle(A_Message *m)
{
  T *n;
  if (T::convert(m, n))
  {
    handle(n);
  }
  else
  {
    delete m;
  }
}

inline bool A_Replica::delay_vc()
{
  return state.in_fetch_state();
}

inline A_Big_req_table* A_Replica::big_reqs()
{
  return &brt;
}

#endif //_Replica_h
