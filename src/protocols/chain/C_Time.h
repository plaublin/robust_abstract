#ifndef _C_Time_h
#define _C_Time_h 1

/*
 * Definitions of various types.
 */
#include <limits.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <netinet/in.h> 
#include <sys/time.h>
#include <unistd.h>

#ifdef USE_GETTIMEOFDAY

typedef struct timeval C_Time;
static inline C_Time currentC_Time() {
  C_Time t;
  int ret = gettimeofday(&t, 0);
  th_assert(ret == 0, "gettimeofday PBFT_C_failed");
  return t;
}

static inline C_Time zeroC_Time() {  
  C_Time t;
  t.tv_sec = 0;
  t.tv_usec = 0
  return t; 
}

static inline long long diffC_Time(C_Time t1, C_Time t2) {
  // t1-t2 in microseconds.
  return (((unsigned long long)(t1.tv_sec-t2.tv_sec)) << 20) + (t1.tv_usec-t2.tv_usec);
}

static inline bool lessThanC_Time(C_Time t1, C_Time t2) {
  return t1.tv_sec < t2.tv_sec ||  
    (t1.tv_sec == t2.tv_sec &&  t1.tv_usec < t2.tv_usec);
}
#else

typedef long long C_Time;

#include "C_Cycle_counter.h"

extern unsigned long long clock_mhz;
// Clock frequency in MHz

extern void init_clock_mhz();
// Effects: Initialize "clock_mhz".

static inline C_Time currentC_Time() { return C_rdtsc(); }

static inline C_Time zeroC_Time() { return 0; }

static inline long long diffC_Time(C_Time t1, C_Time t2) {
  return (t1-t2)/clock_mhz;
}

static inline bool lessThanC_Time(C_Time t1, C_Time t2) {
  return t1 < t2;

}

#endif

#endif // _C_Time_h 
