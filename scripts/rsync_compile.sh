#!/bin/bash
#
# Usage: ./rsync_compile.sh [compile]
# By default, rsync only. Compile if a parameter is given

LOGIN_NAME="bft"
SSH_TIMEOUT=10

function rsync_one_node {
echo "rsync on $1..."
rsync -rvazc -e 'ssh' --exclude-from './exclude_from_rsync.txt' ~/BFT/src/ ${LOGIN_NAME}@$1:~/BFT/src/
rsync -rvazc -e 'ssh' --exclude-from './exclude_from_rsync.txt' ~/BFT/scripts/ ${LOGIN_NAME}@$1:~/BFT/scripts/
rsync -rvazc -e 'ssh' --exclude-from './exclude_from_rsync.txt' ~/BFT/config_private/ ${LOGIN_NAME}@$1:~/BFT/config_private/
#rsync -rvazc -e 'ssh' --exclude-from './exclude_from_rsync.txt' ~/BFT/ ${LOGIN_NAME}@$1:~/BFT/
echo "DONE."
}

function compile_one_node {
echo "compile on $1..."
if [ "$2" = "clean" ]; then
    ssh -c arcfour -o Compression=no -x -i $HOME/.ssh/id_dsa -o StrictHostKeyChecking=no ${LOGIN_NAME}@$1 "cd ~/BFT/src; find . -name "Makefile" -exec touch {} \; ; make clean; make" &
else
    ssh -c arcfour -o Compression=no -x -i $HOME/.ssh/id_dsa -o StrictHostKeyChecking=no ${LOGIN_NAME}@$1 "cd ~/BFT/src; find . -name "Makefile" -exec touch {} \; ; make" &
fi
echo "DONE."
}

MACHINES=( sci50 sci71 sci72 sci73 sci74 sci75 sci76 sci77 )
#MACHINES=( sci50 sci71 sci72 sci73 sci10 sci17 sci19 sci24 )

for m in ${MACHINES[@]}; do
    rsync_one_node $m
    if [ $# -gt 0 ]; then
        compile_one_node $m $1
    fi
done

