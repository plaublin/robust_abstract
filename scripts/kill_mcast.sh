#!/bin/bash

LOGIN_NAME="knl"
simultaneousSSH=30
SSH_TIMEOUT=10
#SUDO="" # no sudo on sci machines
SUDO="sudo" # we need sudo on emulab

for node in `cat mcast.config|cut -d' ' -f1`
do
   while [ true ]
   do
      echo -e "\E[32;40mKilling running code on $node with user $LOGIN_NAME"; tput sgr0 # green
      #ssh -c arcfour -o Compression=no -x -i $HOME/.ssh/id_rsa -o StrictHostKeyChecking=no ${LOGIN_NAME}@$node "${SUDO} pkill -HUP server ; sleep 0.2"
     ssh -c arcfour -o Compression=no -x -i $HOME/.ssh/id_rsa -o StrictHostKeyChecking=no ${LOGIN_NAME}@$node "${SUDO} pkill server ; sleep 0.2" & 
      ssh -c arcfour -o Compression=no -x -i $HOME/.ssh/id_rsa -o StrictHostKeyChecking=no ${LOGIN_NAME}@$node "${SUDO} pkill -f thr_ ; sleep 0.2" & 
      ssh -c arcfour -o Compression=no -x -i $HOME/.ssh/id_rsa -o StrictHostKeyChecking=no ${LOGIN_NAME}@$node "${SUDO} pkill -f serverp ; sleep 0.2" & 
      ssh -c arcfour -o Compression=no -x -i $HOME/.ssh/id_rsa -o StrictHostKeyChecking=no ${LOGIN_NAME}@$node "${SUDO} pkill -f clientp ; sleep 0.2" & 
      ssh -c arcfour -o Compression=no -x -i $HOME/.ssh/id_rsa -o StrictHostKeyChecking=no ${LOGIN_NAME}@$node "${SUDO} pkill -f multicast_replica ; sleep 0.2" &
      ssh -c arcfour -o Compression=no -x -i $HOME/.ssh/id_rsa -o StrictHostKeyChecking=no ${LOGIN_NAME}@$node "${SUDO} pkill -f ddos_ ; sleep 0.2" &
      break
   done
done

wait

sleep 2
${SUDO} pkill -f thr_manager
sleep 2
