#!/bin/bash

LOGIN_NAME="bft"
simultaneousSSH=30
SSH_TIMEOUT=10
#SUDO="" # no sudo on sci machines
SUDO="sudo" # we need sudo on emulab

NODENAMES_REGEX="sci|replica|client|bordeplage"
PROCESS_REGEX="server|server_redis|thr_client|thr_client_redis|serverp|clientp|multicast_replica|redis-benchmark|gdb|valgrind"

#cat /etc/hosts |grep lan1 |perl -nle "if (/$NODENAMES_REGEX/) { @a=split/\s+/;print \$a[-1]; }" | while read node
cat /etc/hosts | grep -v '^#' | perl -nle "if (/$NODENAMES_REGEX/) { @a=split/\s+/;print \$a[-1]; }" | while read node
do
#   echo "Checking line '$node'"
      echo -ne "\E[32;40mKilling running code on $node with user $LOGIN_NAME"; tput sgr0 # green
      #ssh -c arcfour -o Compression=no -x -i $HOME/.ssh/id_rsa -o StrictHostKeyChecking=no ${LOGIN_NAME}@$node "${SUDO} pkill -HUP server ; sleep 0.2"
     ssh -c arcfour -o Compression=no -x -i $HOME/.ssh/id_dsa -o StrictHostKeyChecking=no ${LOGIN_NAME}@$node "${SUDO} pkill '$PROCESS_REGEX' ; sleep 0.2; ${SUDO} pkill -9 '$PROCESS_REGEX'; sleep 0.2" & 
#      ssh -c arcfour -o Compression=no -x -i $HOME/.ssh/id_rsa -o StrictHostKeyChecking=no ${LOGIN_NAME}@$node "${SUDO} pkill -f thr_ ; sleep 0.2" & 
#      ssh -c arcfour -o Compression=no -x -i $HOME/.ssh/id_rsa -o StrictHostKeyChecking=no ${LOGIN_NAME}@$node "${SUDO} pkill -f serverp ; sleep 0.2" & 
#      ssh -c arcfour -o Compression=no -x -i $HOME/.ssh/id_rsa -o StrictHostKeyChecking=no ${LOGIN_NAME}@$node "${SUDO} pkill -f clientp ; sleep 0.2" & 
#      ssh -c arcfour -o Compression=no -x -i $HOME/.ssh/id_rsa -o StrictHostKeyChecking=no ${LOGIN_NAME}@$node "${SUDO} pkill -f multicast_replica ; sleep 0.2" &
      echo -e "\E[32;40m\t...Done"; tput sgr0 # green
done

wait
echo "Ending"

sleep 2
${SUDO} pkill -f thr_manager
sleep 2
