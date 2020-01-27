#!/bin/bash

# GET THE OPTIONS FROM THE CONFIG FILE
. ring_options.conf

# VARIABLES USED IN THE SCRIPT
simultaneousSSH=30
SSH_TIMEOUT=10

REPLICA_COUNTER=0
CLIENT_COUNTER=0

LIMIT_CMD='limit descriptors 8000; limit coredumpsize 1000000'
#LIMIT_CMD='ulimit -n 8000'

echo -e "Deploying master on $MASTER_NAME"
echo -e "./thr_manager $NUM_CLIENTS $NUM_BURSTS $NUM_MESSAGES_PER_BURST"
ssh ${MASTER_LOGIN_NAME}@$MASTER_NAME "cd BFT/src/benchmarks ; $LIMIT_CMD; ./thr_manager $NUM_CLIENTS $NUM_BURSTS $NUM_MESSAGES_PER_BURST" 2>&1 |tee /tmp/manager.out-${NUM_CLIENTS}-${REQUEST_SIZE}-${REPLY_SIZE} & 
THR_MANAGER_PID=$!
sleep 2

REDIS_SRVR='rm -f /tmp/*.rdb; ../redis/src/redis-server ../redis/redis.conf &; sleep 2'
SERVER_EXEC=
CLIENT_EXEC=

node=replica-0
echo -e "Deploying replica $REPLICA_COUNTER on [$node]"
echo -e "./${SERVER_EXEC} $node"
ssh -c arcfour -4 -o Compression=no -n ${LOGIN_NAME}@$node \
	"$LIMIT_CMD; cd BFT/src/benchmarks;\
	${REDIS_SRVR}" &
sleep 2
echo "Will now launch clients"

while read line
do
	node=`echo $line | cut -d ' ' -f 1`
	if [[ $node =~ ($NODENAMES_REGEX).* ]]
	then
		if [ $REPLICA_COUNTER -lt $NUM_REPLICAS ] 
		then
			let "REPLICA_COUNTER+=1"
		elif [ $CLIENT_COUNTER -lt $NUM_CLIENTS ]
		then
			let "CLIENT_COUNTER+=1"  
			echo $node
			echo "Deploying client $CLIENT_COUNTER on [$node]" 
			echo -e "./redis-benchmark $node $NUM_MESSAGES_PER_BURST $REQUEST_SIZE"
			echo ${LOGIN_NAME}@${node/_/} "cd BFT/src/benchmarks ; ./redis-benchmark -h replica-0 -c 1 -n $NUM_MESSAGES_PER_BURST -d $REQUEST_SIZE -l $NUM_BURSTS -m $MASTER_NAME -w $MASTER_PORT" &
			ssh -n ${LOGIN_NAME}@${node/_/} "cd BFT/src/benchmarks ; ./redis-benchmark -h replica-0 -c 1 -n $NUM_MESSAGES_PER_BURST -d $REQUEST_SIZE -l $NUM_BURSTS -m $MASTER_NAME -w $MASTER_PORT" &
		fi
	fi
done < $CONFIG_QUORUM

wait $THR_MANAGER_PID
./kill.sh

true

