#!/bin/bash

LOGIN_NAME="root"
simultaneousSSH=30
SSH_TIMEOUT=10

for node in `cat nodes`
do
   while [ true ]
   do
      echo -e "\E[32;40mClean $node with user $LOGIN_NAME"; tput sgr0 # green
      ssh -c arcfour -o Compression=no -x -i $HOME/.ssh/id_rsa -o StrictHostKeyChecking=no ${LOGIN_NAME}@$node "cd BFT ; rm -rf src config_private" & 
      break
   done
done

wait
