#ifndef _A_parameters_h
#define _A_parameters_h 1

#include "parameters.h"

//! JC: This controls batching, and is the maximum number of messages that
//! can be outstanding at any one time. ie. with CW=1, can't send a pp unless previous
//! operation has executed.
const int congestion_window = 1;

/* !!! Note on congestion_window !!!
 *
 * It would really be nice to use a congestion_window higher than one.
 * The main problem is that a congestion window higher than 1 breaks something
 * inside the state transfer black magic, and view change becomes unstable.
 *
 * I've also observed a memory corruption detected by glibc wile messing
 * around with congestion_window = 2, but this may well be caused by our 
 * modified memory allocator. (even though I have no idea why this happens
 * only with congestion_window > 1)
 *
 * 1 is a safe value, everithing else is not.
 *
 * UPDATE: after having implemented shared memory communication among threads,
 * we are able to use congestion windows higher than 1.
 * Don't know why...
 *
 */

//const int circular_buffer_size = 2000;
const int circular_buffer_size = 15000;

/* ---- THROUGHPUT ADAPTIVENESS ---- */

#define THROUGHPUT_ADAPTIVE 1 //forces a view change if replicas do not observe a satisfactory throughput
//PARAMETERS:

//const float req_throughput_init = 0.0001;
//const float req_throughput_init = 0.0300;
const float req_throughput_init = 0.0060;
//the required throughput is initialized to req_throughout_init reqs/ns.
//e.g. 0.0001 -> 100 req/s

const float req_throughput_increment_init = 0.0001;
//if the observed throughput is higher then the required throughput, the
//required throughput is incremented. this increment is initialized at 
//req_throughout_increment_init reqs/ns. [this value is used only for the first
//check, when we have no measure for the previous throughput]

//initial value: const float req_throughput_increment_scaling_factor = 0.005;
const float req_throughput_increment_scaling_factor = 0.01;
//if the observed throughput is higher than the required throughput,
//the required throughput is incremented by itself*req_throughput_increment_scaling_factor.
//e.g., 0.005 means that the req throughput is incremented by 0.5% every checkpoint,
//until it exceeds the observed throughput 

//initial value: const float new_req_throughput_scaling_factor = 0.80;
const float new_req_throughput_scaling_factor = 0.90;
//after a view change, the required throughput is set as
//new_req_throughput_scaling_factor * (highest of the last throughputs measured for each A_replica)
//e.g., 0.8 means that the new requirement will be the 80% of the highest throughput
//among the last throughput observed by all the replicas.

//initial value: const int increment_timer_duration = 10000;
const int increment_timer_duration = 5000;
//right after a view change, wait increment_timer_duration ms before starting
//to increment the required throughput

const int throughput_timer_duration = 1;
//1 ms, this timer has to be fast... 
//it is just an interface to call send_view_change
//as soon as it is safe to do so

const int number_of_checkpoints_before_adpating = 1;
// previously it was hard-coded. Move it there so that we can
// easily change the value (with sed --in-place for instance)

/* ---- END THROUGHPUT ADAPTIVENESS ---- */

/* ---- DELAY ADAPTIVENESS (aka pre_prepare adaptiveness) ---- */

#define DELAY_ADAPTIVE 1 // forces a view change if replicas do not receive pre_prepares often enough
//delay adaptiveness is used to guarantee that we reach a checkpoint in a determined interval of time

const int expected_pre_prepares = 12;
const int pre_prepare_timer_duration = 480;
// trigger a view change if I have not received at least expected_pre_prepares
// pre_prepare messages in the last pre_prepare_timer_duration ms 
// N.B. atfer triggering a view change due to the preprepare timer, its period is doubled
// the period is set back to the pre_prepare_timer_duration if the timer expires without
// triggering a view change

/* --- END DELAY ADAPTIVENESS (aka pre_prepare adaptiveness) ---- */

/* ----  FAIRNESS ADAPTIVENESS  ---- */
#define FAIRNESS_ADAPTIVE 1 //forces a view change if the progress is too unfair
const int fairness_multiplier = 2;

/* ---- END FAIRNESS ADAPTIVENESS  ---- */

/* ----  REPLICA FLOOD PROTECTION  ---- */

#define REPLICA_FLOOD_PROTECTION 1 //enable the (soft) protection against A_replica flood
const int flood_detection_factor = 20;
//trigger flood protection for A_replica x if messages sent by A_replica x are > than max(messages sent from other replicas)*flood_protection_factor 

const int check_rmcount = 2000;
// check for a possible flood every check_rmcount A_replica messages

