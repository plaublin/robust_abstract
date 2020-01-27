#!/bin/bash

NUMBER_OF_CLIENTS='2'
#NUMBER_OF_CLIENTS='80'
#NUMBER_OF_CLIENTS='1 4'
REPETITIONS=5

REQUEST_SIZES='8'
#REQUEST_SIZES='8'
REPLY_SIZES='8'
TIMES='0'
PROTO=${1:-ring}

NUM_RETRIES=2
TIMEOUT_VALUE=500
TOTAL_REQUESTS=100000

OPT_FILE=ring_options.conf

./kill.sh

for TIMS in $TIMES; do
	rm -rf /tmp/manager.out-*
	for REPS in $REPLY_SIZES; do
		for REQS in $REQUEST_SIZES; do
			for NC in $NUMBER_OF_CLIENTS; do
				if [ -e ~/times/cpu/ta-${PROTO}-${NC}-${REQS}.data ]; then
					continue;
				fi
				if [ "x$PROTO" = "xquorum" -a $NC -ge 4 ]; then
					echo "WILL EXIT AT $NC $REQS $REPS $TIMS"
					./kill.sh
					break;
				fi
				NMPB=$(( $TOTAL_REQUESTS/$NC ))
				perl -i -ple "s/REQUEST_SIZE=\d+\s/REQUEST_SIZE=$REQS /" $OPT_FILE
				perl -i -ple "s/REPLY_SIZE=\d+\s/REPLY_SIZE=$REPS /" $OPT_FILE
				perl -i -ple "s/SLEEP_TIME=\d+\s/SLEEP_TIME=$TIMS /" $OPT_FILE
				perl -i -ple "s/NUM_MESSAGES_PER_BURST=\d+/NUM_MESSAGES_PER_BURST=$NMPB/" $OPT_FILE

				for TRY in `seq 1 $REPETITIONS`; do
					echo REPETITION $TRY
					EXP_TRIES=0
					while [ $EXP_TRIES -lt $NUM_RETRIES ]; do
						ALARM_TIMEOUT_VALUE=${TIMEOUT_VALUE}
						if [ $NC = "1" ]; then
							ALARM_TIMEOUT_VALUE=500
						fi
						rm /tmp/server.*.out
						~/bin/doalarm ${ALARM_TIMEOUT_VALUE} ./launch_ring.sh $NC
						if [ $? -ne 0 ]; then
							echo -n ''
							./kill.sh
							echo "Restarting loop $TIMS $REPS $REQS at $NC"
							rm /tmp/server.*.out
							let "EXP_TRIES+=1"
						else
							grep TIMINGS /tmp/server.*.out >> ~/times/cpu/ta-${PROTO}-${NC}-${REQS}.data
							echo '>>>>' >> ~/times/cpu/ta-${PROTO}-${NC}-${REQS}.data
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
			done
		done
	done

	#perl parse_manager.pl >~/times/${PROTO}-${TIMS}s.dat
	sleep 10
done
