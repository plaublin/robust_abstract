#ifndef ROBUST_MONITOR_H_
#define ROBUST_MONITOR_H_

#include <map>

#include "types.h"
#include "libbyz.h"

#define MONITOR_PORT 7777

#undef CLIENT_SENDS_NOTIFICATION
#define NOTIF_PERIOD 5

#undef NEVER_SWITCH
#define GRACE_PERIOD 5 // grace period duration, in seconds

#define REQUEST_FEEDBACK_TIMEOUT 100
//panic if has received a request feedback but no request during the last <this value> ms

#define RUN_ON_TERMINATORS

typedef void (*handle_msg_func)(int); // function to handle a received message, for each protocol

struct _Monitor_LE
{
    enum protocols_e protocol;
    handle_msg_func func;
    _Monitor_LE* next;
};


/* TIME RELATED */

typedef unsigned long long RM_Time;

static inline long long RM_rdtsc(void) {
  union {
    struct {
      unsigned int l;  /* least significant word */
      unsigned int h;  /* most significant word */
    } w32;
    unsigned long long w64;
  } v;

  __asm __volatile (".byte 0xf; .byte 0x31     # C_rdtsc instruction"
                    : "=a" (v.w32.l), "=d" (v.w32.h) :);
  return v.w64;
}

static RM_Time RM_clock_mhz = 0;

static inline RM_Time RM_current_time() { return RM_rdtsc(); }

static inline RM_Time RM_diff_time(RM_Time t1, RM_Time t2) {
   return (t1-t2)/RM_clock_mhz;
}


/*********** EXPECTATIONS ***********/

static const float RM_req_throughput_init = 0;
//the required throughput is initialized to req_throughout_init reqs/s.

static const int RM_expectations_computation_period = 128;
//period at which the expectations are computed, in number of received reply notification

static const int RM_display_expectations_period = 100;
//display the expectations every <this value> calls to compute_expectations


/*
===============================================================================

   RobustMonitor
   This class is common to all protocols. It is used to monitor the performance
   of the replicas.
   It is also in charge of establishing connections between all replicas, which
   use several NICs for robustness

===============================================================================
*/
class RobustMonitor {
public:
   RobustMonitor(const char* config_file, const char* host_name, const short req_port);
   ~RobustMonitor();

   void register_handle_msg_func(const enum protocols_e protocol, handle_msg_func func);
   void deregister_handle_msg_func(const enum protocols_e protocol);
   void switch_protocol(const enum protocols_e protocol);
   void send_message(const char* msg, const int len, const int dest, const enum protocols_e protocol_instance);
   void* run(void);

   /* expectations */
   void add_client_req(int cid, Request_id rid);
   void add_client_req_notif(int cid, Request_id rid);
   void add_client_req_notif(void);
   void add_client_rep_notif(void);
   void add_client_rep_notif(int cid);
   void compute_expectations(void);
   bool should_panic(void);
   void print_expectations(void);
   void set_aardvark_req_throughput(float aardvark_thr);
   bool not_enough_requests(void);
   bool fairness_problem(void);

private:
   _Monitor_LE* find(const enum protocols_e protocol);

   _Monitor_LE* list;
   _Monitor_LE* current_protocol_LE;
   enum protocols_e current_protocol_name;
   Addr* addresses;
   int num_replicas;
   int id; // not to send the messages to self
   int *snd_socks;
   int *rcv_socks;

   /* expectations */
   //Note: thr are in req/sec
   float obs_thr;
   float req_thr;
   float req_thr_increment;
   long nb_recv_rep;
   RM_Time last_switch_time;
   bool in_grace_period;
   long nb_compute_expectations_calls;
   std::map<int, Request_id> last_client_req;
   std::map<int, Request_id> last_client_req_notif;
   std::map<int, long> nb_client_rep_notif;
   long total_nb_client_rep_notif;
};

extern RobustMonitor* robust_monitor;

inline void RobustMonitor::add_client_req_notif(void) {
}

inline void RobustMonitor::add_client_req(int cid, Request_id rid) {
  std::map<int, Request_id>::iterator it = last_client_req.find(cid);
  if (it == last_client_req.end())
  {
    last_client_req[cid] = rid;
  }
  else if (rid > last_client_req[cid])
  {
    last_client_req[cid] = rid;
  }
}

inline void RobustMonitor::add_client_req_notif(int cid, Request_id rid) {
  std::map<int, Request_id>::iterator it = last_client_req_notif.find(cid);
  if (it == last_client_req_notif.end())
  {
    last_client_req_notif[cid] = rid;
  }
  else if (rid > last_client_req_notif[cid])
  {
    last_client_req_notif[cid] = rid;
  }
}

inline void RobustMonitor::add_client_rep_notif(void)
{
  ++nb_recv_rep;
}

inline void RobustMonitor::add_client_rep_notif(int cid)
{
  add_client_rep_notif();

  total_nb_client_rep_notif++;
  std::map<int, long>::iterator it = nb_client_rep_notif.find(cid);
  if (it == nb_client_rep_notif.end())
  {
    nb_client_rep_notif[cid] = 1;
  }
  else
  {
    nb_client_rep_notif[cid]++;
  }
}

inline void RobustMonitor::set_aardvark_req_throughput(float aardvark_thr) {
   req_thr = aardvark_thr;
   fprintf(stderr, "Robust monitor sets expectations from Aardvark (%f req/s) to req_thr=%f, req_thr_incr=%f\n", aardvark_thr, req_thr, req_thr_increment);
}

inline bool RobustMonitor::should_panic(void) {
#ifdef NEVER_SWITCH
   return false;
#else
   return (!in_grace_period && obs_thr < req_thr);
#endif
}

#endif
