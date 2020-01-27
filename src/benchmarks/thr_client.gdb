#!/bin/bash

echo "handle SIGINT pass" > /tmp/client.gdb
echo "handle SIGINT nostop" >> /tmp/client.gdb
echo "handle SIGPIPE pass" >> /tmp/client.gdb
echo "handle SIGPIPE nostop" >> /tmp/client.gdb
echo "b exit" >> /tmp/client.gdb
echo "run $@" >> /tmp/client.gdb
echo "bt" >> /tmp/client.gdb
echo "quit" >> /tmp/client.gdb
#gdb -x /tmp/client.gdb ./thr_client &> client_$(hostname).log
#gdb -x /tmp/client.gdb ./thr_client &> client_$(hostname).log &
#tail -f client_$(hostname).log
gdb -x /tmp/client.gdb ./thr_client

