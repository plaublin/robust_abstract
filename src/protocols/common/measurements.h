#ifndef _MEASUREMENTS_H_
#define _MEASUREMENTS_H_

//#define DO_TIMINGS

//#define DO_TIMINGS_TICKS
//#define DO_TIMINGS_STD

#ifndef DO_TIMINGS
#undef DO_TIMINGS_TICKS
#undef DO_TIMINGS_STD
#endif

//#include <assert.h>

#ifdef DO_TIMINGS_STD
#define ENTRY_TIMES_SIZE 8
extern struct timeval entry_time[ENTRY_TIMES_SIZE];
extern bool entry_time_valid[ENTRY_TIMES_SIZE];
extern long long total_sum_sec[ENTRY_TIMES_SIZE];
extern long long total_sum_usec[ENTRY_TIMES_SIZE];
extern long long total_samples[ENTRY_TIMES_SIZE];

#define ENTRY_TIME ENTRY_TIME_POS(0)
#define EXIT_TIME EXIT_TIME_POS(0)
#define KILL_ENTRY_TIME KILL_ENTRY_TIME_POS(0)

#define ENTRY_TIME_POS(position) do {\
    gettimeofday(&entry_time[position], NULL); \
    entry_time_valid[position] = true; \
} while (0);
#define EXIT_TIME_POS(position) do {\
    if (entry_time_valid[position]) { \
	entry_time_valid[position] = false; \
	struct timeval exit_time; \
	gettimeofday(&exit_time, NULL); \
	total_sum_sec[position] += (exit_time.tv_sec - entry_time[position].tv_sec); \
	total_sum_usec[position] += (exit_time.tv_usec - entry_time[position].tv_usec); \
	while (total_sum_usec[position] > 1000000) { \
	    total_sum_usec[position] -= 1000000; \
	    total_sum_sec[position] ++; \
	} \
	total_samples[position]++; \
    }} while (0);
#define KILL_ENTRY_TIME_POS(position) do {\
    entry_time_valid[position] = false; \
} while (0);

#ifndef _MEASUREMENTS_ID_
#define _MEASUREMENTS_ID_ -1
#endif

#define REPORT_TIMINGS REPORT_TIMINGS_RANGE(0,ENTRY_TIMES_SIZE)

#define REPORT_TIMINGS_RANGE(start,end) do {\
	for (int i=start;i<end;i++) {\
		if (total_samples[i]) { \
			fprintf(stderr, "TIMINGS[%d][%d]: total samples: %lld, total time: %lld + %lld, average time usec: %g\n", \
					_MEASUREMENTS_ID_, i, total_samples[i], total_sum_sec[i], total_sum_usec[i], 1.0*(total_sum_sec[i]*1000000.0 + total_sum_usec[i])/total_samples[i]); \
		}}} while(0);

#elif defined DO_TIMINGS_TICKS

typedef unsigned long long ticks;

static __inline__ ticks getticks(void)
{
    ticks ret;

    __asm__ __volatile__("rdtsc": "=A" (ret));
    /* no input, nothing else clobbered */
    return ret;
}

#define ENTRY_TIMES_SIZE 8
extern ticks entry_time[ENTRY_TIMES_SIZE];
extern bool entry_time_valid[ENTRY_TIMES_SIZE];
extern ticks total_sum_ticks[ENTRY_TIMES_SIZE];
extern long long total_samples[ENTRY_TIMES_SIZE];

#define ENTRY_TIME ENTRY_TIME_POS(0)
#define EXIT_TIME EXIT_TIME_POS(0)
#define KILL_ENTRY_TIME KILL_ENTRY_TIME_POS(0)

#define ENTRY_TIME_POS(position) do {\
    entry_time[position] = getticks(); \
    entry_time_valid[position] = true; \
} while (0);
#define EXIT_TIME_POS(position) do {\
    if (entry_time_valid[position]) { \
	entry_time_valid[position] = false; \
	ticks exit_time = getticks(); \
	total_sum_ticks[position] += (exit_time - entry_time[position]); \
	total_samples[position]++; \
    }} while (0);
#define KILL_ENTRY_TIME_POS(position) do {\
    entry_time_valid[position] = false; \
} while (0);

#ifndef _MEASUREMENTS_ID_
#define _MEASUREMENTS_ID_ -1
#endif

#define REPORT_TIMINGS REPORT_TIMINGS_RANGE(0,ENTRY_TIMES_SIZE)

#define REPORT_TIMINGS_RANGE(start,end) do {\
	for (int i=start;i<end;i++) {\
		if (total_samples[i]) { \
			fprintf(stderr, "TIMINGS[%d][%d]: total samples: %llu, total ticks: %llu, average ticks: %llu\n", \
					_MEASUREMENTS_ID_, i, total_samples[i], total_sum_ticks[i], total_sum_ticks[i]/total_samples[i]); \
		}}} while(0);


#else
#define ENTRY_TIME
#define EXIT_TIME
#define KILL_ENTRY_TIME
#define REPORT_TIMINGS
#define REPORT_TIMINGS_RANGE(x,y)
#define ENTRY_TIME_POS(X)
#define EXIT_TIME_POS(X)
#define KILL_ENTRY_TIME_POS(X)
#endif

#endif

