#!/bin/bash
#
# This script modified the /etc/hosts files on g5k so that the other scripts (e.g., to launch an experiment) work

while read line; do echo $(ping -c 1 $line | grep PING | awk '{print $3}' | tr '(' ' ' | tr ')' ' ') $(echo $line | awk -F. '{print $1}'); done < ~/nodes  | sudo tee -a /etc/hosts
