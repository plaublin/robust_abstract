#include "measurements.h"

#ifdef DO_TIMINGS_STD

struct timeval entry_time[ENTRY_TIMES_SIZE];
bool entry_time_valid[ENTRY_TIMES_SIZE] = {false,false,false,false};
long long total_sum_sec[ENTRY_TIMES_SIZE] = {0,0,0,0};
long long total_sum_usec[ENTRY_TIMES_SIZE] = {0,0,0,0};
long long total_samples[ENTRY_TIMES_SIZE] = {0,0,0,0};

#elif defined DO_TIMINGS_TICKS

ticks entry_time[ENTRY_TIMES_SIZE];
bool entry_time_valid[ENTRY_TIMES_SIZE] = {false, false, false, false, false, false, false, false};
ticks total_sum_ticks[ENTRY_TIMES_SIZE] = {0,0,0,0,0,0,0,0};
long long total_samples[ENTRY_TIMES_SIZE] = {0,0,0,0,0,0,0,0};

#endif

