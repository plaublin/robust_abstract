#!/bin/bash

#NUMBER_OF_CLIENTS='1 2 4 6 8 10 12 14 18 20 40 80 120 160 200'
NUMBER_OF_FAULTS='1'
NUMBER_OF_CLIENTS='1 10 20 80 100 160 200 400 800'
PERCS_OF_MALICIOUS='10 25 50 75'

REQUEST_SIZES='1024 4096 8192 16384 32768'
REPLY_SIZES='8'
TIMES='0'
PROTO=${1:-ring}

NUM_RETRIES=3
TIMEOUT_VALUE=800

OPT_FILE=ring_options.conf

./kill.sh

for NB_F in $NUMBER_OF_FAULTS; do
perl -i -ple "s/NB_FAULTS=\d+/NB_FAULTS=$NB_F/" $OPT_FILE
for TIMS in $TIMES; do
	rm -rf /tmp/manager.out-*
	for REPS in $REPLY_SIZES; do
		for REQS in $REQUEST_SIZES; do
			DID_200=0
			for NC in $NUMBER_OF_CLIENTS; do
			#for POM in $PERCS_OF_MALICIOUS; do

				if [ x"$2" == x ]; then
        	OUTNAME=${PROTO}-${TIMS}s-${NB_F}.dat
      	else
        	OUTNAME=${PROTO}-${TIMS}s-${NB_F}-$2.dat
      	fi

      	if [ -e ~/times/${OUTNAME} ]; then
        	continue;
      	fi
				#if [ -e ~/times/${PROTO}-${TIMS}s-${NB_F}.dat ]; then
					#continue;
				#fi

				#if [ $REQS -ge 4000 -a $NC -ge 50 ]; then
					#break
				#fi
				#if [ "x$PROTO" = "xquorum" -a $NC -ge 8 ]; then
					#echo "WILL EXIT AT $NC $REQS $REPS $TIMS"
					#./kill.sh
					#break;
				#fi
				#if [ "$DID_200" = "1" ]; then
					#break;
				#fi
				perl -i -ple "s/REQUEST_SIZE=\d+\s/REQUEST_SIZE=$REQS /" $OPT_FILE
				perl -i -ple "s/REPLY_SIZE=\d+\s/REPLY_SIZE=$REPS /" $OPT_FILE
				perl -i -ple "s/SLEEP_TIME=\d+\s/SLEEP_TIME=$TIMS /" $OPT_FILE
				#perl -i -ple "s/PERCENT_MALICIOUS=\d+/PERCENT_MALICIOUS=$POM/" $OPT_FILE

				EXP_TRIES=0
				while [ $EXP_TRIES -lt $NUM_RETRIES ]; do
					TMOUT_VAL=$TIMEOUT_VALUE
					if [ $NC -le 140 ]; then
						TMOUT_VAL=1200
					fi

					PROTO=$PROTO ~/bin/doalarm ${TMOUT_VAL} ./launch_ring.sh $NC
					#~/bin/doalarm ${TMOUT_VAL} ./launch_redis.sh $NC
					if [ $? -ne 0 ]; then
						echo -n ''
						./kill.sh
						echo "Restarting loop $TIMS $REPS $REQS at $NC"
						#rm /tmp/manager.out-${NC}-${REQS}-${REPS}-${POM}
						rm /tmp/manager.out-${NC}-${REQS}-${REPS}
						let "EXP_TRIES+=1"
						sleep 120
					else
						echo "::${OUTNAME} [$NC] [$REQS] [$REPS]" >> ~/times/timings.stat
            grep SAMPLES /tmp/server*.out >> ~/times/timings.stat
            grep TIMINGS /tmp/server*.out >> ~/times/timings.stat
						sleep 45
						break;
					fi
				done
					
				if [ $EXP_TRIES -ge $NUM_RETRIES ]; then
					echo "Exiting loop $TIMS $REPS $REQS at $NC"
					sleep 12
					#if [ $NC = "200" ]; then
						#continue;
					#else
						break;
					#fi
				fi
				#if [ $NC = "200" ]; then
					#DID_200=1
				#fi
				#for i in {1..4}; do
						#sar -P 0 -f ~/oprofiler/sadc.stats.$i; 
				#done|perl -anle '$k=$F[0]; shift @F; shift @F; push @{$h{$k}}, @F; END{$,=" ";$"="\t";foreach $j (keys %h){ print $j, "@{$h{$j}}";};}'|sort >~/times/stats-${PROTO}-${REQS}-${REPS}-${NC}
				#tar cvzf ~/times/tar/stats-${PROTO}-${REQS}-${REPS}-${NC}.tar.gz ~/oprofiler/sadc.stats*
				#grep Statistics /tmp/server*.out >> /tmp/manager.out-${NC}-${REQS}-${REPS}
				sleep 60
			#done
			done
		done
	done

	perl parse_manager.pl >~/times/${OUTNAME}
	#perl parse_manager_stats.pl >~/times/${PROTO}-links.dat
	sleep 10
done
done
