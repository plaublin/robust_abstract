#!/bin/bash

# GET THE OPTIONS FROM THE CONFIG FILE
. ring_options.conf

MAX_NUM_CLIENTS=$NUM_CLIENTS

MYNODE=$1
NUM_CLIENTS=$2

# VARIABLES USED IN THE SCRIPT
simultaneousSSH=30
SSH_TIMEOUT=10

REPLICA_COUNTER=0
CLIENT_COUNTER=0

NODE_COUNTER=0
cp -f ~/BFT/src/benchmarks/${CLIENT_EXEC} /tmp/

while read line
do
	node=`echo $line | cut -d ' ' -f 1`
	if [[ $node =~ ($NODENAMES_REGEX).* ]]
	then
		port_quorum=`echo $line | cut -d ' ' -f 3`
		let "port_backup=$port_quorum+1000"
		let "port_chain=$port_quorum+2000"
		let "port_zlight=$port_quorum+3000"
		let "port_switching=$port_quorum-1000"
		let "NODE_COUNTER+=1"

		if [ $REPLICA_COUNTER -lt $NUM_REPLICAS ] 
		then
			let "REPLICA_COUNTER+=1"
		elif [ "X$node" == "X$MYNODE" -a $CLIENT_COUNTER -lt $NUM_CLIENTS ] 
                then
												sleep 1;
                                let "CLIENT_COUNTER+=1"  
				if [ "x$CLIENT_EXEC" == "xthr_client_db" ]; then
					REQUEST_SIZE=$NODE_COUNTER
				fi
                                echo "Deploying client $CLIENT_COUNTER on [$node,$port_quorum]" 
                                echo -e "./${CLIENT_EXEC} $node $MASTER_NAME $port_quorum $port_backup $port_chain $port_zlight $NUM_BURSTS $NUM_MESSAGES_PER_BURST "$REQUEST_SIZE" ../${CONFIG_QUORUM_CLIENTS} ../${CONFIG_PBFT_CLIENTS} ../${CONFIG_CHAIN_CLIENTS} ../${CONFIG_ZLIGHT_CLIENTS} ../$CONFIG_PRIVATE"
				cd ~/BFT/src/benchmarks;
					
				/tmp/${CLIENT_EXEC} $node $MASTER_NAME $port_quorum $port_backup $port_chain $port_zlight $NUM_BURSTS $NUM_MESSAGES_PER_BURST "$REQUEST_SIZE" ../${CONFIG_QUORUM_CLIENTS} ../${CONFIG_PBFT_CLIENTS} ../${CONFIG_CHAIN_CLIENTS} ../${CONFIG_ZLIGHT_CLIENTS} ../$CONFIG_PRIVATE $INIT_HISTORY_SIZE 2>&1 | tee /tmp/${MYNODE}.${CLIENT_COUNTER}.out || echo ${CLIENT_COUNTER} &
                fi
	fi
done < ${CONFIG_QUORUM_CLIENTS}

wait
