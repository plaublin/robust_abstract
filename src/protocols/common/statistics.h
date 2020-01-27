#ifndef _STATISTICS_H
#define _STATISTICS_H


#ifdef DO_STATISTICS
// client
#define STAT_C_PREQ_POS 0
#define STAT_C_POSTQ_POS 1
// replica: {predecessor, successor}
#define STAT_R_PREQ_POS 2
#define STAT_R_POSTQ_POS 3

// how many readings
#define STAT_SAMPLES 4
// offset where previous data starts
#define STAT_OFFSET 4
#define STAT_SIZE (4*sizeof(long long))

// x is {C,R}, p is {PRE,POST}, s is size
#define UPDATE_IN(x,p,s) do { reqs_in[STAT_##x##_##p##Q_POS]++; bytes_in[STAT_##x##_##p##Q_POS] += s; } while(0);
#define UPDATE_OUT(x,p,s) do { reqs_out[STAT_##x##_##p##Q_POS]++; bytes_out[STAT_##x##_##p##Q_POS] += s; } while(0);

#define STAT_DO_DIFF(name) \
    long long name##_diff[STAT_SAMPLES];\
    for (int ssi=0;ssi<STAT_SAMPLES;ssi++) {\
	name##_diff[ssi] = name[ssi] - name[ssi+STAT_OFFSET];\
    }

#define STAT_MOVE_TO_PREV(name) \
    memcpy(name+STAT_OFFSET, name, STAT_SIZE);

// scripts should do the math and calculate total...
#define STAT_PRINT_STATS(name) \
	    fprintf(statfd, \
		    "R_Replica: [" #name "] client pre-in: %lld, post-in: %lld, pre-out: %lld; replica pre-in: %lld, post-in: %lld, pre-out: %lld;\n",\
		    name##_in_diff[STAT_C_PREQ_POS], name##_in_diff[STAT_C_POSTQ_POS], name##_out_diff[STAT_C_PREQ_POS],\
		    name##_in_diff[STAT_R_PREQ_POS], name##_in_diff[STAT_R_POSTQ_POS], name##_out_diff[STAT_R_PREQ_POS]);

#define STAT_PRINT_STATS_PER_TIME(name, time) \
	    fprintf(statfd, \
		    "R_Replica: [" #name "/s] client pre-in: %.3e, post-in: %.3e, pre-out: %.3e; replica pre-in: %.3e, post-in: %.3e, pre-out: %.3e;\n",\
		    name##_in_diff[STAT_C_PREQ_POS]/time, name##_in_diff[STAT_C_POSTQ_POS]/time, name##_out_diff[STAT_C_PREQ_POS]/time,\
		    name##_in_diff[STAT_R_PREQ_POS]/time, name##_in_diff[STAT_R_POSTQ_POS]/time, name##_out_diff[STAT_R_PREQ_POS]/time);

static long long reqs_in[2*STAT_SAMPLES];
static long long reqs_out[2*STAT_SAMPLES];
static long long bytes_in[2*STAT_SAMPLES];
static long long bytes_out[2*STAT_SAMPLES];

static double avg_tot = 0.0; // average turn-over-time
static double sum_tot = 0.0; // to count the average tot
static long long tot_samples = 0;

#define REPORT_STATS \
	fprintf(stderr, "Statistics(%d): [C] total reqs in: %lld, out: %lld\n", _MEASUREMENTS_ID_, reqs_in[STAT_C_POSTQ_POS], reqs_out[STAT_C_PREQ_POS]); \
	fprintf(stderr, "Statistics(%d): [R] total reqs in: %lld, out: %lld\n", _MEASUREMENTS_ID_, reqs_in[STAT_R_POSTQ_POS], reqs_out[STAT_R_POSTQ_POS]); \
	fprintf(stderr, "Statistics(%d): [C] total bytes in: %lld, out: %lld\n", _MEASUREMENTS_ID_, bytes_in[STAT_C_POSTQ_POS], bytes_out[STAT_C_POSTQ_POS]); \
	fprintf(stderr, "Statistics(%d): [R] total bytes in: %lld, out: %lld\n", _MEASUREMENTS_ID_, bytes_in[STAT_R_POSTQ_POS], bytes_out[STAT_R_POSTQ_POS]);

#if 0
#define REPORT_STATS \
	fprintf(stderr, "Statistics: batching %lld/%lld/%lld, piggybacking %lld/%lld, total_batched %lld\n", successful_batching, nonempty_queue_stat, tried_batching, successful_piggyback, tried_piggyback, total_reqs_batched); \
	fprintf(stderr, "Statistics: avg batch size %g\n", total_reqs_batched*1.0/successful_batching); \
	fprintf(stderr, "Statistics: total reqs in: %lld, out: %lld\n", reqs_in[STAT_C_POSTQ_POS]+reqs_in[STAT_R_POSTQ_POS], reqs_out[STAT_C_PREQ_POS]+reqs_out[STAT_R_POSTQ_POS]); \
	fprintf(stderr, "Statistics: total bytes in: %lld, out: %lld\n", bytes_in[STAT_C_POSTQ_POS]+bytes_in[STAT_R_POSTQ_POS], bytes_out[STAT_C_PREQ_POS]+bytes_out[STAT_R_POSTQ_POS]);
#endif

#else
#define UPDATE_IN(x,p,s)
#define UPDATE_OUT(x,p,s)
#define REPORT_STATS ;
#endif


#endif
