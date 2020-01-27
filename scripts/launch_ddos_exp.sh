#!/bin/bash

# GET THE OPTIONS FROM THE CONFIG FILE
. ring_options.conf

# VARIABLES USED IN THE SCRIPT
simultaneousSSH=30
SSH_TIMEOUT=10

REPLICA_COUNTER=0
CLIENT_COUNTER=0

DO_DEBUG=0

echo -e "Deploying master on $MASTER_NAME"
echo -e "./thr_manager $NUM_CLIENTS $NUM_BURSTS $NUM_MESSAGES_PER_BURST"
ssh ${MASTER_LOGIN_NAME}@$MASTER_NAME "cd BFT/src/benchmarks ; ./thr_manager $NUM_CLIENTS $NUM_BURSTS $NUM_MESSAGES_PER_BURST" 2>&1 |tee /tmp/manager.out-${NUM_CLIENTS}-${REQUEST_SIZE}-${REPLY_SIZE} & 
THR_MANAGER_PID=$!
sleep 2

NUM_REPLICAS=1
SERVER_ADDR=""
NUM_PARTICIPANTS=$NUM_CLIENTS

while read line
do
	node=`echo $line | cut -d ' ' -f 1`
	addr=`echo $line | cut -d ' ' -f 2`
	if [[ $node =~ ($NODENAMES_REGEX).* ]]
	then
		if [ $REPLICA_COUNTER -lt $NUM_REPLICAS ] 
		then
			SERVER_ADDR=$addr
			let "REPLICA_COUNTER+=1"
			echo -e "Deploying replica $REPLICA_COUNTER on [$node]"
			echo -e "./ddos_server ${node/-0/-0} $addr $NUM_PARTICIPANTS $GROUP_ADDRESS_PORT $NUM_BURSTS $NUM_MESSAGES_PER_BURST $REQUEST_SIZE"
			ssh -c arcfour -4 -o Compression=no -n ${LOGIN_NAME}@${node} "cd BFT/src/benchmarks ; ./ddos_server ${node/-0/-0} $addr $NUM_PARTICIPANTS $GROUP_ADDRESS_PORT $NUM_BURSTS $NUM_MESSAGES_PER_BURST $REQUEST_SIZE" 2>&1 | tee /tmp/server.$node.out&

		else
			break;
		fi
	fi
done < mcast.config

sleep 2
echo "Will now launch clients"

while read line
do
	node=`echo $line | cut -d ' ' -f 1`
	addr=`echo $line | cut -d ' ' -f 2`
	if [[ $node =~ ($NODENAMES_REGEX).* ]]
	then
		if [ $CLIENT_COUNTER -lt $NUM_REPLICAS ] 
		then
			let "CLIENT_COUNTER+=1"
			continue
		elif [ $CLIENT_COUNTER -le $NUM_PARTICIPANTS ]; then
			let "CLIENT_COUNTER+=1"
			echo -e "Deploying client $CLIENT_COUNTER on [$node]"
			echo -e "./ddos_client ${node/-0/-0} $addr $CLIENT_COUNTER $NUM_PARTICIPANTS $MASTER_NAME $SERVER_ADDR $GROUP_ADDRESS_PORT $NUM_BURSTS $NUM_MESSAGES_PER_BURST $REQUEST_SIZE $RATE"
			ssh -c arcfour -4 -o Compression=no -n ${LOGIN_NAME}@${node} "cd BFT/src/benchmarks ; ./ddos_client ${node/-0/-0} $addr $CLIENT_COUNTER $NUM_PARTICIPANTS $MASTER_NAME $SERVER_ADDR $GROUP_ADDRESS_PORT $NUM_BURSTS $NUM_MESSAGES_PER_BURST $REQUEST_SIZE $RATE" 2>&1 | tee /tmp/server.$node.out&

		fi
	fi
done < mcast.config

wait $THR_MANAGER_PID

sleep 4;

./kill_mcast.sh
sleep 1;

echo $RATE $NUM_PARTICIPANTS `grep total\ of /tmp/server.node*|perl -nle 'print "$1 $2" if m/total of (\d+)\/(\d+) requests/;'` >> ~/times/ddos.out

#REPLICA_COUNTER=0
#while read line
#do
#	node=`echo $line | cut -d ' ' -f 1`
#	if [[ $node =~ ($NODENAMES_REGEX).* ]]
#	then
#		if [ $REPLICA_COUNTER -lt $NUM_REPLICAS ] 
#		then
#			let "REPLICA_COUNTER+=1"
#			scp -c arcfour -4 -o Compression=no ${LOGIN_NAME}@${node/-1/-0}:/tmp/throughput.\*.out /tmp
#		fi
#	fi
#done < $CONFIG_QUORUM
