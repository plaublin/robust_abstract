#ifndef _A_Statistics_h
#define _A_Statistics_h

#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>

#include "types.h"
#include "Array.h"

#include "A_Cycle_counter.h"

struct A_Recovery_stats {
  Long shutdown_time; // Cycles spent in shutdown
  Long reboot_time;   
  Long restart_time;  // Cycles spent in restart
  Long est_time;      // Cycles for estimation procedure
  Long nk_time;       // Cycles to send new key message
  Long rr_time;       // Cycles to send the recovery request

  Long check_time;    // Cycles spent checking pages
  Long num_checked;   // Number of pages checked

  Long fetch_time;    // Cycles spent fetching during recovery
  Long num_fetched;   // Number of data blocks received
  Long num_fetched_a; // Number of data blocks accepted
  Long refetched;           // Number of blocks refetched
  Long num_fetches;
  Long meta_data_fetched;   // Number of meta-data[d] messages received
  Long meta_data_fetched_a; // Number of meta-data[d] messages accepted
  Long meta_data_bytes;
  Long meta_datad_fetched;    // Number of meta-data-d messages received
  Long meta_datad_fetched_a;  // Number of meta-data-d messages accepted
  Long meta_datad_bytes;      // Number of bytes in meta-data-d blocks
  Long meta_data_refetched;   // Number of meta-data blocks refetched  


  Long sys_cycles;    // Cycles spent handling syscalls during rec.

  Long rec_bin;
  Long rec_bout;
 
  Long rec_time;      // Cycles to complete recovery

  A_Recovery_stats();
  void print_stats();
  void zero_stats();
};


struct A_Statistics {
  // Number of cycles after statistics were zeroed.
  A_Cycle_counter cycles_after_zero; 

  //
  // Authenticators:
  //
  long num_gen_auth; // Number of authenticators generated
  A_Cycle_counter gen_auth_cycles; // and number of cycles.
  
  long num_ver_auth;   // Number of authenticators verified 
  A_Cycle_counter ver_auth_cycles;// and number of cycles.
  
  long reply_auth;   // Number of replies authenticated
  A_Cycle_counter reply_auth_cycles; // and number of cycles.
  
  long reply_auth_ver;// Number of replies verified
  A_Cycle_counter reply_auth_ver_cycles; // and number of cycles.

  //
  // Digests:
  //
  long num_digests;  // Number of digests
  A_Cycle_counter digest_cycles; // and number of cycles.

  long pp_digest; // Number of times pre-prepare digests are computed 
  A_Cycle_counter pp_digest_cycles; // and number of cycles.

  
  //
  // Signatures
  //
  long num_sig_gen;  // Number of signature generations
  A_Cycle_counter sig_gen_cycles; // and number of cycles.

  long num_sig_ver; // Number of signature verifications
  A_Cycle_counter sig_ver_cycles; // and number of cycles.


  //
  // Recovery:
  //
  Array<A_Recovery_stats> rec_stats;
  long rec_counter;  // Number of recoveries
  long rec_overlaps; // Number of recoveries that ended after executing recovery 
                     // request for next A_replica.
  long incomplete_recs;      // Number of recoveries ended by my next recovery 
  A_Cycle_counter rec_time;    // Total cycles for recovery
  A_Cycle_counter est_time;    // Cycles for estimation procedure
  A_Cycle_counter nk_time;     // Cycles to send new key message
  A_Cycle_counter rr_time;     // Cycles to send the recovery request
  long num_checked;          // Number of pages checked
  A_Cycle_counter check_time;  // Cycles spent checking pages
  A_Cycle_counter shutdown_time; // Cycles spent in shutdown
  A_Cycle_counter restart_time;  // Cycles spent in restart
  A_Cycle_counter reboot_time;   

  //
  // Bandwidth:
  // 
  long long bytes_in;
  long long bytes_out;

  //
  // View changes:
  //

  //
  // A_State:
  //
  long num_fetches;              // Number of times fetch is started
  long num_fetched;              // Number of data blocks fetched
  long num_fetched_a;            // Number of data blocks accepted
  long refetched;                // Number of data refetched while checking
  long meta_data_fetched;        // Number of meta-data messages received
  long meta_data_fetched_a;      // Number of meta-data messages accepted
  long meta_data_bytes;          // Number of bytes in meta-data blocks
  long meta_datad_fetched;       // Number of meta-data-d messages received
  long meta_datad_fetched_a;     // Number of meta-data-d messages accepted
  long meta_datad_bytes;         // Number of bytes in meta-data-d blocks
  long meta_data_refetched;
  long num_ckpts;                // Number of checkpoints computed
  A_Cycle_counter ckpt_cycles;     // and number of cycles.
  long num_rollbacks;            // Number of rollbacks
  A_Cycle_counter rollback_cycles; // and number of cycles
  long num_cows;                 // Number of copy-on-writes
  A_Cycle_counter cow_cycles;      // and number of cycles
  A_Cycle_counter fetch_cycles;    // Cycles fetching state (w/o waiting)

  long cache_hits;
  long cache_misses;

  //
  // Syscalls:
  //
  long num_recvfrom;     // Number of recvfroms
  long num_recv_success; // Number of successful recvfroms
  A_Cycle_counter recvfrom_cycles; // and number of cycles
  
  long num_sendto; // Number of sendtos
  A_Cycle_counter sendto_cycles; // and number of cycles

  A_Cycle_counter select_cycles; // Number of cycles in select
  long select_success; // Number of times select exits with fd set
  long select_fail;    // Number of times select exits with fd not set.
  
  A_Cycle_counter handle_timeouts_cycles;

  struct rusage ru;

  long req_retrans; // Number of request retransmissions 


  A_Statistics();
  void print_stats();
  void zero_stats();

  void init_rec_stats();
  void end_rec_stats();
};


extern A_Statistics A_stats;

//#define PRINT_STATS
#ifdef PRINT_STATS
#define START_CC(x) A_stats.##x##.start()
#define STOP_CC(x)  A_stats.##x##.stop()
#define INCR_OP(x) A_stats.##x##++
#define INCR_CNT(x,y) (A_stats.##x## += (y))
#define INIT_REC_STATS() A_stats.init_rec_stats()
#define END_REC_STATS() A_stats.end_rec_stats()
#else 
#define START_CC(x)
#define STOP_CC(x)
#define INCR_OP(x)
#define INCR_CNT(x,y)
#define INIT_REC_STATS()
#define END_REC_STATS()
#endif 

#endif // _Statistics_h
