#!/bin/bash

# TO BE DEFINED BY USERS
NB_FAULTS=1
REQUEST_SIZE=1000 #This must be a multiple of 8
REPLY_SIZE=8 #This must be a multiple of 8
NUM_CLIENTS=${1:-2} #if nothing passed, assume 2 clients
CONFIG_QUORUM=../config_private/config_quorum_f_${NB_FAULTS}
CONFIG_PBFT=../config_private/config_backup_BFT_f_$NB_FAULTS
CONFIG_CHAIN=../config_private/config_chain_f_$NB_FAULTS
CONFIG_PRIVATE=../config_private
if [ $NB_FAULTS = 1 ]
then 
	NUM_REPLICAS=4
elif [ $NB_FAULTS = 2 ]
then 
	NUM_REPLICAS=7
else
	NUM_REPLICAS=10
fi
NUM_BURSTS=20
NUM_MESSAGES_PER_BURST=10000
LOGIN_NAME="root"
MASTER_NAME=sci6
MASTER_PORT=5000

# VARIABLES USED IN THE SCRIPT
simultaneousSSH=30
SSH_TIMEOUT=10

REPLICA_COUNTER=0
CLIENT_COUNTER=0

echo -e "Deploying master on $MASTER_NAME"
echo -e "./thr_manager $NUM_CLIENTS $NUM_BURSTS $NUM_MESSAGES_PER_BURST"
ssh ${LOGIN_NAME}@$MASTER_NAME "cd BFT/src/benchmarks ; ./thr_manager $NUM_CLIENTS $NUM_BURSTS $NUM_MESSAGES_PER_BURST" & 

while read line
do
	node=`echo $line | cut -d ' ' -f 1`
	str=`expr substr $node 1 3`
	if [ $str == "sci" ]
	then
		sleep 1
		port_quorum=`echo $line | cut -d ' ' -f 3`
		let "port_backup=$port_quorum+1000"
		let "port_chain=$port_quorum+2000"
		if [ $REPLICA_COUNTER -lt $NUM_REPLICAS ] 
		then
			let "REPLICA_COUNTER+=1"
			echo -e "Deploying replica $REPLICA_COUNTER on [$node,$port_quorum]"
			echo -e "./server $node $port_quorum $port_backup $port_chain $REPLY_SIZE ../$CONFIG_QUORUM ../$CONFIG_PBFT ../$CONFIG_PRIVATE ../$CONFIG_CHAIN"
	        ssh -n ${LOGIN_NAME}@${node/_/} "cd BFT/src/benchmarks ; ./server $node $port_quorum $port_backup $port_chain $REPLY_SIZE ../$CONFIG_QUORUM ../$CONFIG_PBFT ../$CONFIG_PRIVATE ../$CONFIG_CHAIN" &
		elif [ $CLIENT_COUNTER -lt $NUM_CLIENTS ] 
		then
				let "CLIENT_COUNTER+=1"  
				echo "Deploying client $CLIENT_COUNTER on [$node,$port_quorum]" 
				echo -e "./thr_client $node $port_quorum $port_backup $port_chain $NUM_BURSTS $NUM_MESSAGES_PER_BURST $REQUEST_SIZE ../$CONFIG_QUORUM ../$CONFIG_PBFT ../${CONFIG_CHAIN}_clients ../$CONFIG_PRIVATE"
		       ssh -n ${LOGIN_NAME}@${node/_/} "cd BFT/src/benchmarks ; ./thr_client $node $port_quorum $port_backup $port_chain $NUM_BURSTS $NUM_MESSAGES_PER_BURST $REQUEST_SIZE ../$CONFIG_QUORUM ../$CONFIG_PBFT ../${CONFIG_CHAIN}_clients ../$CONFIG_PRIVATE" &
		fi
	fi
done < $CONFIG_QUORUM

wait
