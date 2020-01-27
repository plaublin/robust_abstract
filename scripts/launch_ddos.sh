#!/bin/bash

REQUEST_SIZES='4096'
RATES='250 260 270 280 290 300 310 320 330 340 350'

NUM_RETRIES=2
TIMEOUT_VALUE=500

OPT_FILE=ring_options.conf
TOTAL_REQUESTS=100000

./kill_mcast.sh

for RATE in $RATES; do
	perl -i -ple "s/RATE=\d+\s/RATE=$RATE /" $OPT_FILE
	for NC in `seq 1 13`; do
		NMPB=$(( $TOTAL_REQUESTS/$NC ))
		perl -i -ple "s/NUM_MESSAGES_PER_BURST=\d+/NUM_MESSAGES_PER_BURST=$NMPB/" $OPT_FILE

		for TRY in `seq 1 $REPETITIONS`; do
			echo REPETITION $TRY
			EXP_TRIES=0
			while [ $EXP_TRIES -lt $NUM_RETRIES ]; do
				ALARM_TIMEOUT_VALUE=${TIMEOUT_VALUE}
				~/bin/doalarm ${ALARM_TIMEOUT_VALUE} ./launch_ddos_exp.sh $NC
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
