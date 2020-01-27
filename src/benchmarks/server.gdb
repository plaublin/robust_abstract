#!/bin/bash

echo "handle SIGINT pass" > /tmp/server.gdb
echo "handle SIGINT nostop" >> /tmp/server.gdb
echo "handle SIGPIPE pass" >> /tmp/server.gdb
echo "handle SIGPIPE nostop" >> /tmp/server.gdb
echo "b exit" >> /tmp/server.gdb
echo "run $@" >> /tmp/server.gdb
echo "bt" >> /tmp/server.gdb
echo "quit" >> /tmp/server.gdb
#gdb -x /tmp/server.gdb ./server &> server_$(hostname).log
#gdb -x /tmp/server.gdb ./server &> server_$(hostname).log &
#tail -f server_$(hostname).log
gdb -x /tmp/server.gdb ./server