// const float rmcount_threshold = 0.5; no more used
// a flood is detected if one A_replica is responsible for
// rmcount_threshold*check_rmcount of the last check_rmcount
// messages.
// e.g. 0.6 means that if a single A_replica generated 60%
// or more of the last check_rmcount, that A_replica is considered
// as a flooder A_replica

const int flood_protected_views = 500;
//the flooded A_replica is unmuted after
//flood_protected_views views

/* ----  END REPLICA FLOOD PROTECTION  ---- */

const int batch_size_limit = 1000;

/* Define SIGN_REQUESTS to force all requests to be signed by the client */
#define SIGN_REQUESTS 
//#define SIMULATE_SIGS

// clients do not sign the requests
#define NO_CLIENT_SIGNATURES
//#undef NO_CLIENT_SIGNATURES

#undef COSTLY_EXECUTION

/* size of the A_replica's UDP buffers */
const int param_snd_buffer_size = 4000000;
const int param_rcv_buffer_size = 4000000;

/* client timeout for retransmission of requests, in ms, default is 150 */
#ifdef COSTLY_EXECUTION
const int client_retrans_timeout = 150;
#else
const int client_retrans_timeout = 150;
#endif

/* TCP port used by the replicas when bootstrapping */
#define BOOTSTRAP_PORT 6000

// set it to true in order to deactivate the view changes
const bool view_change_deactivated = false;

// If defined, then use the original code to adapt the throughput.
// Otherwise use the code written by us, which fits more with the paper
// the original code monitors the throughput between the last 2 checkpoints
// and can increment the expectations by a jump.

//#define R_AARDVARK
#ifdef R_AARDVARK
#define ORIGINAL_AARDVARK_THROUGHPUT_ADAPTIVE
#endif

// We always use E_Aardvark since, with a window size of 1, it fits more with the paper.
#define E_AARDVARK
#ifdef E_AARDVARK
#undef ORIGINAL_AARDVARK_THROUGHPUT_ADAPTIVE
#endif


// define this macro if you want the observed and expected throughput to be computed periodically
// THROUGHPUT_ADAPTIVE must be defined
#undef THROUGHPUT_ADAPTIVE_PERIODIC_COMPUTATION

// period at which the A_replica computes the expectations, in ms.
const int compute_expectations_period = 160;

/* ----  FLUCTUATING LOAD ATTACK  ---- */

// Define it for the fluctuating load attack.
#undef RRBFT_ATTACK

// id of the malicious primary
#define MALICIOUS_PRIMARY_ID 0

// max latency between the send of a PP and the reception by the replicas (fault-free case), in ms.
// Depends on the network and on the speed of your machine
#define MAX_LATENCY_PP_SEND_RECV 2.0

// max latency between the send of a PP and the execution of the requests by the replicas (fault-free case), in ms.
// Depends on the network and on the speed of your machine
#define MAX_LATENCY_PP_SEND_EXEC 5.0

// max number of logged sleeping times.
#define MAX_NB_SLEEPING_TIME_LOGGED 1000000

/* ---- END FLUCTUATING LOAD ATTACK  ---- */


// If defined, then vtimer timeout is multiplied by 2 every time the timer is triggered
// and reset to its initial value when a new view starts.
#undef EXPONENTIAL_VTIMER_TIMEOUT

// If defined, then replace the test n_le >= n-f by n_le >= n-f-1 in A_NV_info::check_comp()
// solves a bug with the A_replica flooding attack (only?)
#undef N_LE_GT_N_F_1

// If defined, then when A_replica 0 is the primary, it sends the PP to 2f replicas only
#undef NEW_BIG_MAC_ATTACK

// the expectations are display periodically, every PERIODIC_EXPECTATIONS_DISPLAY checkpoints,
// or when a view change occurs because of the expectations.
// If THROUGHPUT_ADAPTIVE_PERIODIC_COMPUTATION, then this is every
//         PERIODIC_EXPECTATIONS_DISPLAY * compute_expectations_period ms.
#define PERIODIC_EXPECTATIONS_DISPLAY 20

// how often does the primary display the batch size?
// default size was 10000
const int periodic_batch_size_display = 10000;

/****************************** WHEN USING A REAL TRACE ******************************/
// Do the replicas periodically measure the throughput?
// What is the period (in ms)?
// What is the max nb of measures?
#undef PERIODICALLY_MEASURE_THROUGHPUT
#define PRINT_THROUGHPUT_PERIOD 1000
#define MAX_NB_MEASURES 2000


/****************************** AARDVARK SWITCHING ******************************/
#define AARDVARK_DO_SWITCHING

//Number of requests executed in Aardvark before switching to the next protocol
//#define AARDVARK_NB_REQS_BEFORE_SWITCHING 1000000
#define AARDVARK_NB_REQS_BEFORE_SWITCHING 200000

#endif // _parameters_h
