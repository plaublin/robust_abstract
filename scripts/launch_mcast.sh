#!/bin/bash

# GET THE OPTIONS FROM THE CONFIG FILE
. ring_options_mcast.conf

# VARIABLES USED IN THE SCRIPT
simultaneousSSH=30
SSH_TIMEOUT=10

REPLICA_COUNTER=0
CLIENT_COUNTER=0

NUM_PARTICIPANTS=$NUM_CLIENTS

echo -e "Deploying master on $MASTER_NAME"
echo -e "./thr_manager $NUM_PARTICIPANTS $NUM_BURSTS $NUM_MESSAGES_PER_BURST"
ssh ${MASTER_LOGIN_NAME}@$MASTER_NAME "cd BFT/src/benchmarks ; ./thr_manager $NUM_PARTICIPANTS $NUM_BURSTS $NUM_MESSAGES_PER_BURST" 2>&1 |tee /tmp/manager.out & 
THR_MANAGER_PID=$!
sleep 4

rm -rf /tmp/server*.out

while read line
do
	node=`echo $line | cut -d ' ' -f 1`
	addr=`echo $line | cut -d ' ' -f 2`
	if [[ $node =~ ($NODENAMES_REGEX).* ]]
	then
		if [ $REPLICA_COUNTER -lt $NUM_PARTICIPANTS ] 
		then
			let "REPLICA_COUNTER+=1"
			#if [ $REPLICA_COUNTER -eq 1 ]
			#then
				#echo -e "Skipping one"
			#else
			echo -e "Deploying replica $REPLICA_COUNTER on [$node]"
			echo -e "./multicast_replica ${node/-0/-1} $addr $REPLICA_COUNTER $NUM_PARTICIPANTS $MASTER_NAME $GROUP_ADDRESS $GROUP_ADDRESS_PORT $NUM_BURSTS $NUM_MESSAGES_PER_BURST $REQUEST_SIZE $RATE"
			ssh -c arcfour -4 -o Compression=no -n ${LOGIN_NAME}@${node} "cd BFT/src/benchmarks ; ./multicast_replica ${node/-0/-1} $addr $REPLICA_COUNTER $NUM_PARTICIPANTS $MASTER_NAME $GROUP_ADDRESS $GROUP_ADDRESS_PORT $NUM_BURSTS $NUM_MESSAGES_PER_BURST $REQUEST_SIZE $RATE" 2>&1 | tee /tmp/server.$node.out&
			#fi
			sleep 2
		fi
	fi
done < mcast.config 

wait $THR_MANAGER_PID

echo $RATE `grep 'received a total of' /tmp/server.*.out | perl -nle 'BEGIN{$avg=0;$n=0;@v=();}; if (m,total of (\d+)/(\d+) requests,){$avg+=($2-$1)/$2;$n++;push @v,(($2-$1)/$2);};END{$avg=$avg/$n;$s=0;foreach (@v){$s+=(($_-$avg)**2);};$s=sqrt($s/($n-1));print "Loses are [$n]: $avg $s";}'` >> ~/times/multicast.out

./kill.sh
