#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/types.h>
#include <netinet/tcp.h>
#include <sys/time.h>

extern "C" {
#include "TCP_network.h"
}

#include "RobustMonitor.h"

struct C_Checkpoint_rep {
   short tag;
   short extra; // May be used to store extra information.
   int size; // Must be a multiple of 8 bytes to ensure proper
   short listnum;
   uint32_t unused;
   int id; // id of the replica which generated the message
   Seqno seqid; // sequence number of the last message in the request history
};

#ifdef RUN_ON_TERMINATORS
   // interfaces[i][j] = n <=> node i communicates with node j via interface n
   static int interfaces[4][4] = {
       20, 21, 22, 23,
       21, 20, 24, 22,
       22, 24, 20, 21,
       23, 22, 21, 20
   };
#endif

static void RM_init_clock_mhz() {
  struct timeval t0,t1;
  
  long long c0 = RM_rdtsc();
  gettimeofday(&t0, 0);
  sleep(1);
  long long c1 = RM_rdtsc();
  gettimeofday(&t1, 0);
    
  RM_clock_mhz = (c1-c0)/((t1.tv_sec-t0.tv_sec)*1000000+t1.tv_usec-t0.tv_usec);
}

void *robust_monitor_thread_startup(void *_tgtObject)
{
  printf("Robust monitor thread startup....\n");

  RobustMonitor *tgtObject = (RobustMonitor *) _tgtObject;
  void *threadResult = tgtObject->run();
  delete tgtObject;
  return threadResult;
}

