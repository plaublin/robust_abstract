include ../Makefile.common

APPS=thr_client thr_manager server multicast_replica \
	 server_ldap thr_client_ldap server_db server_redis \
	 thr_client_redis redis-benchmark thr_client_db ddos_client ddos_server

all: $(APPS)

INCLUDES += $(PROTO_INCS)

DBT2_INCS:=-Idbt2/src/include
DBT2_LIBS:=-lm -lsqlite3 -lpthread
DBT2_SOURCE:= dbt2/src/sqlite/dbc_common.c dbt2/src/nonsp/dbc_new_order.c \
                dbt2/src/nonsp/dbc_payment.c dbt2/src/nonsp/dbc_order_status.c \
                dbt2/src/nonsp/dbc_delivery.c dbt2/src/nonsp/dbc_stock_level.c \
                dbt2/src/nonsp/dbc_integrity.c dbt2/src/nonsp/dbc_nonsp.c \
		dbt2/src/common.c dbt2/src/logging.c dbt2/src/_socket.c \
        	dbt2/src/client_interface.c dbt2/src/transaction_data.c dbt2/src/_semaphore.c \
		dbt2/src/db.c \
		dbt2/src/input_data_generator.c

DBT2_OBJS=$(DBT2_SOURCE:%.c=%.o)

REDIS_INCS:=-I../redis/src/
REDIS_SOURCE:= \
../redis/src/adlist.c      ../redis/src/ae.c          \
../redis/src/anet.c \
../redis/src/sds.c \
../redis/src/zmalloc.c

REDIS_OBJS:=$(REDIS_SOURCE:%.c=%.o)

INCLUDES += $(DBT2_INCS) $(REDIS_INCS)
LIBS     += $(DBT2_LIBS) $(REDIS_LIBS)

DEFINES += -DLIBSQLITE -D_POSIX_C_SOURCE=199309

CFLAGS+=--std=c99

# THis doesn't work for chain
#LIBS:=lib.a -lsfscrypt -lasync -lgmp -lpthread -ltcmalloc
LIBS:=lib.a -lsfscrypt -lasync -lgmp -lpthread -lm -lsqlite3 -lldap -llber #-ltcmalloc

%.o:: %.cc
	$(CC) $(CPPFLAGS) -o $@ -c $<

%.o:: %.c
	$(C) -Os $(CFLAGS) -o $@ -c $<

replica_main_redis.o: replica_main_redis.cc
	$(CC) $(CPPFLAGS) $(REDIS_INCS) -o $@ -c $<

thr_client_redis.o: thr_client_redis.c
	$(C) $(CFLAGS) $(REDIS_INCS) -o $@ -c $<

redis-benchmark.o: redis-benchmark.c
	$(C) $(CFLAGS) $(REDIS_INCS) -o $@ -c $<

C_FILES=\
thr_manager.cc incremental_stats.cc \
replica_main.c \
replica_main_db.c \
replica_main_redis.c \
replica_main_ldap.c \
thr_client.cc \

c_FILES=\
multicast_replica.c \
thr_client_db.c  \
thr_client_redis.c \
thr_client_ldap.c \
ddos_client.c ddos_server.c \
ldapsearch.c \
ldap_search.c \
common.c

H_FILES := $(C_FILES:%.C=%.H)

h_FILES := $(c_FILES:%.c=%.h)

OBJ_FILES:=$(C_FILES:%.cc=%.o) $(c_FILES:%.c=%.o)

objs: $(DBT2_OBJS) $(OBJ_FILES)

dbt2_objs: $(DBT2_OBJS)

$(REDIS_OBJS): $(REDIS_SOURCE) ../redis/Makefile
	cd ../redis/src && make CC="${C}" C="${C}"

redis_objs: $(REDIS_OBJS)

clean: common-clean
	-rm -f $(DBT2_OBJS)
	-rm -f $(APPS)
	-cd ../redis/src && make clean

clobber: common-clobber
	-rm -f thr_manager thr_client server server_db

thr_manager: lib.a thr_manager.o incremental_stats.o
	$(CC) -o thr_manager $(CPPFLAGS) $(LIBDIRS) thr_manager.o incremental_stats.o $(LIBS)

thr_manager_aliph: lib.a thr_manager_aliph.o incremental_stats.o
	$(CC) -o thr_manager_aliph $(CPPFLAGS) $(LIBDIRS) thr_manager_aliph.o incremental_stats.o $(LIBS)

server: lib.a replica_main.o incremental_stats.o
	$(CC) -o server $(CPPFLAGS) $(LIBDIRS) replica_main.o incremental_stats.o $(LIBS)

server_db: lib.a replica_main_db.o incremental_stats.o dbt2_objs
	$(CC) -o server_db $(CPPFLAGS) $(LIBDIRS) $(DBT2_OBJS) replica_main_db.o incremental_stats.o $(LIBS) -lsqlite3

server_redis: lib.a replica_main_redis.o incremental_stats.o redis_objs
	$(CC) -o $@ $(CPPFLAGS) $(LIBDIRS) $(REDIS_OBJS) replica_main_redis.o incremental_stats.o $(LIBS)

server_ldap: lib.a replica_main_ldap.o incremental_stats.o ldap_search.o
	$(CC) -o server_ldap $(CPPFLAGS) $(LIBDIRS) replica_main_ldap.o incremental_stats.o ldap_search.o $(LIBS)

thr_client_redis: lib.a thr_client_redis.o incremental_stats.o redis_objs
	$(CC) -o $@ $(CPPFLAGS) $(LIBDIRS) $(REDIS_OBJS) thr_client_redis.o incremental_stats.o $(LIBS) 

redis-benchmark: redis-benchmark.o redis_objs
	$(C) -o $@ $(CFLAGS) $(LIBDIRS) $(REDIS_OBJS) redis-benchmark.o $(LIBS)

thr_client_db: lib.a thr_client_db.o incremental_stats.o dbt2_objs
	$(CC) -o thr_client_db $(CPPFLAGS) $(LIBDIRS) $(DBT2_OBJS) thr_client_db.o incremental_stats.o $(LIBS) -lsqlite3 

thr_client: lib.a thr_client.o incremental_stats.o
	$(CC) -o thr_client $(CPPFLAGS) $(LIBDIRS) thr_client.o incremental_stats.o $(LIBS) 

thr_client_aliph: lib.a thr_client_aliph.o incremental_stats.o
	$(CC) -o thr_client_aliph $(CPPFLAGS) $(LIBDIRS) thr_client_aliph.o incremental_stats.o $(LIBS) 

thr_client_ldap: lib.a thr_client_ldap.o incremental_stats.o ldap_search.o
	$(CC) -o thr_client_ldap $(CPPFLAGS) $(LIBDIRS) thr_client_ldap.o incremental_stats.o ldap_search.o  $(LIBS) 

multicast_replica: lib.a multicast_replica.o
	$(CC) -o multicast_replica $(CPPFLAGS) $(LIBDIRS) multicast_replica.o $(LIBS) -lrt

ddos_server: lib.a ddos_server.o
	$(CC) -o ddos_server $(CPPFLAGS) $(LIBDIRS) ddos_server.o $(LIBS) -lrt

ddos_client: lib.a ddos_client.o
	$(CC) -o ddos_client $(CPPFLAGS) $(LIBDIRS) ddos_client.o $(LIBS) -lrt

include ../Makefile.common.tail
