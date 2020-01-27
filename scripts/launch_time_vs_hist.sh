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

# FIX: quorum needs a different configuration
CHOICE_FILE=../src/modular_bft/libmodular_BFT_choice.h
if grep define $CHOICE_FILE | grep -q QUORUM; then
	  echo "Quorum defined"
		CONFIG_QUORUM=${CONFIG_QUORUM}_clients
fi

if [ "x$PROTO" = "xring" ]; then
	LD_PRELOAD_LINE="LD_PRELOAD=/users/knl/hoard-38/src/libhoard.so"
fi

NUM_MALICIOUS=0;
if [ $HAVE_MALICIOUS != 0 ]; then
	NUM_MALICIOUS=$(( PERCENT_MALICIOUS * NUM_CLIENTS / 100 ))
fi

echo -e "Deploying master on $MASTER_NAME"
echo -e "./thr_manager $NUM_CLIENTS $NUM_BURSTS $NUM_MESSAGES_PER_BURST $NUM_MALICIOUS"
#ssh ${MASTER_LOGIN_NAME}@$MASTER_NAME "cd BFT/src/benchmarks ; $LIMIT_CMD; ./thr_manager $NUM_CLIENTS $NUM_BURSTS $NUM_MESSAGES_PER_BURST $NUM_MALICIOUS" 2>&1 |tee /tmp/manager.out-${NUM_CLIENTS}-${REQUEST_SIZE}-${REPLY_SIZE}-$(( PERCENT_MALICIOUS * HAVE_MALICIOUS )) & 
ssh ${MASTER_LOGIN_NAME}@$MASTER_NAME "cd BFT/src/benchmarks ; $LIMIT_CMD; ./thr_manager $NUM_CLIENTS $NUM_BURSTS $NUM_MESSAGES_PER_BURST $NUM_MALICIOUS" 2>&1 |tee /tmp/manager.out-${NUM_CLIENTS}-${REQUEST_SIZE}-${REPLY_SIZE} & 
THR_MANAGER_PID=$!
sleep 2

if [[ ${SERVER_EXEC} =~ redis ]]
then
	REDIS_SRVR='rm -f /tmp/*.rdb; ../redis/src/redis-server ../redis/redis.conf &; sleep 2'
else
	REDIS_SRVR=''
fi

#SYSSTAT_START='rm -f /tmp/sadc.stats; /usr/lib/sysstat/sadc -F 1 /tmp/sadc.stats &; echo go'
#SYSSTAT_STOP='sudo killall sadc'
#SYSSTAT_COLLECT='cp -f /tmp/sadc.stats ~/oprofiler/sadc.stats'
SYSSTAT_COLLECT='echo '

while read line
do
	node=`echo $line | cut -d ' ' -f 1`
	if [[ $node =~ ($NODENAMES_REGEX).* ]]
	then
		port_quorum=`echo $line | cut -d ' ' -f 3`
		let "port_backup=$port_quorum+1000"
		let "port_chain=$port_quorum+2000"
		let "port_switching=$port_quorum-1000"
		if [ $REPLICA_COUNTER -lt $NUM_REPLICAS ] 
		then
			let "REPLICA_COUNTER+=1"
			if [ $DO_DEBUG -gt 0 -a $REPLICA_COUNTER -eq $DO_DEBUG ]
			then
				echo -e "Skipping one"
			else
			echo -e "Deploying replica $REPLICA_COUNTER on [$node,$port_quorum]"
			#echo -e "./${SERVER_EXEC} ${node/-0/-1} $port_quorum $port_backup $port_chain $REPLY_SIZE $SLEEP_TIME ../$CONFIG_QUORUM ../$CONFIG_PBFT ../$CONFIG_PRIVATE ../$CONFIG_CHAIN 0 0"
			echo -e "./${SERVER_EXEC} $node $port_quorum $port_backup $port_chain $REPLY_SIZE $SLEEP_TIME ../$CONFIG_QUORUM ../$CONFIG_PBFT ../$CONFIG_PRIVATE ../$CONFIG_CHAIN 0 0"
			if [ $DO_PROFILING -eq 0 ]
			then
			    DB_PREP=""
			    if [ "x$SERVER_EXEC" = "xserver_db" ]; then
				    DB_PREP="cp -f ~/BFT/dbt2.sqlite /tmp/"
			    fi
			    #ssh -c arcfour -4 -o Compression=no -n ${LOGIN_NAME}@${node/-1/-0} \
			    ssh -c arcfour -4 -o Compression=no -n ${LOGIN_NAME}@$node \
			    	"$LIMIT_CMD; cd BFT/src/benchmarks;\
				${DB_PREP}; \
				${REDIS_SRVR}; \
				${SYSSTAT_START}; \
			    	sudo opcontrol --shutdown; \
			     	sudo ${LD_PRELOAD_LINE} ./${SERVER_EXEC} ${node} $port_quorum $port_backup $port_chain $REPLY_SIZE $SLEEP_TIME ../$CONFIG_QUORUM ../$CONFIG_PBFT ../$CONFIG_PRIVATE ../$CONFIG_CHAIN 0 0; \
				${SYSSTAT_STOP}; \
				${SYSSTAT_COLLECT}.${REPLICA_COUNTER}; \
			    	 " 2>&1 | tee /tmp/server.${node}.out &
			else
				  echo "RUNNING OPROFILE"
			    ssh -c arcfour -4 -o Compression=no -n ${LOGIN_NAME}@${node} \
			    	"$LIMIT_CMD; cd BFT/src/benchmarks;\
			    	sudo opcontrol --init; \
			    	sudo opcontrol --reset; \
			    	sudo opcontrol --no-vmlinux --callgraph=8 --image=/users/knl/BFT/src/benchmarks/${SERVER_EXEC} --start; \
			     	./${SERVER_EXEC} ${node} $port_quorum $port_backup $port_chain $REPLY_SIZE $SLEEP_TIME ../$CONFIG_QUORUM ../$CONFIG_PBFT ../$CONFIG_PRIVATE ../$CONFIG_CHAIN 0 0; \
			    	sudo opcontrol --dump; \
			    	sudo opcontrol --stop; \
			    	opreport --callgraph >~/oprofiler/$REPLICA_COUNTER/${SERVER_EXEC}.callgraph; \
			    	opannotate --source --output-dir=/users/knl/oprofiler/$REPLICA_COUNTER/ /users/knl/BFT/src/benchmarks/${SERVER_EXEC}; \
			    	opannotate --source -a /users/knl/BFT/src/benchmarks/${SERVER_EXEC} >~/oprofiler/$REPLICA_COUNTER/${SERVER_EXEC}.asm; \
			    	sudo opcontrol --deinit; \
			    	" 2>&1 | tee /tmp/server.${node}.out &
					#DO_PROFILING=0
			fi
			fi
		else
			break;
		fi
	fi
done < $CONFIG_QUORUM

sleep 2
echo "Will now launch clients"

perl launch_ring_clients.pl $NODENAMES_REGEX $NUM_REPLICAS $NUM_CLIENTS\
	"ssh -c arcfour -4 -o Compression=no" \
	$LOGIN_NAME \
	"$LIMIT_CMD; rm -f /tmp/client*; cd BFT/scripts; ./run_ring_clients.sh " \
	<$CONFIG_QUORUM &

wait $THR_MANAGER_PID
./kill.sh

true

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