RobustMonitor::RobustMonitor(const char* config_file, const char* host_name,
    const short req_port)
{
  list = NULL;
  current_protocol_LE = NULL;
  current_protocol_name = chain;
  id = -1;

  RM_init_clock_mhz();
  obs_thr = RM_req_throughput_init * 10;
  req_thr = RM_req_throughput_init;
  nb_recv_rep = 0;
  total_nb_client_rep_notif = 0;
  last_switch_time = RM_current_time();
  in_grace_period = true;
  nb_compute_expectations_calls = 0;

  FILE *f = fopen(config_file, "r");
  if (!f)
  {
    fprintf(stderr, "RobustMonitor: Error while trying to open %s !!!\n",
        config_file);
    return;
  }

  // Read public configuration file:
  // read max_faulty and compute derived variables
  int max_faulty;
  fscanf(f, "%d\n", &max_faulty);
  num_replicas = 3 * max_faulty + 1;
  addresses = new Addr[num_replicas];

#ifdef RUN_ON_TERMINATORS
  if (max_faulty != 1) {
      fprintf(stderr, "Cannot use multiple NICS with f=%d: the association between nodes is not defined\n", max_faulty);
  }
#endif

  // read in all the principals
  char addr_buff[100];
  short port;
  int i_dummy;
  char s_dummy[1024];

  fscanf(f, "%d\n", &i_dummy);
  fscanf(f, "%256s %hd\n", s_dummy, &port);

  struct hostent *hent = gethostbyname(host_name);
  if (hent == 0)
  {
    fprintf(stderr, "RobustMonitor: Could not get hostent\n");
    return;
  }

  struct in_addr my_address = *((in_addr*) hent->h_addr_list[0]);
  id = -1;

  char temp_host_name[MAXHOSTNAMELEN + 1];
  for (int i = 0; i < num_replicas; i++)
  {
    fscanf(f, "%256s %32s %hd %1024s \n", temp_host_name, addr_buff, &port,
        s_dummy);

    addresses[i].sin_family = AF_INET;
    addresses[i].sin_addr.s_addr = inet_addr(addr_buff);
    addresses[i].sin_port = htons(MONITOR_PORT);

    fprintf(stderr,
        "RobustMonitor: node %d (host %s) addr is %3d.%3d.%3d.%3d:%i\n", i,
        temp_host_name, NIPQUAD(addresses[i].sin_addr.s_addr),ntohs(addresses[i].sin_port)) ;

    if (my_address.s_addr == addresses[i].sin_addr.s_addr && id == -1
        && (req_port == 0 || req_port == port))
    {
      id = i;
    }
  }
  if (id < 0)
  {
    fprintf(stderr, "RobustMonitor: Could not find my principal");
    return;
  }

  fclose(f);

  int bootstrap_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (bootstrap_socket == -1)
  {
    perror("RobustMonitor: Error while creating the socket, exiting! ");
    exit(errno);
  }
  // TCP NO DELAY
  int flag = 1;
  int result = setsockopt(bootstrap_socket, IPPROTO_TCP, TCP_NODELAY,
      (char*) &flag, sizeof(int));
  if (result == -1)
  {
    perror("RobustMonitor: Error while setting TCP NO DELAY! ");
  }

  struct sockaddr_in bind_addr;
  bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_port = htons(MONITOR_PORT);
  if (bind(bootstrap_socket, (struct sockaddr*) &bind_addr,
      sizeof(bind_addr)) == -1)
  {
    perror("RobustMonitor: Error while binding to the socket! ");
    exit(errno);
  }

  // 3) make the socket listening for incoming connections
  if (listen(bootstrap_socket, num_replicas + 1) == -1)
  {
    perror("RobustMonitor: Error while calling listen! ");
    exit(errno);
  }

  rcv_socks = (int*) malloc(sizeof(int) * num_replicas);
  snd_socks = (int*) malloc(sizeof(int) * num_replicas);
  for (int i = 0; i < num_replicas; i++)
  {
    rcv_socks[i] = -1;
    snd_socks[i] = -1;
  }

  // 4) connect to the other replicas
  for (int i = 0; i <= id; i++)
  {
    // 1) create the socket
    snd_socks[i] = socket(AF_INET, SOCK_STREAM, 0);
    if (snd_socks[i] == -1)
    {
      perror("RobustMonitor: Error while creating the socket! ");
      exit(errno);
    }

    // 2)  connect to the server
    unsigned int s_addr = addresses[i].sin_addr.s_addr;
#ifdef RUN_ON_TERMINATORS
    NIPQUADi(s_addr, 2) = interfaces[id][i];
#endif
    struct sockaddr_in addr;
    addr.sin_addr.s_addr = s_addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(MONITOR_PORT);

    fprintf(stderr, "RobustMonitor: Node %d connects to verifier %d at %3d.%3d.%3d.%3d:%i\n",
        id, i, NIPQUAD(s_addr),ntohs(addr.sin_port)) ;

    // 2b) TCP NO DELAY
    flag = 1;
    result = setsockopt(snd_socks[i], IPPROTO_TCP, TCP_NODELAY, (char *) &flag,
        sizeof(int));
    if (result == -1)
    {
      perror("RobustMonitor: Error while setting TCP NO DELAY! ");
      exit(-1);
    }

    // 2c) connect
    while (true)
    {
      if (connect(snd_socks[i], (struct sockaddr *) &(addr), sizeof(addr)) < 0)
      {
        perror("RobustMonitor: Cannot connect, attempting again..");
        sleep(1);
      }
      else
      {
        fprintf(stderr,
            "RobustMonitor: Connection successful from robust monitor %i to %i!\n", id, i);
        break;
      }
    }
  }

  // 5) accepting connections from my id to the last replica
  for (int i = 0; i < num_replicas - id; i++)
  {
    struct sockaddr_in csin;
    int sinsize = sizeof(csin);
    int rcv_sock = accept(bootstrap_socket, (struct sockaddr*) &csin,
        (socklen_t*) &sinsize);

    if (rcv_sock == -1)
    {
      perror("RobustMonitor: An invalid socket has been accepted! ");
      continue;
    }

    // TCP NO DELAY
    flag = 1;
    result = setsockopt(rcv_sock, IPPROTO_TCP, TCP_NODELAY, (char *) &flag,
        sizeof(int));
    if (result == -1)
    {
      perror("RobustMonitor: Error while setting TCP NO DELAY! ");
    }

    int id_of_rcv = 0;
    unsigned int s_addr;
    while (id_of_rcv < num_replicas)
    {
      s_addr = addresses[id_of_rcv].sin_addr.s_addr;
#ifdef RUN_ON_TERMINATORS
      NIPQUADi(s_addr, 2) = interfaces[id_of_rcv][id];
#endif
      if (csin.sin_addr.s_addr == s_addr)
      {
        break;
      }
      id_of_rcv++;
    }

    if (id_of_rcv >= num_replicas)
    {
      fprintf(stderr, "RobustMonitor: Unknown host: %3d.%3d.%3d.%3d\n",
          NIPQUAD(csin.sin_addr.s_addr));
    } else {
       rcv_socks[id_of_rcv] = rcv_sock;
    }

    // print some information about the accepted connection
    fprintf(
        stderr,
        "RobustMonitor: A connection has been accepted from Robust Monitor %3d.%3d.%3d.%3d:%i, id=%i\n",
        NIPQUAD(s_addr),ntohs(csin.sin_port), id_of_rcv) ;
  }

  for (int i = 0; i < num_replicas; i++)
  {
    if (rcv_socks[i] == -1)
    {
      rcv_socks[i] = snd_socks[i];
    }
    if (snd_socks[i] == -1)
    {
      snd_socks[i] = rcv_socks[i];
    }
  }

  pthread_t t;
  int rc = pthread_create(&t, NULL, robust_monitor_thread_startup, this);
  if (rc)
  {
    fprintf(stderr, "RobustMonitor: Failed to create the robust monitor thread\n");
    exit(1);
  }
}

