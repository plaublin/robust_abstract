#include <signal.h>
#include "th_assert.h"
#include "A_ITimer.h"
#include "types.h"

#include "Array.h"


#ifndef USE_GETTIMEOFDAY
#include "A_Cycle_counter.h"
#endif // USE_GETTIMEOFDAY

Array<A_ITimer*> A_ITimer::timers;

#ifndef USE_GETTIMEOFDAY
A_Time A_ITimer::min_deadline = Long_max;
#endif


A_ITimer::A_ITimer(int t, void (*h) ()) {
  state = stopped;

#ifdef USE_GETTIMEOFDAY
  period.tv_sec = t/1000;
  period.tv_usec =  (t%1000)*1000;
#else
  period = t*clock_mhz*1000;
#endif // USE_GETTIMEOFDAY

  handler = h;
  timers.append(this);
}


A_ITimer::~A_ITimer() {
  for (int i=0; i < timers.size(); i++) {
    if (timers[i] == this) {
      timers[i] = timers.high();
      timers.remove();
      break;
    }
  }
}

bool A_ITimer::is_expired(){
  if (state == expired)
    return true;
  else
    return false;
}

void A_ITimer::start() {
  if (state != stopped)  return;
  restart();
}


void A_ITimer::restart() {
  //if (state != stopped && state != expired)  return;

  state = running;

#ifdef USE_GETTIMEOFDAY
  int ret = gettimeofday(&deadline, 0);
  th_assert(ret == 0, "gettimeofday failed");

  deadline.tv_sec += period.tv_sec;
  deadline.tv_usec += period.tv_usec;

  // adjust for overflows:
  deadline.tv_sec += deadline.tv_usec/1000000;
  deadline.tv_usec = deadline.tv_usec%1000000;
#else 
  deadline = A_rdtsc();
  deadline += period;

  if (deadline < min_deadline)
    min_deadline = deadline;
#endif // USE_GETTIMEOFDAY
}


void A_ITimer::adjust(int t) {
#ifdef USE_GETTIMEOFDAY
  period.tv_sec = t/1000;
  period.tv_usec =  (t%1000)*1000;
#else 
  period = t*clock_mhz*1000;
#endif // USE_GETTIMEOFDAY
}

int A_ITimer::get_period() {
#ifdef USE_GETTIMEOFDAY
  return (period.tv_sec*1000 + period.tv_usec/1000);
#else
  return period/(clock_mhz*1000);
#endif //USE_GETTIMEOFDAY
}	


void A_ITimer::stop() {
  if (state != running) return;

  state = stopped;
}


void A_ITimer::restop() {
  state = stopped;
}


#ifdef USE_GETTIMEOFDAY

inline bool less_than(struct timeval t1, struct timeval t2) {
  return t1.tv_sec < t2.tv_sec ||  (t1.tv_sec == t2.tv_sec &&  t1.tv_usec < t2.tv_usec);
}

void A_ITimer::handle_timeouts() {
  struct timeval current;
  int ret = gettimeofday(&current, 0);
  th_assert(ret == 0, "gettimeofday failed");
  for (int i=0; i < timers.size(); i++) {
    A_ITimer *timer = timers[i];
    if (timer->state == running && less_than(timer->deadline, current)) {
      timer->state = expired;
      timer->handler();
    }
  }
}
#else
void A_ITimer::_handle_timeouts(A_Time current) {
  min_deadline = 9223372036854775807LL;

  for (int i=0; i < timers.size(); i++) {
    A_ITimer *timer = timers[i];
    if (timer->state == running) {
      if (timer->deadline < current) {
	timer->state = expired;
	timer->handler();
      } else {
	if (timer->deadline < min_deadline)
	  min_deadline = timer->deadline;
      }
    }
  }
}
#endif // USE_GETTIMEOFDAY



