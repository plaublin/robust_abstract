#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/param.h>
#include <unistd.h>
#include <time.h>
#include <sched.h>

#include "th_assert.h"
#include "Traces.h"
#include "libmodular_BFT.h"

#include "benchmarks.h"

int reply_size;
long sleep_time_us;

//#define _R_M_MEASURE_SLEEP_ACCURACY

#ifdef _R_M_MEASURE_SLEEP_ACCURACY
static struct timeval entry_time;
	static struct timeval exit_time;
static long long total_sum_sec = 0;
static long long total_sum_usec = 0;
static long long total_samples = 0;
static long long sums_s[10000];
static long long sums_us[10000];

#include <math.h>

void kill_this_replica(int sig) {
    double avg = 0;
    double sd = 0;
    for (int i = 0; i <total_samples; i++) {
    	avg += 1.0*(sums_s[i]*1000000.0+sums_us[i]);
    }
    avg = avg/total_samples;
    for (int i = 0; i<total_samples; i++) {
    	sd += (1.0*(sums_s[i]*1000000.0+sums_us[i])-avg)*(1.0*(sums_s[i]*1000000.0+sums_us[i])-avg);
    }
    sd = sqrt(sd/(total_samples-1));
    fprintf(stderr, "TIMINGS[%d]: total samples: %lld, total time: %g + %g\n",
		-1, total_samples, avg, sd);
}
#endif

typedef unsigned long long ticks;

static __inline__ __volatile__ ticks getticks(void)
{
    ticks ret;

    __asm__ __volatile__("rdtsc": "=A" (ret));
    /* no input, nothing else clobbered */
    return ret;
}

unsigned long long CPU_SPEED_KHZ; // or get it from /proc/cpuinfo

void init_clock_khz() {
  struct timeval t0,t1;
  
  ticks c0 = getticks();
  gettimeofday(&t0, 0);
  sleep(1);
  ticks c1 = getticks();
  gettimeofday(&t1, 0);
    
  CPU_SPEED_KHZ = 1000*(c1-c0)/((t1.tv_sec-t0.tv_sec)*1000000+t1.tv_usec-t0.tv_usec);
  fprintf(stderr, "replica_main.cc: Clock speed set to %d kHz\n", CPU_SPEED_KHZ);
}

// Service specific functions.
int exec_command(Byz_req *inb, Byz_rep *outb, Byz_buffer *non_det, int client,
		bool ro)
{
	//fprintf(stderr, "replica_main.cc: We are executing a request. The client is %d, size %d\n", client, reply_size);
	bzero(outb->contents, reply_size);
	//if (reply_size >= sizeof(Seqno))
	    //memcpy(outb->contents, (char*)non_det, sizeof(Seqno));

	outb->size = reply_size;

	// now, it is time to sleep.
	if (sleep_time_us != 0) {
#ifdef _R_M_MEASURE_SLEEP_ACCURACY
	    gettimeofday(&entry_time, NULL);
#endif
	    ticks start = getticks();
	    ticks end = start+(sleep_time_us*CPU_SPEED_KHZ)/1000UL;
#if 0
	    struct timespec tts;
	    tts.tv_sec = 0;
	    tts.tv_nsec = sleep_time_us;
#endif
	    //nanosleep(&tts, NULL);
	    while (getticks() <= end) {
		__asm__ ("nop;nop;nop;nop;nop");
	    }

#ifdef _R_M_MEASURE_SLEEP_ACCURACY
	    gettimeofday(&exit_time, NULL);
	    //sums_us[total_samples] = end-start;
	    //sums_s[total_samples]  =0;
	    sums_s[total_samples] = (exit_time.tv_sec - entry_time.tv_sec);
	    sums_us[total_samples] = (exit_time.tv_usec - entry_time.tv_usec);
	    total_samples++;
#endif
	}
	return 0;
}

int main(int argc, char **argv)
{
	short port_quorum;
	short port_pbft;
	short port_chain;
    short port_zlight;
	char config_quorum[PATH_MAX];
	char config_pbft[PATH_MAX];
	char config_priv_pbft[PATH_MAX];
	char config_priv_tmp_pbft[PATH_MAX];
	char config_chain[PATH_MAX];
    char config_zlight[PATH_MAX];
	char host_name[MAXHOSTNAMELEN+1];
	int argNb = 1;
	ssize_t init_history_size = 0;
	int percent_misses = 0;

	strcpy(host_name, argv[argNb++]);
	port_quorum = atoi(argv[argNb++]);
	port_pbft = atoi(argv[argNb++]);
	port_chain = atoi(argv[argNb++]);
    port_zlight = atoi(argv[argNb++]);
	reply_size = atoi(argv[argNb++]);
	sleep_time_us = atol(argv[argNb++]);
	strcpy(config_quorum, argv[argNb++]);
    strcpy(config_pbft, argv[argNb++]);
    strcpy(config_zlight, argv[argNb++]);
	strcpy(config_priv_tmp_pbft, argv[argNb++]);
	strcpy(config_chain, argv[argNb++]);
	init_history_size = atoi(argv[argNb++]);
	percent_misses= atoi(argv[argNb++]);

	init_clock_khz();

	// Priting parameters
	fprintf(stderr, "********************************************\n");
	fprintf(stderr, "*       Replica compilation date: %s @ %s  *\n", __DATE__, __TIME__);
	fprintf(stderr, "*             Replica parameters           *\n");
	fprintf(stderr, "********************************************\n");
	fprintf(
	stderr,
	"Host name = %s\nPort_quorum = %d\nPort_pbft = %d\nPort_chain = %d\nPort_zlight = %d\nReply size = %d \nConfiguration_quorum file = %s\nConfiguration_chain file = %s\nConfig_zlight directory = %s\nConfiguration_pbft file = %s\nConfig_private_pbft directory = %s\n",
	host_name, port_quorum, port_pbft, port_chain, port_zlight, reply_size,
	config_quorum, config_chain, config_zlight, config_pbft, config_priv_tmp_pbft);
	fprintf(stderr, "********************************************\n\n");

	char hname[MAXHOSTNAMELEN];
	gethostname(hname, MAXHOSTNAMELEN);
	// Try to open default file
	sprintf(config_priv_pbft, "%s/%s", config_priv_tmp_pbft, hname);

	int mem_size = 2048 * 8192;
	char *mem = (char*) valloc(mem_size);
	bzero(mem, mem_size);

#ifdef _R_M_MEASURE_SLEEP_ACCURACY
	struct sigaction act;
        act.sa_handler = kill_this_replica;
        sigemptyset (&act.sa_mask);
        act.sa_flags = 0;
        sigaction (SIGHUP, &act, NULL);
#endif

#if 0
	struct sched_param schedparam;
	schedparam.sched_priority = 50;
	if (sched_setscheduler(0, SCHED_RR, &schedparam)) {
	    perror("Problem setting schedule policy");
	    exit(1);
	}
#endif

	MBFT_init_replica(host_name, config_quorum, config_pbft,
			config_priv_pbft, config_chain, config_zlight, mem, mem_size, exec_command,
			port_quorum, port_pbft, port_chain, port_zlight);
}

