#!/bin/bash

# TO BE DEFINED BY USERS
NB_FAULTS=1
REQUEST_SIZE=4096 #This must be a multiple of 8
REPLY_SIZE=8 #This must be a multiple of 8
NUM_CLIENTS=${1:-1} #if nothing passed, assume 1 client
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
NUM_BURSTS=2
NUM_MESSAGES_PER_BURST=2000
LOGIN_NAME="root"
MASTER_LOGIN_NAME="root"
MASTER_NAME=sci6
MASTER_PORT=5000

# VARIABLES USED IN THE SCRIPT
simultaneousSSH=30
SSH_TIMEOUT=10

REPLICA_COUNTER=0
CLIENT_COUNTER=0

for node in `cat nodes`
do
	echo "Deleting logs on [$node]" 
      	ssh -c arcfour -o Compression=no -x -i $HOME/.ssh/id_rsa -o StrictHostKeyChecking=no ${LOGIN_NAME}@$node "rm /tmp/client\*.log" & 
done
rm -f ~/BFT/data/client_*.log

echo -e "Deploying master on $MASTER_NAME"
echo -e "./thr_manager_aliph $NUM_CLIENTS $NUM_BURSTS $NUM_MESSAGES_PER_BURST"
ssh ${MASTER_LOGIN_NAME}@$MASTER_NAME "cd BFT/src/benchmarks ; ./thr_manager_aliph $NUM_CLIENTS $NUM_BURSTS $NUM_MESSAGES_PER_BURST" 2>&1 | tee /tmp/output.manager & 
MASTERPID=$!

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
		let "port_switching=$port_quorum-1000"
		if [ $REPLICA_COUNTER -lt $NUM_REPLICAS ] 
		then
			let "REPLICA_COUNTER+=1"
			echo -e "Deploying replica $REPLICA_COUNTER on [$node,$port_quorum]"
			echo -e "./server $node $port_quorum $port_backup $port_chain $REPLY_SIZE ../$CONFIG_QUORUM ../$CONFIG_PBFT ../$CONFIG_PRIVATE ../$CONFIG_CHAIN 0"
			#if [ $REPLICA_COUNTER != 1 ]
			#then
		#ssh -n ${LOGIN_NAME}@${node/_/} "cd BFT/src/benchmarks ; ./server $node $port_quorum $port_backup $port_chain $REPLY_SIZE ../$CONFIG_QUORUM ../$CONFIG_PBFT ../$CONFIG_PRIVATE ../$CONFIG_CHAIN 0 0 >/dev/null 2>&1" &
		#ssh -n ${LOGIN_NAME}@${node/_/} "cd BFT/src/benchmarks ; ./server $node $port_quorum $port_backup $port_chain $REPLY_SIZE ../$CONFIG_QUORUM ../$CONFIG_PBFT ../$CONFIG_PRIVATE ../$CONFIG_CHAIN 0 0 >/tmp/server.$node.out 2>&1" &
		ssh -n ${LOGIN_NAME}@${node/_/} "cd BFT/src/benchmarks ; ./server $node $port_quorum $port_backup $port_chain $REPLY_SIZE ../$CONFIG_QUORUM ../$CONFIG_PBFT ../$CONFIG_PRIVATE ../$CONFIG_CHAIN 0 0" 2>&1 |tee /tmp/server.$node.out &
			#fi
		elif [ $CLIENT_COUNTER -lt $NUM_CLIENTS ] 
		then
			#sleep 20;
				let "CLIENT_COUNTER+=1"  
				echo "Deploying client $CLIENT_COUNTER on [$node,$port_quorum]" 
				echo -e "./thr_client_aliph $node $port_quorum $port_backup $port_chain $NUM_BURSTS $NUM_MESSAGES_PER_BURST $REQUEST_SIZE ../$CONFIG_QUORUM ../$CONFIG_PBFT ../${CONFIG_CHAIN} ../$CONFIG_PRIVATE"
		       #ssh -n ${LOGIN_NAME}@${node/_/} "cd BFT/src/benchmarks ; ./thr_client_aliph $node $port_quorum $port_backup $port_chain $NUM_BURSTS $NUM_MESSAGES_PER_BURST $REQUEST_SIZE ../$CONFIG_QUORUM ../$CONFIG_PBFT ../${CONFIG_CHAIN} ../$CONFIG_PRIVATE 0 >/dev/null 2>&1" &
		       #ssh -n ${LOGIN_NAME}@${node/_/} "cd BFT/src/benchmarks ; ./thr_client_aliph $node $port_quorum $port_backup $port_chain $NUM_BURSTS $NUM_MESSAGES_PER_BURST $REQUEST_SIZE ../$CONFIG_QUORUM ../$CONFIG_PBFT ../${CONFIG_CHAIN} ../$CONFIG_PRIVATE 0 >/tmp/client.$node.out 2>&1" &
		       ssh -n ${LOGIN_NAME}@${node/_/} "cd BFT/src/benchmarks ; ./thr_client_aliph $node $port_quorum $port_backup $port_chain $NUM_BURSTS $NUM_MESSAGES_PER_BURST $REQUEST_SIZE ../$CONFIG_QUORUM ../$CONFIG_PBFT ../${CONFIG_CHAIN} ../$CONFIG_PRIVATE 0" 2>&1 | tee /tmp/client.$node.out &
		fi
	fi
done < $CONFIG_QUORUM

wait $!
wait $MASTERPID

for node in `cat nodes`
do
	echo "Fetching logs from [$node]" 
	rsync -avz ${LOGIN_NAME}@${node/_/}:/tmp/client\*.log ../data/ 
done

exit
