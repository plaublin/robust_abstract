#ifndef _A_Time_h
#define _A_Time_h 1

/*
 * Definitions of various types.
 */
#include <limits.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <netinet/in.h> 
#include <sys/time.h>
#include <unistd.h>

//#define USE_GETTIMEOFDAY 1

#ifdef USE_GETTIMEOFDAY

#include "th_assert.h"

typedef struct timeval A_Time;
static inline A_Time A_currentTime() {
  A_Time t;
  int ret = gettimeofday(&t, 0);
  th_assert(ret == 0, "gettimeofday failed");
  return t;
}

static inline A_Time A_zeroTime() {  
  A_Time t;
  t.tv_sec = 0;
  t.tv_usec = 0;
  return t; 
}

static inline long long A_diffTime(A_Time t1, A_Time t2) {
  // t1-t2 in microseconds.
  return (((unsigned long long)(t1.tv_sec-t2.tv_sec)) << 20) + (t1.tv_usec-t2.tv_usec);
}

static inline bool A_lessThanTime(A_Time t1, A_Time t2) {
  return t1.tv_sec < t2.tv_sec ||  
    (t1.tv_sec == t2.tv_sec &&  t1.tv_usec < t2.tv_usec);
}
#else

typedef long long A_Time;

#include "A_Cycle_counter.h"

extern long long clock_mhz;
// Clock frequency in MHz

extern void init_clock_mhz();
// Effects: Initialize "clock_mhz".

static inline A_Time A_currentTime() { return A_rdtsc(); }

static inline struct timeval A_currentTime_gettimeofday() {
  struct timeval t;
  gettimeofday(&t, 0);
  return t;
}

static inline long long A_diffTime_timeval(struct timeval t1, struct timeval t2) {
    // t1-t2 in microseconds.
    return (((unsigned long long)(t1.tv_sec-t2.tv_sec)) << 20) + (t1.tv_usec-t2.tv_usec);
}


static inline A_Time A_zeroTime() { return 0; }

/* return the difference between t1 and t2, in usec */
static inline long long A_diffTime(A_Time t1, A_Time t2) {
  return (t1-t2)/clock_mhz;
}

static inline bool A_lessThanTime(A_Time t1, A_Time t2) {
  return t1 < t2;

}

#endif

#endif // _Time_h 
