#ifndef _A_ITimer_h
#define _A_ITimer_h 1

#include <sys/time.h>
#include "Array.h"
#include "types.h"
#include "A_Time.h"

class A_ITimer {
  // 
  // Interface to a real time interval timer that can be in three
  // states running, stopped and expired. A timer is initially stopped
  // and start changes its state to running. If the timer is not
  // explicitly stopped (by calling stop) before time t elapses, the
  // timer expires, and the handler is called the next time
  // handle_timeouts is called.
  //

public:
  A_ITimer(int t, void (*h) ());
  // Effects: Creates a timer that expires after running for time "t"
  // msecs and calls handler "h" when it expires.

  ~A_ITimer();
  // Effects: Deletes a timer.
  
  void start();
  // Effects: If state is stopped, starts the timer. Otherwise, it has
  // no effect.

  void restart();
  // Effects: Like start, but also starts the timer if state is expired.

  void adjust(int t);
  // Effects: Adjusts the timeout period to "t" msecs.

  int get_period();
  // Effects: Return the current period (in msecs)

  void stop();
  // Effects: If state is running, stops the timer. Otherwise, it has
  // no effect.

  void restop();
  // Effects: Like stop, but also changes state to stopped if state is expired.

  bool is_expired();
  //Effects: returns true if the state is "expired" 
  //         returns false otherwise.

#ifdef USE_GETTIMEOFDAY
  static void handle_timeouts();
  // Effects: Calls handlers for A_ITimer instances that have expired.
#else
  inline static void handle_timeouts() {
    A_Time current = A_rdtsc();
    if (current < min_deadline)
      return;
    _handle_timeouts(current);
  }
#endif

private:
  enum {stopped, running, expired} state;
  void (*handler)();

  A_Time deadline;
  A_Time period;

#ifndef USE_GETTIMEOFDAY
  // Use cycle counter
  static A_Time min_deadline;
  static void _handle_timeouts(A_Time current);
#endif // USE_GETTIMEOFDAY

  static Array<A_ITimer*> timers;
};

#endif // _A_ITimer_h
