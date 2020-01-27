#ifndef _benchmarks_h
#define _benchmarks_h 1

#define MANAGER_PORT 10000 

typedef struct sockaddr_in Address;

enum bft_protocol { proto_quorum = 1, proto_chain = 2, proto_backup = 4, proto_zlight = 8 };

typedef struct thr_command
{
    int malicious;
   int num_iter;
   enum bft_protocol proto;
   int done;
   int notify;
   int pos;
   int cid;
   int make_switch;
   double avg_rt;
   double avg_thr; //if message sent by the manager, then this value is the client's limit throughput
}thr_command;

#endif // _simple_h
