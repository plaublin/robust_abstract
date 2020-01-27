#!/bin/bash

REQUEST_SIZES='4096'
#RATES='250 500 600 700 750 800 850 900 950 1000 1050 1100 1200 1300 1400 1500'
RATES='80 85 90 95 100 105 110' # in Mbps
#RATES='85 95 105' # in Mbps

NUM_RETRIES=2
TIMEOUT_VALUE=500
REPETITIONS=5
NUM_R='10 8 6 4'

OPT_FILE=ring_options_mcast.conf

./kill_mcast.sh

for NC in $NUM_R; do
for MRATE in $RATES; do
	RATE=$(( MRATE * 1000000 / (8 * (REQUEST_SIZES+120) * (NC-1)) ))
	echo Rate is $RATE
	TOT_REQ=$(( 1000000 / NC ))
	echo Total number of requests per node is $TOT_REQ
	perl -i -ple "s/RATE=\d+/RATE=$RATE/" $OPT_FILE
	perl -i -ple "s/NUM_MESSAGES_PER_BURST=\d+/NUM_MESSAGES_PER_BURST=$TOT_REQ/" $OPT_FILE
	for TRY in `seq 1 $REPETITIONS`; do
		echo REPETITION $TRY
		EXP_TRIES=0
		while [ $EXP_TRIES -lt $NUM_RETRIES ]; do
			ALARM_TIMEOUT_VALUE=${TIMEOUT_VALUE}
			~/bin/doalarm ${ALARM_TIMEOUT_VALUE} ./launch_mcast.sh $NC
			if [ $? -ne 0 ]; then
				echo -n ''
				./kill_mcast.sh
				echo "Restarting loop $RATE $NC at $TRY"
				rm /tmp/server.*.out
				let "EXP_TRIES+=1"
			else
				sleep 10
				break;
			fi
		done

		if [ $EXP_TRIES -ge $NUM_RETRIES ]; then
			echo "Exiting loop $TIMS $REPS $REQS at $NC"
			sleep 10
			break 2;
		fi
		sleep 10
	done
	sleep 1
done
done