RobustMonitor::~RobustMonitor()
{
  _Monitor_LE* ptr = list;
  while (ptr)
  {
    delete ptr;
    ptr = list->next;
    list = ptr;
  }
  list = NULL;
}

void RobustMonitor::register_handle_msg_func(const enum protocols_e protocol,
    handle_msg_func func)
{
  _Monitor_LE* ptr = find(protocol);
  if (ptr == NULL)
  {
    ptr = new _Monitor_LE();
  }
  ptr->protocol = protocol;
  ptr->func = func;
  ptr->next = list;
  list = ptr;
}

void RobustMonitor::deregister_handle_msg_func(const enum protocols_e protocol)
{
  _Monitor_LE* ptr = find(protocol);
  if (ptr == NULL)
  {
    return;
  }
  if (ptr == list)
  {
    list = list->next;
    ptr->next = NULL;
    delete ptr;
    return;
  }

  _Monitor_LE* ptr2 = list;
  while (ptr2->next && ptr2->next != ptr)
  {
    ptr2 = ptr2->next;
  }
  if (ptr2)
  {
    ptr2->next = ptr->next;
    ptr->next = NULL;
    delete ptr;
  }
}

/*
 * Send the message msg of size len for protocol instance protocol_instance to:
 *  -all replicas but self if dest == -1
 *  -replica dest otherwise
 */
void RobustMonitor::send_message(const char *msg, const int len, const int dest, const enum protocols_e protocol_instance)
{
  if (dest == -1)
  {
//     if (current_protocol_name == chain) {
//        struct C_Checkpoint_rep* rep = (struct C_Checkpoint_rep*)msg;
//        if (rep->tag == 3) {
//           fprintf(stderr, "RobustMonitor %d sends checkpoint for %lld of size %d to %d with protocol %d: [", id, rep->seqid, len, dest, protocol_instance);
//           for (int i=0; i<len; i++) {
//              fprintf(stderr, " %x", msg[i]);
//           }
//           fprintf(stderr, "]\n");
//        }
//     }

    for (int i = 0; i < num_replicas; i++)
    {
      send_message(msg, len, i, protocol_instance);
    }
    return;
  }

  if (dest == id)
  {
    return;
  }

  //fprintf(stderr, "RobustMonitor %d sends message of size %d to %d with protocol %d\n", id, len, dest, current_protocol_name);

  int ret;
  int l = sizeof(protocol_instance);
  ret = send_all(snd_socks[dest], (void*)&protocol_instance, &l);
  if (ret == -1 || l != sizeof(protocol_instance)) {
     fprintf(stderr, "send_all[%d].1 error: ret=%d, l=%d\n", dest, ret, l);
  }

  l = len;
  send_all(snd_socks[dest], (void*)msg, &l);
  if (ret == -1 || l != len) {
     fprintf(stderr, "send_all[%d].2 error: ret=%d, l=%d\n", dest, ret, l);
  }

}

void* RobustMonitor::run(void)
{
  fd_set file_descriptors; //set of file descriptors to listen to (only one socket, in this case)
  timeval listen_time; //max time to wait for something readable in the file descriptors
  while (1)
  {
    int maxsock = 0;
    FD_ZERO(&file_descriptors); //initialize file descriptor set
    for (int i = 0; i < num_replicas; i++)
    {
      if (i != id && rcv_socks[i] != -1)
      {
        FD_SET(rcv_socks[i], &file_descriptors);
        maxsock = MAX(maxsock, rcv_socks[i]);
      }
    }

    listen_time.tv_sec = 0;
    listen_time.tv_usec = 500;

    select(maxsock + 1, &file_descriptors, NULL, NULL, &listen_time);

    for (int i = 0; i < num_replicas; i++)
    {
       if (i != id && FD_ISSET(rcv_socks[i], &file_descriptors)) {
          enum protocols_e protocol_instance;
          recv_all_blocking(rcv_socks[i], &protocol_instance, sizeof(protocol_instance), 0);

          //fprintf(stderr, "RobustMonitor %d has received message for protocol %d\n", id, protocol_instance);

          if (protocol_instance == current_protocol_name && current_protocol_LE != NULL)
          {
             current_protocol_LE->func(rcv_socks[i]);
          } else {
            _Monitor_LE* protocol_LE = find(protocol_instance);
            if (protocol_LE != NULL) {
               protocol_LE->func(rcv_socks[i]);
            } else {
               //fprintf(stderr, "Nobody to handle for protocol %d\n", protocol_instance);
            }
          }
       }
    }
  }

  return NULL;
}

void RobustMonitor::compute_expectations(void)
{
  nb_compute_expectations_calls++;

  RM_Time now = RM_current_time();
  double diff = (double) RM_diff_time(now, last_switch_time);
  obs_thr = (nb_recv_rep*NOTIF_PERIOD*1e6) / diff;

  if (in_grace_period) {
     if (diff > GRACE_PERIOD * 1e6) {
         in_grace_period = false;
         last_switch_time = RM_current_time();
         nb_recv_rep = 0;
     } else if (nb_compute_expectations_calls % RM_display_expectations_period == 0) {
     fprintf(stderr, "Time: %qd\tObs thr: %f\tReq thr: %f (req/sec) [Grace period]\n", RM_diff_time(now, 0), obs_thr, req_thr);
     }
     return;
  } 

  if (nb_compute_expectations_calls % RM_display_expectations_period == 0 || obs_thr < req_thr) {
     fprintf(stderr, "Time: %qd\tObs thr: %f\tReq thr: %f (req/sec)\n", RM_diff_time(now, 0), obs_thr, req_thr);
  }
}

_Monitor_LE* RobustMonitor::find(const enum protocols_e protocol)
{
  _Monitor_LE* ptr = list;
  while (ptr != NULL)
  {
    if (ptr->protocol == protocol)
    {
      return ptr;
    }
    ptr = ptr->next;
  }
  return ptr;
}

void RobustMonitor::switch_protocol(const enum protocols_e protocol) {
   current_protocol_LE = find(protocol);
   current_protocol_name = protocol;

   nb_recv_rep = 0;
   last_switch_time = RM_current_time();
   in_grace_period = true;

   last_client_req.clear();
   last_client_req_notif.clear();
   nb_client_rep_notif.clear();

   fprintf(stderr, "Time: %qd\tRobust monitor switches to %d, obs_thr=%f, req_thr=%f\n", RM_diff_time(last_switch_time, 0), protocol, obs_thr, req_thr);
}

void RobustMonitor::print_expectations(void)
{
  fprintf(stderr, "Robust monitor, time %lld\tobs= %f\treq= %f\n",
      RM_diff_time(RM_current_time(), 0), obs_thr, req_thr);
}

bool RobustMonitor::not_enough_requests(void)
{
#ifndef CLIENT_SENDS_NOTIFICATION
    return false;
#endif

  for (std::map<int, Request_id>::iterator it = last_client_req_notif.begin(); it
      != last_client_req_notif.end(); it++)
  {
    Request_id rid_notif = it->second;
    std::map<int, Request_id>::iterator it2 = last_client_req.find(it->first);
    if (it2 == last_client_req.end())
    {
      fprintf(
          stderr,
          "Client Feedback problem: client %d has not sent request but notif %llu\n",
          it->first, it->second);
      return true;
    }
    else if (it2->second < it->second - NOTIF_PERIOD)
    {
      fprintf(
          stderr,
          "Client Feedback problem: client %d has sent request %llu and notif %llu\n",
          it->first, it2->second, it->second);
      return true;
    }
  }

  return false;
}

bool RobustMonitor::fairness_problem(void) {
#ifndef CLIENT_SENDS_NOTIFICATION
    return false;
#endif

    if (nb_client_rep_notif.size() == 0)
    {
      return false;
    }

  long avg = total_nb_client_rep_notif / nb_client_rep_notif.size();
  for (std::map<int, long>::iterator it = nb_client_rep_notif.begin(); it != nb_client_rep_notif.end(); it++) {
    if (it->second < avg/2 || it->second > avg*1.5) {
      fprintf(stderr, "Fairness problem: avg=%ld, client %d has %ld notifs\n", avg, it->first, it->second);
      return true;
    }
  }

  nb_client_rep_notif.clear();

  return false;
}
