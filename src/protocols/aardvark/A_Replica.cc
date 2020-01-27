#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>  
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h> //for non_blocking socket
#include <errno.h>
#include <netinet/tcp.h> // for TCP_NODELAY
#include <pthread.h>
#include <signal.h>

#include "Array.h"
#include "Array.h"
#include "th_assert.h"
#include "A_Message_tags.h"
#include "A_ITimer.h"
#include "A_Request.h"
#include "A_Pre_prepare.h"
#include "A_Prepare.h"
#include "A_Commit.h"
#include "A_Checkpoint.h"
#include "A_New_key.h"
#include "A_Status.h"
#include "A_Fetch.h"
#include "A_Data.h"
#include "A_Meta_data.h"
#include "A_Meta_data_d.h"
#include "A_View_change.h"
#include "A_View_change_ack.h"
#include "A_New_view.h"
#include "A_Principal.h"
#include "A_Prepared_cert.h"
#include "A_Reply.h"
#include "K_max.h"
#include "A_Verifier_thread.h"
#include "A_Replica.h"
#include "A_Statistics.h"
#include "A_State_defs.h"
#include "A_attacks.h"
#include "RobustMonitor.h"
#include "Switcher.h"
#include "A_parameters.h"
#include "tcp_net.h"

// Global A_replica object.
A_Replica *A_replica;

A_Verifier_thread *A_verifier;

// initial timeout period of the vtimer
int vtimer_initial_timeout;

void print_statistics_before_dying(int sig);

// Force template instantiation
#include "A_Certificate.t"
template class A_Certificate<A_Commit> ;
template class A_Certificate<A_Checkpoint> ;
template class A_Certificate<A_Reply> ;

#include "Log.t"
template class Log<A_Prepared_cert> ;
template class Log<A_Certificate<A_Commit> > ;
template class Log<A_Certificate<A_Checkpoint> > ;

#include "Set.h"
template class Set<A_Checkpoint> ;

static void handle_robust_monitor_message(int s)
{
  A_Message *m = A_replica->A_Node::recv(s);
  delete m;
}

void switch_a_replica(bool state)
{
  if (A_replica != NULL)
    A_replica->enable_replica(state);
}

template<class T>
void A_Replica::retransmit(T *m, A_Time &cur, A_Time *tsent, A_Principal *p)
{
  // re-authenticate.
  m->re_authenticate(p);

#ifdef MSG_DEBUG
  //fprintf(stderr, "RET: %s to %d \n", m->stag(), p->pid());
  //fprintf(stderr, "A_Replica is retransmitting to %i\n", p->pid());
#endif

  // Retransmit message
  send(m, p->pid());
}

A_Replica::A_Replica(FILE *config_file, FILE *config_priv, char *mem, int nbytes,
    int _byz_pre_prepare_delay, int _delay_every_p_pp, bool _small_batches,
    long int _exec_command_delay, int _sliding_window_size) :
  A_Node(config_file, config_priv), rqueue(), ro_rqueue(), plog(max_out),
      clog(max_out), elog(max_out * 2, 0), sset(n()),
      replies(mem, nbytes, num_principals), state(this, mem, nbytes),
      vi(node_id, 0),
      cur_state(replica_state_STOP)
{
  rcv_socks = (int*) malloc(sizeof(int) * num_replicas);
  snd_socks = (int*) malloc(sizeof(int) * num_replicas);
  for (int i = 0; i < num_replicas; i++)
  {
    rcv_socks[i] = -1;
    snd_socks[i] = -1;
  }

  // Fail if A_node is not a A_replica.
  if (!is_replica(id()))
    th_fail("Node is not a replica");

  signal(SIGINT, print_statistics_before_dying);

  // The following two lines implement the 'slow' primary
  byz_pre_prepare_delay = _byz_pre_prepare_delay;
  nb_pp_so_far = 0;
  delay_every_p_pp = _delay_every_p_pp;
  small_batches = _small_batches;
  exec_command_delay = _exec_command_delay;

  great_switcher->register_switcher(instance_id(), switch_a_replica);
  robust_monitor->register_handle_msg_func(this->instance_id(),
      handle_robust_monitor_message);

#ifdef FAIRNESS_ADAPTIVE
  max_rqueue_size = 0;
  executed_snapshot = (Seqno*) malloc(num_principals * sizeof(Seqno));
  for (int j = 0; j < num_principals; j++)
    executed_snapshot[j] = -1;
  fairness_bound = fairness_multiplier * num_principals;
#endif

#ifdef THROUGHPUT_ADAPTIVE

  last_view_time = 0;
  last_cp_time = 0;

  req_throughput = req_throughput_init;
  req_throughput_increment = req_throughput_increment_init;
  first_checkpoint_after_view_change = false;
  highest_throughput_ever = 0;
  vc_already_triggered = false;
  time_to_increment = false;

  last_throughput_of_replica = (float*) malloc(num_replicas * sizeof(float));

  for (int j = 0; j < num_replicas; j++)
    last_throughput_of_replica[j] = req_throughput_init;

#endif
  checkpoints_in_new_view = 0;

#ifdef RRBFT_ATTACK
  nb_pp_since_checkpoint_start = 0;
  nb_req_in_pp_since_checkpoint_start = 0;

#ifdef THROUGHPUT_ADAPTIVE_PERIODIC_COMPUTATION
  nb_req_in_pp_since_last_expectations_computation = 0;
#endif

  next_expected_throughput = 0;

  next_sleeping_time_idx = 0;

  fake_pp = new A_Pre_prepare(true);

  total_nb_sent_pp = 0;
  total_nb_delayed_pp = 0;
#endif

  seqno = 0;
  last_stable = 0;
  low_bound = 0;

  last_prepared = 0;
  last_executed = 0;
  last_tentative_execute = 0;
#ifdef USE_GETTIMEOFDAY 
  last_status.tv_sec=0;
  last_status.tv_nsec=0;
#else
  last_status = 0;
#endif

  limbo = false;
  has_nv_state = true;

  status_messages_badly_needed = false;
  not_deprioritize_status_before_this = 0;

  nbreqs = 0;
  nbrounds = 0;

   nb_retransmissions =0;
   nb_executed =0;

   elapsed_sum1 = 0;
   elapsed_sum2 = 0;
   elapsed_sum3 = 0;
   elapsed_sum4 = 0;



  // Read view change, status, and recovery timeouts from A_replica's portion
  // of "config_file"
  int vt, st, rt;
  fscanf(config_file, "%d\n", &vt);
  fscanf(config_file, "%d\n", &st);
  fscanf(config_file, "%d\n", &rt);

  // Create timers and randomize times to avoid collisions.
  srand48(getpid());

  delay_pre_prepare_timer = new A_ITimer(byz_pre_prepare_delay,
      delay_pre_prepare_timer_handler);

  INIT_LIST_HEAD(&delayed_pps);

#ifdef DELAY_ADAPTIVE 
  //  call_pre_prepare_timer = new A_ITimer(call_pre_prepare_timer_duration, call_pre_prepare_timer_handler);
  pre_prepare_timer = new A_ITimer(pre_prepare_timer_duration,
      pre_prepare_timer_handler);
  pre_prepare_timer->start();
#endif

#ifdef THROUGHPUT_ADAPTIVE
  throughput_timer = new A_ITimer(throughput_timer_duration,
      throughput_timer_handler);
  increment_timer = new A_ITimer(increment_timer_duration,
      increment_timer_handler);

#ifdef THROUGHPUT_ADAPTIVE_PERIODIC_COMPUTATION
  compute_expectations_timer = new A_ITimer(compute_expectations_period,
      compute_expectations_handler);
  compute_expectations_timer->start();
  start_cycle_4_periodic_thr_adaptive_computation = A_currentTime();
  req_count = 0;
#endif
#endif

  req_count_switch = 0;

  vtimer_initial_timeout = vt + lrand48() % 100;
  vtimer = new A_ITimer(vtimer_initial_timeout, A_vtimer_handler);

  stimer = new A_ITimer(st + lrand48() % 100, A_stimer_handler);


#ifdef PERIODICALLY_MEASURE_THROUGHPUT
  first_request_time = 0;
  periodic_thr_measure = new A_ITimer(PRINT_THROUGHPUT_PERIOD, periodic_thr_measure_handler);
  nb_requests_4_periodic_thr_measure = 0;
  next_measure_idx =  0;
  start_cycle_4_periodic_thr_measure = A_currentTime();
  periodic_thr_measure->start();
#endif


  //  stimer->start(); //not started in oldbft, so not starting here

  // Skew recoveries. It is important for nodes to recover in the reverse order
  // of their A_node ids to avoid a view-change every recovery which would degrade
  // performance.
  rec_ready = false;
  ntimer = new A_ITimer(30000 / max_out, A_ntimer_handler);

  recovering = false;
  rr = 0;
  rr_views = new View[num_replicas];
  recovery_point = Seqno_max;
  max_rec_n = 0;

  exec_command = 0;
  non_det_choices = 0;

  print_stuff = true;

  // rewind config_file, and read again all the A_replica lines

  char* fake_string;
  fake_string = (char*) malloc(2000 * sizeof(char));
  int fake_int;

  rewind(config_file);

  //now, skip the useless stuff at the beginning
  fscanf(config_file, "%256s\n", fake_string);
  fscanf(config_file, "%d\n", &fake_int);
  fscanf(config_file, "%d\n", &fake_int);
  fscanf(config_file, "%d\n", &fake_int);
  fscanf(config_file, "%256s %d\n", fake_string, &fake_int);

  //ok, now i am in the right place.
  for (int j = 0; j < num_replicas; j++)
  {
    int rp;
    fscanf(config_file, "%256s %32s %d ", fake_string, fake_string, &rp);

    if (replicas_ports[j] != rp)
    {
      fprintf(
          stderr,
          "Error in the config file: found %i for the port for replica %i instead of %i\n",
          rp, j, replicas_ports[j]);
    }

    for (int k = 0; k < num_replicas; k++)
    {
      fscanf(config_file, "%32s %d ", fake_string, &fake_int);
    }

    fscanf(config_file, "%d %d %1024s \n", &fake_int, &fake_int, fake_string);
  }

  //PL: this is better to close the file descriptor when it is no longer used
  fclose(config_file);

  free(fake_string);

  excluded_clients = false;

  excluded_replicas = (bool*) malloc(num_replicas * sizeof(bool));

  fprintf(stderr, "I am replica %d\n", id());

  for (int j = 0; j < num_replicas; j++)
  {
    //resetting the excluded_replicas array
    excluded_replicas[j] = false;
  }

  status_pending = (A_Message**) malloc(num_replicas * sizeof(A_Message*));
  for (int index = 0; index < num_replicas; index++)
    status_pending[index] = NULL;
  status_to_process = 0;
  s_identity = -1;

  //blacklist malloc and initialization
  blacklisted = (bool*) malloc(sizeof(bool) * num_principals);
  for (int i = 0; i < num_principals; i++)
    blacklisted[i] = false;

  bootstrap_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (bootstrap_socket == -1)
  {
    perror("Error while creating the socket! ");
    exit(errno);
  }

  // TCP NO DELAY
  int flag = 1;
  int result = setsockopt(bootstrap_socket, IPPROTO_TCP, TCP_NODELAY,
      (char*) &flag, sizeof(int));
  if (result == -1)
  {
    perror("Error while setting TCP NO DELAY! ");
  }

  // 2) bind on it
  bootstrap_sin.sin_addr.s_addr = htonl(INADDR_ANY);
  bootstrap_sin.sin_family = AF_INET;
  bootstrap_sin.sin_port = htons(BOOTSTRAP_PORT + id()); // we add id so that it is possible to launch the replicas on the same machine

  if (bind(bootstrap_socket, (struct sockaddr*) &bootstrap_sin,
      sizeof(bootstrap_sin)) == -1)
  {
    perror("Error while binding to the socket! ");
    exit(errno);
  }

  // 3) make the socket listening for incoming connections
  if (listen(bootstrap_socket, num_replicas + 1) == -1)
  {
    perror("Error while calling listen! ");
    exit(errno);
  }

  // 4) connect to the other replicas
  for (int i = 0; i <= id(); i++)
  {
    // 1) create the socket
    snd_socks[i] = socket(AF_INET, SOCK_STREAM, 0);
    if (snd_socks[i] == -1)
    {
      perror("Error while creating the socket! ");
      exit(errno);
    }

    // 2)  connect to the server
    // since we have multiple NICs, use replicas_ipaddr[], and not replicas_hostname[]
    struct sockaddr_in addr;
    addr.sin_addr.s_addr = inet_addr(replicas_ipaddr[i]);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(BOOTSTRAP_PORT + i); // we add i so that it is possible to launch the replicas on the same machine

    // 2b) TCP NO DELAY
    int flag = 1;
    int result = setsockopt(snd_socks[i], IPPROTO_TCP, TCP_NODELAY,
        (char *) &flag, sizeof(int));
    if (result == -1)
    {
      perror("Error while setting TCP NO DELAY! ");
    }

    // 2c) connect
    while (true)
    {
      if (connect(snd_socks[i], (struct sockaddr *) &(addr), sizeof(addr)) < 0)
      {
        perror("Cannot connect");
        sleep(1);
      }
      else
      {
        fprintf(stderr, "Connection successful from %i to %i!\n", id(), i);
        break;
      }
    }
  }

  // 5) accepting connections from my id to the last A_replica
  for (int i = 0; i < num_replicas - id(); i++)
  {
    struct sockaddr_in csin;
    int sinsize = sizeof(csin);

    int rcv_sock;
    while ((rcv_sock = accept(bootstrap_socket, (struct sockaddr*) &csin, (socklen_t*) &sinsize)) < 0)
    {
        if (errno != EINTR)
        {
            perror("Cannot accept connection \n");
            exit(-1);
        }
    }

    // TCP NO DELAY
    int flag = 1;
    int result = setsockopt(rcv_sock, IPPROTO_TCP, TCP_NODELAY, (char *) &flag,
        sizeof(int));
    if (result == -1)
    {
      perror("Error while setting TCP NO DELAY! ");
    }

    char *hostname = inet_ntoa(csin.sin_addr);

    int id_of_rcv = 0;
    while (id_of_rcv < num_replicas)
    {
      if (!strcmp(hostname, replicas_ipaddr[id_of_rcv]))
      {
        break;
      }
      id_of_rcv++;
    }

    if (id_of_rcv >= num_replicas)
    {
      fprintf(stderr, "Unknown host: %s\n", hostname);
    }

    rcv_socks[id_of_rcv] = rcv_sock;

    // print some information about the accepted connection
    fprintf(stderr, "A connection has been accepted from %s:%i, id=%i\n",
        hostname, ntohs(csin.sin_port), id_of_rcv);
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

#ifdef E_AARDVARK
  sliding_window_size = _sliding_window_size;

  fprintf(stderr, "Size of the sliding window: %d\n", sliding_window_size);

  if (sliding_window_size != 0)
  {
    sliding_window = (float*) malloc(sizeof(float) * sliding_window_size);
    for (int i = 0; i < sliding_window_size; i++)
      sliding_window[i] = 0;
    sliding_window_cur_idx = 0;
    sliding_window_nb_got = 0;
  }
#endif
  total_nb_checkpoints = 0;

  // old code which uses the original circular buffer
#if 0
  circular_buffer_magic = (A_Message*) 0x12344321; //I don't want to read that value

  circular_buffer = (A_Message**) malloc(circular_buffer_size * sizeof(A_Message*));
  for (int i = 0; i < circular_buffer_size; i++)
    circular_buffer[i] = circular_buffer_magic;
  cb_write_index = 0;
  cb_read_index = 0;
#endif

  verifier_thr_to_replica_buffer = new A_Circular_buffer(circular_buffer_size);

  // PL: we have moved the creation of the main at the end of the constructor
  // It should not change anything but it is cleaner than having it in the middle
  // of the code
  A_verifier = NULL;
  pthread_t thread;
  int rc;

  //fprintf(stderr, "port number (from A_replica) %d\n",principals[id()]->address()->sin_port);
  A_verifier = new A_Verifier_thread();

  rc = pthread_create(&thread, NULL, Verifier_thread_startup, A_verifier);
  if (rc)
  {
    fprintf(stderr, "Failed to create a thread\n");
    exit(1);
  }

  // associate this thread to CPU 0
  cpu_set_t cpuset1;
  CPU_ZERO(&cpuset1);
  CPU_SET(0, &cpuset1);

  int s = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset1);
  if (s != 0)
  {
    fprintf(stderr, "Error while associating this thread to CPU 0\n");
  }

  // associate the A_verifier to CPU 1
  cpu_set_t cpuset2;
  CPU_ZERO(&cpuset2);
  CPU_SET(1, &cpuset2);

  s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset2);
  if (s != 0)
  {
    fprintf(stderr, "Error while associating Verifier thread to CPU 1\n");
  }
}

void A_Replica::register_exec(
    int(*e)(Byz_req *, Byz_rep *, Byz_buffer *, int, bool, long int))
{ //last long int is the execute delay
  exec_command = e;
}

void A_Replica::register_nondet_choices(void(*n)(Seqno, Byz_buffer *),
    int max_len)
{

  non_det_choices = n;
  max_nondet_choice_len = max_len;
}

void A_Replica::compute_non_det(Seqno s, char *b, int *b_len)
{
  if (non_det_choices == 0)
  {
    *b_len = 0;
    return;
  }
  Byz_buffer buf;
  buf.contents = b;
  buf.size = *b_len;
  non_det_choices(s, &buf);
  *b_len = buf.size;
}

A_Replica::~A_Replica()
{
}

void A_Replica::recv()
{
  // Compute session keys and send initial new-key message.
  A_Node::send_new_key();

  // Compute digest of initial state and first checkpoint.
  state.compute_full_digest();

  // Start status and authentication freshness timers
  stimer->start();
  atimer->start();
  if (id() == primary())
    ntimer->start();

  // Allow recoveries
  rec_ready = true;

  fprintf(stderr, "Replica ready\n");

  if (A_floodMax())
  {
    fprintf(stderr, "I am Byzantine and will flood to the max.\n");
  }
  if (A_floodProtocol())
  {
    fprintf(stderr, "I am Byzantine and will flood with protocol messages.\n");
  }

  int count_idle = 0;
  //int max_queue_size=0;
  //int silly_counter=1;

#ifdef REPLICA_FLOOD_PROTECTION

  int max_replica_messages;
  int max_replica_messages_index;
  int second_max_replica_messages;
  int second_max_replica_messages_index;

  for (int k = 0; k < 5; k++)
    rmcount[k] = 0;
  flood_protection_active = true;
  flood_protection_view = 0;
#endif
  long int client_count = 0;

  unsigned long long count = 0;

  A_Message* m;
  while (1)
  {
    int maxsock = 0;

    // [attacks] Check whether we need to perform a specific type of attack.
    if (A_floodMax())
    {
        // A_replica flooding. This A_node is detected and its NIC is deactivated,
        // so it does nothing
        /*
      // flood with max-sized messages
      A_Message m2(A_Max_message_size);
      A_Message::set_mac_unvalid((A_Message_rep*) m2.contents());
      m2.set_size(A_Max_message_size);

      if (count % 25 == 0)
      {
        // do not flood myself :)
        for (int x = 0; x < num_replicas; x++)
        {
          if (x != id())
            send(&m2, x);
        }
      }

      count++;
      */

      /*******************************************************************************************/
      // With TCP, if the receiver buffer is full, then the packets will be discarded.
      // However, since the sender is waiting for the ACKs of the sent packets, it will
      // retransmit them (at the kernel level).
      // to avoid this problem, the faulty A_replica reads messages coming from the replicas.
      FD_ZERO(&file_descriptors); //initialize file descriptor set

      for (int j = 0; j < num_replicas; j++)
      {
        FD_SET(rcv_socks[j], &file_descriptors);
        maxsock = MAX(maxsock, rcv_socks[j]);
      }

      listen_time.tv_sec = 0;
      listen_time.tv_usec = 500;

      select(maxsock + 1, &file_descriptors, NULL, NULL, &listen_time);

      for (int j = 0; j < num_replicas; j++)
      {
        //I want to listen at this A_replica
        if (FD_ISSET(rcv_socks[j], &file_descriptors))
        {
          A_Message* mp = new A_Message(A_Max_message_size);

          // 1) first of all, we need to receive the A_Message_rep (in order to get the message size)
          int msg_rep_size = recvMsg(rcv_socks[j], (void*) mp->contents(),
              sizeof(A_Message_rep));

          // 2) now that we have the size of the message, receive the content
          int msg_size = recvMsg(rcv_socks[j],
              (void*) ((char*) mp->contents() + msg_rep_size),
              mp->size() - msg_rep_size);

          int ret = msg_rep_size + msg_size;

          delete mp;
        }
      }
      /*******************************************************************************************/

      continue;
    }

    if (A_floodProtocol())
    {
      // flood with protocol messages (status and request)
      send_status(true);
      state.send_fetch(true);
      continue;
    }

    FD_ZERO(&file_descriptors); //initialize file descriptor set

    FD_SET(verifier_thr_to_replica_buffer->fd, &file_descriptors);
    maxsock = MAX(maxsock, verifier_thr_to_replica_buffer->fd);

    for (int j = 0; j < num_replicas; j++)
      if (!excluded_replicas[j])
      {
        FD_SET(rcv_socks[j], &file_descriptors);
        maxsock = MAX(maxsock, rcv_socks[j]);
      }

    listen_time.tv_sec = 0;
    listen_time.tv_usec = 500;

    bool idle = true;
    select(maxsock + 1, &file_descriptors, NULL, NULL, &listen_time);

    A_ITimer::handle_timeouts();

    if (id() == primary() && id() == 0 && byz_pre_prepare_delay) {
      struct delayed_pp *dpp;
      struct list_head *pos, *n;
      list_for_each_safe(pos, n, &delayed_pps) {
        dpp = list_entry(pos, struct delayed_pp, link);

        unsigned long long now = A_rdtsc();
        if (A_diffTime(now, dpp->t)/1000.0 >= (float)byz_pre_prepare_delay) {
          //fprintf(stderr, "now= %qd ppt= %qd diff= %f pp_delay= %f\n", A_diffTime(now, 0), A_diffTime(dpp->t, 0), A_diffTime(now, dpp->t)/1000.0, (float)byz_pre_prepare_delay);

          send(dpp->pp, All_replicas);
          plog.fetch(seqno).add_mine(dpp->pp);

          list_del(pos);
          delete dpp;
        } else {
          break;
        }
      }
    }

    if (!excluded_clients && FD_ISSET(verifier_thr_to_replica_buffer->fd, &file_descriptors))
    {
      // old code which uses the original circular buffer
#if 0
      m = cb_read_message();
#endif

      m = verifier_thr_to_replica_buffer->cb_read_msg();

      //th_assert(m != circular_buffer_magic, "Ooooops... messed up with circular buffer...");
      //if (m != circular_buffer_magic && m)
      if (m)
      {
        //buffer was not empty, read something
        client_count++;
        jump_table(m);

        //if( primary() == id()  && has_new_view() && (client_count < np()-n()) && (!rqueue.full_batch() && rqueue.size() < (np()-n())/2))
      //  if (primary() == id() && has_new_view() && !rqueue.full_batch())
      //    continue;
      }
    }

    for (int j = 0; j < num_replicas; j++)
    {
      // for each A_replica
      if (!excluded_replicas[j])
      {
        //I want to listen at this A_replica
        if (FD_ISSET(rcv_socks[j], &file_descriptors))
        {
          client_count = 0;
          idle = false;

#ifdef REPLICA_FLOOD_PROTECTION
          if (flood_protection_active)
          {
            /*
             if (j != primary())
             {
             */
            rmcount[j]++;
            rmcount[4]++;
            //}

            if (rmcount[4] > check_rmcount)
            {

              //time to check. compute max and second_max
              //fprintf(stderr, "A_Replica %i: %d %d %d %d %d\n", id(), rmcount[0],
              //    rmcount[1], rmcount[2], rmcount[3], rmcount[4]);

              max_replica_messages = -1;
              max_replica_messages_index = -1;
              second_max_replica_messages = -1;
              second_max_replica_messages_index = -1;

              // get the first max
              for (int k = 0; k < 4; k++)
              {
                /*
                 if (k != primary())
                 {
                 */
                //for all the non-primary replicas
                if (rmcount[k] > max_replica_messages)
                {
                  max_replica_messages = rmcount[k];
                  max_replica_messages_index = k;
                }
                //}
              }

              // get the second max
              for (int k = 0; k < 4; k++)
              {
                /*
                 if (k != primary())
                 {
                 */
                //for all the non-primary replicas
                if (k != max_replica_messages_index && rmcount[k]
                    > second_max_replica_messages)
                {
                  second_max_replica_messages = rmcount[k];
                  second_max_replica_messages_index = k;
                }
                //}
              }

              // if the values in the array are all the same ones, then set the second max to the first one
              // not to verify the assert
              if (second_max_replica_messages == -1)
              {
                second_max_replica_messages = max_replica_messages;
                second_max_replica_messages_index = max_replica_messages_index;
              }

              /*
               fprintf(
               stderr,
               "max_replica_messages = %i, second_max_replica_messages = %i\n",
               max_replica_messages, second_max_replica_messages);
               */th_assert((max_replica_messages != -1)
                  && (second_max_replica_messages != -1),
                  "Wrong computation of max_replica_message");

              // computed max and second_max. check triggering condition

              if (max_replica_messages >= second_max_replica_messages
                  * flood_detection_factor)
              {
                //FLOOD DETECTED! max_replica_messages_index is the flooder
                fprintf(
                    stderr,
                    " ********* FLOOD PROTECTION: excluding replica %d: %i >= %i\n",
                    max_replica_messages_index, max_replica_messages,
                    second_max_replica_messages * flood_detection_factor);
                //excluded_replicas[max_replica_messages_index] = true;
                flood_protection_active = false;
                flood_protection_view = view();

                //PL: we do not disconnect nodes since we do not use multiple interfaces
                //char command[30];
                //sprintf(command, "./disconnect_node A_node%d &\n",
                //    max_replica_messages_index);
                //fprintf(stderr, "********** Executing Command %s\n", command);
                //system(command);

              }

              for (int j = 0; j < 5; j++)
                rmcount[j] = 0;
            }
          }
#endif

          A_Message* mp = new A_Message(A_Max_message_size);
          // 1) first of all, we need to receive the A_Message_rep (in order to get the message size)
          int msg_rep_size = recvMsg(rcv_socks[j], (void*) mp->contents(),
              sizeof(A_Message_rep));

          // 2) now that we have the size of the message, receive the content
          int msg_size = recvMsg(rcv_socks[j],
              (void*) ((char*) mp->contents() + msg_rep_size),
              mp->size() - msg_rep_size);

          int ret = msg_rep_size + msg_size;

          if (ret >= (int) sizeof(A_Message_rep) && ret >= mp->size())
          {
            jump_table(mp);
          }
          else
          {
            delete mp;
            if (ret < 0)
              fprintf(stderr, "errno: %s\n", strerror(errno));
          }
        }
      }
    }

    //checked clients and all the replicas, if idle is still true, I have nothing to do...
    if (idle)
    {
      count_idle++;
    }
    else
    {
      count_idle = 0;
    }

    if (count_idle > 3)
    {
      A_Message* m = pick_next_status();
      if (m)
      {
        gen_handle<A_Status> (m);
      }
    }
  }
}

void A_Replica::send(A_Message *m, int i)
{
  th_assert(i == All_replicas || (i >= 0 && i < num_principals),
      "Invalid argument");
  m->check_msg();

  if (i == All_replicas)
  {
    for (int x = 0; x < num_replicas; x++)
    {
      //  if (x != id()) //some code relies on receiving self-created messages
      A_replica->send(m, x);
    }
    return;
  }

  th_assert(i != All_replicas,
      "the multisocket version does not work well with multicast... \n");

  if (i < num_replicas)
  {
    sendMsg(snd_socks[i], m->contents(), m->size());
  }
  else
  {
    sendMsg(clients_socks[i - num_replicas], m->contents(), m->size());
  }
}

// verify a request comming from a A_Client
A_Request* A_Replica::verify(A_Wrapped_request* wrapped)
{
  if (blacklisted[wrapped->client_id()])
  {
    /*
     fprintf(stderr, "[A_Replica %i] client %i is blacklisted\n",
     A_replica->id(), wrapped->client_id());
     */
    delete wrapped;
    return NULL;
  }

  bool verified_wrap = wrapped->verify_MAC();
  if (!verified_wrap)
  {
/*
     fprintf(stderr,
     "[A_Replica %i] request %qd, mac from client %i is not valid\n",
     A_replica->id(), wrapped->seqno(), wrapped->client_id());
*/

    delete wrapped;
    return NULL;
  }

  // verify_MAC returned true, so digest and MAC are ok.
  // now we call verify_request to verify the signature.

  verified_wrap = wrapped->verify_request();
  if (!verified_wrap)
  {
    // adding the sender of this wrapped request to the blacklist
    blacklisted[wrapped->client_id()] = true;
    fprintf(stderr,
        "*** +++ --- \\\\\\ Blacklisted client %d /// --- +++ ***\n",
        wrapped->client_id());
    delete wrapped;
    return NULL;
  }

  A_Wrapped_request::Requests_iter iter(wrapped);
  A_Request req;
  iter.get(req);

  A_Request* to_write;
  to_write = req.clone();

  delete wrapped;
  return to_write;
}

void A_Replica::jump_table(A_Message* m)
{

  th_assert(m, "null message...");

  // TODO: This should probably be a jump table.
  // PL: NOTE: if there is no #ifdef MSG_DEBUG for some tags, this is because
  // this is done in the function which handles this type of tags.
  switch (m->tag())
  {

  case A_Request_tag:
    //fprintf(stderr, "A");
    gen_handle<A_Request> (m);
    break;

  case A_Pre_prepare_tag:
    //fprintf(stderr, "B");
    gen_handle<A_Pre_prepare> (m);
    break;

  case A_Prepare_tag:
    //fprintf(stderr, "C");
    gen_handle<A_Prepare> (m);
    break;

  case A_Commit_tag:
    //fprintf(stderr, "D");
    gen_handle<A_Commit> (m);
    break;

  case A_Checkpoint_tag:
    //fprintf(stderr, "E");
#ifdef MSG_DEBUG
    fprintf(stderr, "Replica %i (primary=%i) (view %qd) handles a Checkpoint\n", this->id(), primary(), view());
#endif
    gen_handle<A_Checkpoint> (m);
    break;

  case A_New_key_tag:
    //fprintf(stderr, "F");
#ifdef MSG_DEBUG
    fprintf(stderr, "Replica %i handles a New key\n", this->id());
#endif
    gen_handle<A_New_key> (m);
    break;

  case A_View_change_ack_tag:
    //fprintf(stderr, "G");
    gen_handle<A_View_change_ack> (m);
    break;

  case A_Status_tag:
    //fprintf(stderr, "H");

    if (status_messages_badly_needed)
    {
      gen_handle<A_Status> (m);
    }
    else
    {
      //for now, just add this message to status_pending
      s_identity = ((A_Status*) m)->id();
      th_assert(s_identity >= 0 && s_identity < num_replicas,
          "meaningless s_identity\n");
      if (status_pending[s_identity] != NULL)
      {
        delete status_pending[s_identity];
        //fprintf(stderr, "removed older status from %d to status_pending\n",identity);
      }
      //fprintf(stderr, "added status from %d to status_pending\n",identity);
      status_pending[s_identity] = m;
    }
    break;

  case A_Fetch_tag:
    //fprintf(stderr, "I");
#ifdef MSG_DEBUG
    fprintf(stderr, "Replica %i (primary=%i) (view %qd) handles a Fetch\n", this->id(), primary(), view());
#endif
    gen_handle<A_Fetch> (m);
    break;

  case A_Reply_tag:
    gen_handle<A_Reply> (m);
    break;

  case A_Meta_data_tag:
    //fprintf(stderr, "O");
#ifdef MSG_DEBUG
    fprintf(stderr, "Replica %i (primary=%i) (view %qd) handles a Meta data\n", this->id(), primary(), view());
#endif
    gen_handle<A_Meta_data> (m);
    break;

  case A_Meta_data_d_tag:
    //fprintf(stderr, "P");
#ifdef MSG_DEBUG
    fprintf(stderr, "Replica %i (primary=%i) (view %qd) handles a Meta_data_d\n", this->id(), primary(), view());
#endif
    gen_handle<A_Meta_data_d> (m);
    break;

  case A_Data_tag:
    //fprintf(stderr, "Q");
#ifdef MSG_DEBUG
    fprintf(stderr, "Replica %i  (primary=%i) (view %qd) handles a Data\n", this->id(), primary(), view());
#endif
    gen_handle<A_Data> (m);
    break;

  case A_View_change_tag:
    //fprintf(stderr, "R");
    gen_handle<A_View_change> (m);
    break;

  case A_New_view_tag:
    //fprintf(stderr, "S");
    gen_handle<A_New_view> (m);
    break;

  case A_Wrapped_request_tag:
    //fprintf(stderr, "**** Main Thread should not receive wrapped requests...");

#ifdef MSG_DEBUG
    fprintf(stderr, "Replica %i (primary=%i) (view %qd) handles a Wrapped request\n", this->id(), primary(), view());
#endif

    if (id() == primary()) {
      A_Wrapped_request::Requests_iter iterator((A_Wrapped_request*)m);
      A_Request req;
      while (iterator.get(req))
      {
        A_Request *r = req.clone();
        gen_handle<A_Request>(r);
      }
    }
    delete m;

    break;

  default:
    // Unknown message type.
    //fprintf(stderr, "**** Received garbage, deleting\n");
#ifdef MSG_DEBUG
    fprintf(stderr, "Replica %i (primary=%i) (view %qd) handles an unknown message\n", this->id(), primary(), view());
#endif
    delete m;
  }
}

void A_Replica::handle(A_Request *m)
{

  //if (print_stuff)
  //    fprintf(stderr, "\t\t\t\t\t\thandling request\n");

  int cid = m->client_id();
  bool ro = m->is_read_only();
  Request_id rid = m->request_id();

  /*
   fprintf(stderr, "A_Replica %i (Primary=%i) handles request %qu from client %i\n", id(),
   primary(), rid, cid);
   */

#ifdef AARDVARK_DO_SWITCHING
  if (cur_state == replica_state_STOP)
  {
    if (!is_replica(cid))
    {
      A_Reply rr(view(), m->request_id(), node_id, replies.digest(cid),
          i_to_p(cid), true);
      rr.set_instance_id(next_instance_id());
      rr.authenticate(i_to_p(cid), 0, true);
      send(&rr, cid);
    }
    delete m;
    return;
  }

  if (req_count_switch == AARDVARK_NB_REQS_BEFORE_SWITCHING)
  {
    cur_state = replica_state_STOP;
    great_switcher->switch_state(instance_id(), false);
    great_switcher->switch_state(next_instance_id(), true);
  }
  req_count_switch++;
#endif

  // [attacks] Bias on clients?
  if (A_clientBias(m))
  {
    delete m;
    return;
  }

  if (has_new_view())
  {

    // A_Replica's requests must be signed and cannot be read-only.
    if (!is_replica(cid) || (m->is_signed() & !ro))
    {
      if (ro)
      {
        // Read-only requests.
        if (execute_read_only(m) || !ro_rqueue.append(m))
          delete m;

        return;
      }
      Request_id last_rid = replies.req_id(cid);
      if (last_rid < rid)
      {
        // A_Request has not been executed.
        if (id() == primary())
        {
          if (!rqueue.in_progress(cid, rid, v))
          {
            if (rqueue.append(m))
            {
              //      fprintf(stderr, "RID %qd. ", rid);
              send_pre_prepare();
              return;
            }
          }
        }
        else
        {
          if (m->size() > A_Request::big_req_thresh && brt.add_request(m))
            return;

          if (rqueue.append(m))
          {
#if 0
            if (!limbo)
            {
              A_Req_queue tmp; //req_queue used just to store the request, because wrapped request constructor takes a request queue as a parameter
              //It can be easily modified to accept a request* instead of a request queue, but I prefer to do exactly the same things
              //that are done for the generation of a A_Pre_prepare message.
              A_Request *cloned = m->clone();
              tmp.append(cloned);
              A_Wrapped_request* wrapped = new A_Wrapped_request((Seqno) id(),
                  (View) 1, tmp);
#ifdef MSG_DEBUG
              fprintf(stderr, "Replica %i, Primary=%i, request %qu from client %i: Sending a wrapped request to the primary\n",
                  id(), primary(), rid, cid);
#endif

              A_Message *m = (A_Message*) wrapped;
              sendMsg(snd_socks[primary()], m->contents(), m->size());

              delete wrapped;

              //vtimer->start(); //??? necessary? useful?
            }
#endif
            return;
          }
        }
      }
      else if (last_rid == rid)
      {
        // Retransmit reply.
        replies.send_reply(cid, view(), id());
        nb_retransmissions++;

        if (id() != primary() && !replies.is_committed(cid) && rqueue.append(m))
        {
          //fprintf(stderr, "vtimer restart (5)\n");
          vtimer->start();
          return;
        }
      }
    }
  }
  else
  {
    if (m->size() > A_Request::big_req_thresh && !ro && brt.add_request(m, false))
    {
      th_assert(false, "big requests are disabled");
      return;
    }
  }

  delete m;
}

#ifdef NEW_BIG_MAC_ATTACK
// send the PP to only 2f replicas
// called by A_replica 0 when it is the primary
void A_Replica::send_pp_to_2f_replicas(A_Pre_prepare *pp)
{
  th_assert(id() == 0 && primary() == id(), "send_pp_to_2f_replicas() must be called only by replica 0 when it is the primary");

  for (int i = 0; i < num_replicas; i++)
  {
    if (i > 2 * A_node->f())
    {
      A_Message::set_mac_unvalid((A_Message_rep*) (pp->contents()));
    }

    send(pp, i);
  }

  A_Message::set_mac_valid((A_Message_rep*) (pp->contents()));
  plog.fetch(seqno).add_mine(pp);
}
#endif

void A_Replica::send_pre_prepare(bool force_send)
{
  if (!has_new_view())
  {
    //    fprintf(stderr, "Should not be here!!!!!\n");
    return;
  }
  th_assert(primary() == id(), "Non-primary called send_pre_prepare");

  /*
  // if the parameter byz_pre_prepare_delay is not 0, this
  // is a byzantine A_replica that waits a delay of
  // byz_pre_prepare_delay us before sending the pre_prepare
  if (byz_pre_prepare_delay && !force_send)
  {
    nb_pp_so_far++;
    if (nb_pp_so_far >= delay_every_p_pp)
    {
      nb_pp_so_far = 0;
      delay_pre_prepare_timer->restart();
      return;
    }
  }
  */

  // If rqueue is empty there are no requests for which to send
  // pre_prepare and a pre-prepare cannot be sent if the seqno excedes
  // the maximum window or the A_replica does not have the new view.
  if ((force_send || rqueue.size() > 0) && seqno + 1 <= last_executed
      + congestion_window && seqno + 1 <= max_out + last_stable
      && has_new_view())
  {
    //  fprintf(stderr, "requeu.size = %d\n", rqueue.size());
    //  if (seqno % checkpoint_interval == 0)
    //if (print_stuff)
    //  fprintf(stderr, "SND: PRE-PREPARE seqno= %qd last_stable= %qd\n", seqno+1, last_stable);

    long old_size = rqueue.size();

#ifdef RRBFT_ATTACK
    if (id() == MALICIOUS_PRIMARY_ID && checkpoints_in_new_view > number_of_checkpoints_before_adpating)
    {
      int sleeping_time;
      int batch_size;
      int d1, d2;

      // compute the delay
      batch_size = rqueue.size();

#ifdef THROUGHPUT_ADAPTIVE_PERIODIC_COMPUTATION
      float last_expectations_computation_time_in_ms = A_diffTime(A_currentTime(),
          start_cycle_4_periodic_thr_adaptive_computation) / 1000.0;
      // d1 = (nb_pp_since_checkpoint_start+1) * pre_prepare_timer_duration / (float)expected_pre_prepares - last_cp_time_in_ms - MAX_LATENCY_PP_SEND_RECV;
      d2 = (nb_req_in_pp_since_last_expectations_computation + batch_size)
              / (next_expected_throughput * 1000.0)
              - last_expectations_computation_time_in_ms - MAX_LATENCY_PP_SEND_EXEC;

      // next_expected_throughput may be equal to 0 at the beginning of the experiment,
      // before the first call to the compute expectations handler
      if (next_expected_throughput == 0) {
          d2 = 0;
      }

#else
      float last_cp_time_in_ms = A_diffTime(A_currentTime(), last_cp_time) / 1000.0;
      // d1 = (nb_pp_since_checkpoint_start+1) * pre_prepare_timer_duration / (float)expected_pre_prepares - last_cp_time_in_ms - MAX_LATENCY_PP_SEND_RECV;
      d2 = (nb_req_in_pp_since_checkpoint_start + batch_size)
          / (next_expected_throughput * 1000.0) - last_cp_time_in_ms
          - MAX_LATENCY_PP_SEND_EXEC;
#endif

      // delay by the delay
      // sleeping_time = MIN(d1, d2); // in ms
      sleeping_time = MIN(pre_prepare_timer_duration / expected_pre_prepares, d2) - 10; // in ms.
      // -10 is the epsilon. We need the epsilon (not necessarily this value), otherwise
      // the attack does not last for a long time

      if (sleeping_time > 0)
      {
        if (next_sleeping_time_idx < MAX_NB_SLEEPING_TIME_LOGGED)
        {
          sleeping_times[next_sleeping_time_idx] = sleeping_time;
          next_sleeping_time_idx++;
        }
        total_nb_delayed_pp++;

        usleep(sleeping_time * 1000);
      }
    }
#endif


    if (nbreqs >= periodic_batch_size_display)
    {
        //fprintf(stderr, "Avg batch sz: %f\n", (float) nbreqs / (float) nbrounds);
        nbreqs = nbrounds = 0;
    }


    // Create new pre_prepare message for set of requests
    // in rqueue, log message and multicast the pre_prepare.
    seqno++;
    //    fprintf(stderr, "Sending PP seqno %qd\n", seqno);

    A_Pre_prepare *pp = new A_Pre_prepare(view(), seqno, rqueue);
    nbreqs += old_size - rqueue.size();
    nbrounds++;
    // TODO: should make code match my proof with request removed
    // only when executed rather than removing them from rqueue when the
    // pre-prepare is constructed.


    //if (print_stuff)
    //fprintf(stderr, "SND:  pp: (%qd, %qd),  last stable: %qd\n",seqno, pp->view(),  last_stable);
#ifdef MSG_DEBUG
    fprintf(stderr, "Replica %i, Primary=%i: Sending a pre prepare with seqno= %qd to all replicas\n", id(), primary(), pp->seqno());
#endif

#ifdef NEW_BIG_MAC_ATTACK
    if (id() == 0)
    {
      send_pp_to_2f_replicas(pp);
    }
    else
    {
#endif

      if (byz_pre_prepare_delay && ++nb_pp_so_far >= delay_every_p_pp)
      {
        struct delayed_pp *dpp = new struct delayed_pp;
        dpp->pp = pp;
        dpp->t = A_rdtsc();
        list_add_tail(&(dpp->link), &delayed_pps);
        nb_pp_so_far = 0;
        //fprintf(stderr, "adding pp @ %qd usec\n", A_diffTime(dpp->t, 0));
      } else {
        plog.fetch(seqno).add_mine(pp);
        send(pp, All_replicas);
      }

#ifdef NEW_BIG_MAC_ATTACK
    }
#endif


#ifdef RRBFT_ATTACK
    if (id() == MALICIOUS_PRIMARY_ID)
    {
      // compute the nb of PP and the nb of requests sent since the beginning of the checkpoint
      nb_pp_since_checkpoint_start++;
      nb_req_in_pp_since_checkpoint_start += old_size - rqueue.size();

#ifdef THROUGHPUT_ADAPTIVE_PERIODIC_COMPUTATION
      nb_req_in_pp_since_last_expectations_computation += old_size - rqueue.size();
#endif

      total_nb_sent_pp++;
    }
#endif

    //    vtimer->stop(); //not in old pbft, so commented here
  }
  else
  {
    /*
     fprintf(stderr, "A_Replica %i, Primary=%i: pre prepare %qd has not been sent\n", id(),
     primary(), seqno);
     */

    /*
     printf(
     "force_send=%s, rqueue.size()=%i, seqno=%qd, last_executed=%qd, congestion_window=%i, max_out=%i, last_stable=%qd, has_new_view()=%s\n",
     (force_send) ? "true" : "false", rqueue.size(), seqno, last_executed,
     congestion_window, max_out, last_stable, (has_new_view()) ? "true"
     : "false");
     */

  }

}

template<class T>
bool A_Replica::in_w(T *m)
{
  const Seqno offset = m->seqno() - last_stable;

  if (offset > 0 && offset <= max_out)
    return true;

  if (offset > max_out && m->verify())
  {
    // Send status message to obtain missing messages. This works as a
    // negative ack.
    send_status();
  }

  return false;
}

template<class T>
bool A_Replica::in_wv(T *m)
{
  const Seqno offset = m->seqno() - last_stable;

  if (offset > 0 && offset <= max_out && m->view() == view())
    return true;

  if ((m->view() > view() || offset > max_out) && m->verify())
  {
    // Send status message to obtain missing messages. This works as a
    // negative ack.
    send_status();
  }

  return false;
}

void A_Replica::handle(A_Pre_prepare *m)
{
  const Seqno ms = m->seqno();

#ifdef MSG_DEBUG
  fprintf(stderr, "Replica %i (primary %i) handles a PrePrepare with seqno= %qd from replica %i in view %qd which contains requests\n", this->id(), primary(), ms, m->id(), m->view());

  /*
   A_Pre_prepare::Requests_iter iter(m);
   A_Request req;

   while (iter.get(req))
   {
   fprintf(stderr, " %qu (%i)", req.request_id(), req.client_id());
   }
   fprintf(stderr, "\n");
   */
#endif

  //if(print_stuff)
  //  fprintf(stderr, "\tRCV: pp (%qd, %qd) from %d\n",ms,view(),m->id());

  Byz_buffer b;

  b.contents = m->choices(b.size);

  if (in_wv(m))
  {
    if (ms > low_bound)
    {
      if (has_new_view())
      {
        A_Prepared_cert& pc = plog.fetch(ms);

        /*
         printf(
         "\tReplica %i (primary %i) handles a PrePrepare with seqno= %qd from A_replica %i in view %qd with quorum=%i/%i\n",
         this->id(), primary(), ms, m->id(), m->view(), pc.num_correct(),
         pc.num_complete());
         */

        // Only accept message if we never accepted another pre-prepare
        // for the same view and sequence number and the message is valid.
        int replica_sender_id = m->id();
        View replica_sender_view = m->view();
        if (pc.add(m))
        {
#ifdef DELAY_ADAPTIVE
          received_pre_prepares++;
#endif
          //      fprintf(stderr, "\t\t\t\tsending prepare\n");

          /*
           fprintf(stderr, "\tReplica %i (primary %i) sends a A_Prepare\n", this->id(),
           primary());
           */

          send_prepare(pc);
          if (pc.is_complete())
          {
            send_commit(ms);
          }
          else
          {
            /*
             printf(
             "\tReplica %i (primary %i) handles a PrePrepare with seqno= %qd from A_replica %i in view %qd, quorum not complete\n",
             this->id(), primary(), ms, replica_sender_id,
             replica_sender_view);
             */
          }
        }
        else
        {
          /*
           printf(
           "\tReplica %i (primary %i) handles a PrePrepare with seqno= %qd from A_replica %i in view %qd, failed to add the pre-prepare\n",
           this->id(), primary(), ms, replica_sender_id, replica_sender_view);
           */
        }

        return;
      }
      else
      {
        /*
         printf(
         "\tReplica %i (primary %i) handles a PrePrepare with seqno= %qd from A_replica %i in view %qd, has_new_view() is false\n",
         this->id(), primary(), ms, m->id(), m->view());
         */
      }
    }
    else
    {
      /*
       printf(
       "\tReplica %i (primary %i) handles a PrePrepare with seqno= %qd from A_replica %i in view %qd, %qd <= %qd\n",
       this->id(), primary(), ms, m->id(), m->view(), ms, low_bound);
       */
    }
  }
  else
  {
    /*
     printf(
     "\tReplica %i (primary %i) handles a PrePrepare with seqno= %qd from A_replica %i in view %qd while view=%qd and last_stable= %qd\n",
     this->id(), primary(), ms, m->id(), m->view(), view(), last_stable);
     */
  }

  if (!has_new_view())
  {
    // This may be an old pre-prepare that A_replica needs to complete
    // a view-change.
    vi.add_missing(m);
    return;
  }

  delete m;
}

void A_Replica::send_prepare(A_Prepared_cert& pc)
{
  if (pc.my_prepare() == 0 && pc.is_pp_complete())
  {
    // Send prepare to all replicas and log it.
    A_Pre_prepare* pp = pc.pre_prepare();
    A_Prepare *p = new A_Prepare(v, pp->seqno(), pp->digest());

    //if (print_stuff)
    //  fprintf(stderr, "\tSND prepare (%qd, %qd)\n", p->seqno(), p->view());

#ifdef MSG_DEBUG
    fprintf(stderr, "Replica %i (primary=%i): Sending a prepare to all replicas\n", id(), primary());
#endif

    send(p, All_replicas);
    pc.add_mine(p);
  }
}

void A_Replica::send_commit(Seqno s)
{

  // Executing request before sending commit improves performance
  // for null requests. May not be true in general.
  if (s == last_executed + 1)
    execute_prepared(false);

  A_Commit* c = new A_Commit(view(), s);
  //if (print_stuff)
  //  fprintf(stderr, "\t\tSND commit (%qd, %qd)\n", s, c->view());
#ifdef MSG_DEBUG
  fprintf(stderr, "Replica %i (primary=%i): Sending a commit %qd to all replicas\n", id(), primary(), c->seqno());
#endif
  send(c, All_replicas);

  if (s > last_prepared)
    last_prepared = s;

  A_Certificate<A_Commit>& cs = clog.fetch(s);
  if (cs.add_mine(c) && cs.is_complete())
  {
    execute_committed();
  }
}

void A_Replica::handle(A_Prepare *m)
{
  const Seqno ms = m->seqno();

#ifdef MSG_DEBUG
  fprintf(stderr, "Replica %i (primary=%i): handles a Prepare with seqno= %qd from %i in view %qd\n", this->id() , primary(), ms, m->id(), m->view());
#endif

  //if (print_stuff)
  //  fprintf(stderr, "\t\tRCV p (%qd, %qd) from %d\n", ms, m->view(), m->id());

  // Only accept prepare messages that are not sent by the primary for
  // current view.
  if (in_wv(m))
  {
    if (ms > low_bound)
    {
      if (primary() != m->id())
      {
        if (has_new_view())
        {
          A_Prepared_cert& ps = plog.fetch(ms);

          /*
           printf(
           "\tReplica %i (primary=%i): handles a A_Prepare with seqno= %qd and quorum=%i/%i\n",
           this->id(), primary(), ms, ps.pc_num_correct(),
           ps.pc_num_complete());
           */

          if (ps.add(m))
          {
            if (ps.is_complete())
            {
              send_commit(ms);
            }
            else
            {
              // certificate not complete
              /*
               printf(
               "\tReplica %i (primary=%i): handles a A_Prepare with seqno= %qd, certificate not complete\n",
               this->id(), primary(), ms);
               */
            }
          }
          else
          {
            // failed to add
            /*
             printf(
             "\tReplica %i (primary=%i): handles a A_Prepare with seqno= %qd, failed to add the prepare\n",
             this->id(), primary(), ms);
             */
          }
          return;
        }
        else
        {
          /*
           printf(
           "\tReplica %i (primary=%i): handles a A_Prepare with seqno= %qd, has_new_view() is false\n",
           this->id(), primary(), ms);
           */
        }
      }
      else
      {
        /*
         printf(
         "\tReplica %i (primary=%i): handles a A_Prepare with seqno= %qd, %i != %i\n",
         this->id(), primary(), ms, primary(), m->id());
         */
      }
    }
    else
    {
      /*
       printf(
       "\tReplica %i (primary=%i): handles a A_Prepare with seqno= %qd, %qd <= %qd\n",
       this->id(), primary(), ms, ms, low_bound);
       */
    }
  }
  else
  {
    /*
     printf(
     "\tReplica %i (primary=%i): handles a A_Prepare with seqno= %qd while view=%qd and last_stable= %qd\n",
     this->id(), primary(), ms, view(), last_stable);
     */
  }

  if (m->is_proof() && !has_new_view())
  {
    // This may be an prepare sent to prove the authenticity of a
    // request to complete a view-change.
    vi.add_missing(m);
    return;
  }

  delete m;
  return;
}

void A_Replica::handle(A_Commit *m)
{
  const Seqno ms = m->seqno();

#ifdef MSG_DEBUG
  fprintf(stderr, "Replica %i (primary=%i): handles a Commit with seqno= %qd from %i in view %qd\n", this->id() , primary(), ms, m->id(), m->view());
#endif

  //if(print_stuff)
  //  fprintf(stderr, "\t\t\tRCV c (%qd, %qd) from %d\n", ms, m->view(), m->id());

  // Only accept messages with the current view.  TODO: change to
  // accept commits from older views as in proof.
  //Seqno commit_seqno = m->seqno();
  if (in_wv(m))
  {
    if (ms > low_bound)
    {
      A_Certificate<A_Commit>& cs = clog.fetch(m->seqno());
      /*
       printf(
       "\tReplica %i (primary=%i): handles a A_Commit with seqno= %qd, quorum size = %i/%i\n",
       this->id(), primary(), ms, cs.num_correct(), cs.num_complete());
       */

      int replica_sender_id = m->id();
      View replica_sender_view = m->view();
      if (cs.add(m))
      {
        if (cs.is_complete())
        {
          /*
           printf(
           "\t\tReplica %i (primary=%i): handles a A_Commit with seqno= %qd from %i in view %qd, executing the command\n",
           this->id(), primary(), ms, replica_sender_id, replica_sender_view);
           */
          execute_committed();
        }
        else
        {
          /*
           printf(
           "\t\tReplica %i (primary=%i): handles a A_Commit with seqno= %qd from %i in view %qd, quroum not complete\n",
           this->id(), primary(), ms, replica_sender_id, replica_sender_view);
           */
        }
      }
      else
      {
        /*
         printf(
         "\tReplica %i (primary=%i): handles a A_Commit with seqno= %qd from %i in view %qd, failed to add commit\n",
         this->id(), primary(), ms, replica_sender_id, replica_sender_view);
         */
      }
      return;
    }
    else
    {
      /*
       printf(
       "\tReplica %i (primary=%i): handles a A_Commit with seqno= %qd from %i in view %qd, %qd <= %qd\n",
       this->id(), primary(), ms, m->id(), m->view(), ms, low_bound);
       */
    }
  }
  else
  {
    /*
     printf(
     "\tReplica %i (primary=%i): handles a A_Commit with seqno= %qd from %i in view %qd while view=%qd and last_stable= %qd\n",
     this->id(), primary(), ms, m->id(), m->view(), view(), last_stable);
     */
  }

  delete m;
  return;
}

void A_Replica::handle(A_Checkpoint *m)
{
  const Seqno ms = m->seqno();
  //if (print_stuff)
  //  fprintf(stderr, "\t\t\t\tRCV CP %qd from %d.  last_stable: %qd, last_exec: %qd\n",ms, m->id(), last_stable, last_executed);
  if (ms > last_stable)
  {
    //fprintf(stderr, "CHECKPOINT 1\n");
    if (ms <= last_stable + max_out)
    {
      //fprintf(stderr, "CHECKPOINT 2\n");
      // A_Checkpoint is within my window.  Check if checkpoint is
      // stable and it is above my last_executed.  This may signal
      // that messages I missed were garbage collected and I should
      // fetch the state
      bool late = m->stable() && last_executed < ms;

      //fprintf(stderr, "CHECKPOINT 3a\n");
      if (!late)
      {
        //fprintf(stderr, "CHEcKPOINT 4\n");
        A_Certificate<A_Checkpoint> &cs = elog.fetch(ms);
        if (cs.add(m) && cs.mine() && cs.is_complete())
        {
          //fprintf(stderr, "CHECKPOINT 5\n");
          // I have enough A_Checkpoint messages for m->seqno() to make it stable.
          // Truncate logs, discard older stable state versions.
          //    fprintf(stderr, "CP MSG call MS %qd!!!\n", last_executed);
          mark_stable(ms, true);
        }
        return;
      }
    }

    if (m->verify())
    {
      //fprintf(stderr, "CHECKPOINT 6\n");
      // A_Checkpoint message above my window.

      if (!m->stable())
      {
        //fprintf(stderr, "CHECKPOINT 7\n");
        // Send status message to obtain missing messages. This works as a
        // negative ack.
        send_status();

        delete m;
        return;
      }

      // Stable checkpoint message above my last_executed.
      A_Checkpoint *c = sset.fetch(m->id());
      if (c == 0 || c->seqno() < ms)
      {
        //fprintf(stderr, "CHECKPOINT 8\n");
        delete sset.remove(m->id());
        sset.store(m);
        if (sset.size() > f())
        {
          //fprintf(stderr, "CHECKPOINT 9\n");
          if (last_tentative_execute > last_executed)
          {
            // Rollback to last checkpoint
            //fprintf(stderr, "CHECKPOINT 10\n");
            th_assert(!state.in_fetch_state(), "Invalid state");
            Seqno rc = state.rollback();
            last_tentative_execute = last_executed = rc;
            //      fprintf(stderr, ":):):):):):):):) Set le = %d\n", last_executed);
          }
          //fprintf(stderr, "CHECKPOINT 10a\n");
          // Stop view change timer while fetching state. It is restarted
          // in new state when the fetch ends.
          vtimer->stop();
          state.start_fetch(last_executed);
        }
        //fprintf(stderr, "checkpoint 11\n");
        return;
      }
    }
  }
  delete m;
  return;
}

void A_Replica::handle(A_New_key *m)
{
  //fprintf(stderr, "\t\t\t\t\t\tnew key\n");
  if (!m->verify())
  {
    //fprintf(stderr, "BAD NKEY from %d\n", m->id());
  }
  delete m;
}

void A_Replica::handle(A_Status* m)
{
#ifdef MSG_DEBUG
  // there are too many status messages
  // A_Status messages from this A_replica to this A_replica are discarded, thus we do not display them
  //if (id() != m->id())
  //fprintf(stderr,
  //    "A_Replica %i handles a A_Status from %i with status_badly_needed=%s\n",
  //    this->id(), m->id(), (status_messages_badly_needed ? "true" : "false"));
#endif

  //fprintf(stderr, "\t\t\t\t\t\tstatus from %d\n", m->id());
  static const int max_ret_bytes = 65536;

#ifdef NEW_BIG_MAC_ATTACK
  // if I am A_replica 0, the primary, and the message comes from A_replica >2*f,
  // then retransmit only New view messages.
  if (id() == 0 && id() == primary() && m->id() > 2 * f())
  {

    if (m->verify())
    {
      A_Time current;
      A_Time *t;
      current = A_currentTime();
      A_Principal *p = A_node->i_to_p(m->id());

      //TODO


      // TODO
      //XXX
      //FIXME


    }
  }

  delete m;
  return;
#endif

  if (m->verify())
  {
    A_Time current;
    A_Time *t;
    current = A_currentTime();
    A_Principal *p = A_node->i_to_p(m->id());

    // Retransmit messages that the sender is missing.
    if (last_stable > m->last_stable() + max_out)
    {
#ifdef NEW_BIG_MAC_ATTACK
      // do not transmit checkpoints to these nodes!
      if (id() == 0 && id() == primary() && m->id() > 2 * f())
      {
        delete m;
        return;
      }
#endif

      // A_Node is so out-of-date that it will not accept any
      // pre-prepare/prepare/commmit messages in my log.
      // Send a stable checkpoint message for my stable checkpoint.
      A_Checkpoint *c = elog.fetch(last_stable).mine(&t);
      //th_assert(c != 0 && c->stable(), "Invalid state");
      if (c != 0 && c->stable()) // randomly triggered assertion replaced with this line...
        retransmit(c, current, t, p);
      delete m;
      return;
    }

    // Retransmit any checkpoints that the sender may be missing.
    int max = MIN(last_stable, m->last_stable()) + max_out;
    int min = MAX(last_stable, m->last_stable()+1);
    for (Seqno n = min; n <= max; n++)
    {
#ifdef NEW_BIG_MAC_ATTACK
      // do not transmit checkpoints to these nodes!
      if (id() == 0 && id() == primary() && m->id() > 2 * f())
      {
        continue;
      }
#endif

      if (n % checkpoint_interval == 0)
      {
        A_Checkpoint *c = elog.fetch(n).mine(&t);
        if (c != 0)
        {
          retransmit(c, current, t, p);
          th_assert(n == last_stable || !c->stable(), "Invalid state");
        }
      }
    }

    if (m->view() < v)
    {
      // Retransmit my latest view-change message
      A_View_change* vc = vi.my_view_change(&t);
      if (vc != 0)
      {
        /*
         printf(
         "A_Replica %i, primary %i, view %qd: retransmitting View change for view %qd (1)\n",
         id(), primary(), view(), vc->view());
         */

        retransmit(vc, current, t, p);
      }
      delete m;
      return;
    }

    if (m->view() == v)
    {
      if (m->has_nv_info())
      {
        min = MAX(last_stable+1, m->last_executed()+1);
        for (Seqno n = min; n <= max; n++)
        {
          if (m->is_committed(n))
          {
            // No need for retransmission of commit or pre-prepare/prepare
            // message.
            continue;
          }

          A_Commit *c = clog.fetch(n).mine(&t);
          if (c != 0)
          {
            retransmit(c, current, t, p);
          }

          if (m->is_prepared(n))
          {
            // No need for retransmission of pre-prepare/prepare message.
            continue;
          }

          // If I have a pre-prepare/prepare send it, provided I have sent
          // a pre-prepare/prepare for view v.
          if (primary() == node_id)
          {
            A_Pre_prepare *pp = plog.fetch(n).my_pre_prepare(&t);
            if (pp != 0)
            {
#ifdef NEW_BIG_MAC_ATTACK
              // do not transmit valid PP to these nodes!
              if (id() == 0 && id() == primary() && m->id() > 2 * f())
              {
                A_Message::set_mac_unvalid((A_Message_rep*) (pp->contents()));
              }
#endif

              retransmit(pp, current, t, p);

#ifdef NEW_BIG_MAC_ATTACK
              // do not transmit valid PP to these nodes!
              if (id() == 0 && id() == primary() && m->id() > 2 * f())
              {
                A_Message::set_mac_valid((A_Message_rep*) (pp->contents()));
              }
#endif
            }
          }
          else
          {
            A_Prepare *pr = plog.fetch(n).my_prepare(&t);
            if (pr != 0)
            {
              retransmit(pr, current, t, p);
            }
          }
        }

        if (id() == primary())
        {
          // For now only primary retransmits big requests.
          A_Status::BRS_iter gen(m);

          int count = 0;
          Seqno ppn;
          BR_map mrmap;
          while (gen.get(ppn, mrmap) && count <= max_ret_bytes)
          {
            if (plog.within_range(ppn))
            {
              A_Pre_prepare_info::BRS_iter
                  gen(plog.fetch(ppn).prep_info(), mrmap);
              A_Request* r;
              while (gen.get(r))
              {
#ifdef MSG_DEBUG
                fprintf(stderr, "Replica %i (primary=%i): Sending a request to %i\n", id(), primary(), m->id());
#endif
                send(r, m->id());
                count += r->size();
              }
            }
          }
        }
      }
      else
      {
        // m->has_nv_info() == false
        if (!m->has_vc(node_id))
        {
          // p does not have my view-change: send it.
          A_View_change* vc = vi.my_view_change(&t);
          //A_View_change* vc = vi.my_view_change();
          th_assert(vc != 0, "Invalid state");

          /*
           printf(
           "A_Replica %i, primary %i, view %qd: retransmitting View change for view %qd (2)\n",
           id(), primary(), view(), vc->view());
           */

          retransmit(vc, current, t, p);
        }

        if (!m->has_nv_m())
        {
          if (primary(v) == node_id && vi.has_new_view(v))
          {
            // p does not have new-view message and I am primary: send it
            A_New_view* nv = vi.my_new_view(&t);
            //A_New_view* nv = vi.my_new_view();
            if (nv != 0)
            {
              /*
               printf(
               "A_Replica %i, primary %i, view %qd: retransmitting new view for view %qd\n",
               id(), primary(), view(), nv->view());
               */
              retransmit(nv, current, t, p);
            }
          }
        }
        else
        {
          if (primary(v) == node_id && vi.has_new_view(v))
          {
            // Send any view-change messages that p may be missing
            // that are referred to by the new-view message.  This may
            // be important if the sender of the original message is
            // faulty.


          }
          else
          {
            // Send any view-change acks p may be missing.
            for (int i = 0; i < num_replicas; i++)
            {
              if (m->id() == i)
                continue;
              A_View_change_ack* vca = vi.my_vc_ack(i);
              if (vca && !m->has_vc(i))
              {
                //printf(
                //    "A_Replica %i, primary %i, view %qd: retransmitting view change ack for view %qd, from %i for ack of %i\n",
                //   id(), primary(), view(), vca->view(), vca->id(),
                //   vca->vc_id());

                // View-change acks are not being authenticated
                retransmit(vca, current, &current, p);
              }
            }
          }

          // Send any pre-prepares that p may be missing and any proofs
          // of authenticity for associated requests.
          A_Status::PPS_iter gen(m);

          int count = 0;
          Seqno ppn;
          View ppv;
          bool ppp;
          BR_map mrmap;
          while (gen.get(ppv, ppn, mrmap, ppp))
          {
            A_Pre_prepare* pp = 0;
            if (m->id() == primary(v))
              pp = vi.pre_prepare(ppn, ppv);
            else
            {
              if (primary(v) == id() && plog.within_range(ppn))
                pp = plog.fetch(ppn).pre_prepare();
            }

            if (pp)
            {
#ifdef NEW_BIG_MAC_ATTACK
              // do not transmit valid PP to these nodes!
              if (id() == 0 && id() == primary() && m->id() > 2 * f())
              {
                A_Message::set_mac_unvalid((A_Message_rep*) (pp->contents()));
              }
#endif

              retransmit(pp, current, &current, p);

#ifdef NEW_BIG_MAC_ATTACK
              // do not transmit valid PP to these nodes!
              if (id() == 0 && id() == primary() && m->id() > 2 * f())
              {
                A_Message::set_mac_valid((A_Message_rep*) (pp->contents()));
              }
#endif

              if (count < max_ret_bytes && mrmap != ~0)
              {
                A_Pre_prepare_info pi;
                pi.add_complete(pp);

                A_Pre_prepare_info::BRS_iter gen(&pi, mrmap);
                A_Request* r;
                while (gen.get(r))
                {
#ifdef MSG_DEBUG
                  fprintf(stderr, "Replica %i (primary=%i): Sending a request to %i\n", id(), primary(), m->id());
#endif
                  send(r, m->id());
                  count += r->size();
                }
                pi.zero(); // Make sure pp does not get deallocated
              }
            }

            if (ppp)
              vi.send_proofs(ppn, ppv, m->id());
          }
        }
      }
    }
  }
  else
  {
    /*
     // A_Status messages from this A_replica to this A_replica are discarded, thus we do not display them
     if (id() != m->id())
     fprintf(stderr, "A_Replica %i: The A_Status message from %i is not valid\n",
     id(), m->id());
     */

    // It is possible that we could not verify message because the
    // sender did not receive my last new_key message. It is also
    // possible message is bogus. We choose to retransmit last new_key
    // message.  TODO: should impose a limit on the frequency at which
    // we are willing to do it to prevent a denial of service attack.
    // This is not being done right now.
    if (last_new_key != 0 && !m->verify())
    {
      //fprintf(stderr, "sending new key to %d\n", m->id());
#ifdef MSG_DEBUG
      fprintf(stderr, "Sending a last new key to %i\n", m->id());
#endif
      send(last_new_key, m->id());
    }
  }

  delete m;
}

A_Pre_prepare* A_Replica::prepared(Seqno n)
{
  A_Prepared_cert& pc = plog.fetch(n);
  if (pc.is_complete())
  {
    return pc.pre_prepare();
  }
  return 0;
}

A_Pre_prepare *A_Replica::committed(Seqno s)
{
  // TODO: This is correct but too conservative: fix to handle case
  // where commit and prepare are not in same view; and to allow
  // commits without prepared requests, i.e., only with the
  // pre-prepare.
  A_Pre_prepare *pp = prepared(s);
  if (clog.fetch(s).is_complete())
    return pp;
  return 0;
}

bool A_Replica::execute_read_only(A_Request *req)
{
  // JC: won't execute read-only if there's a current tentative execution
  // this probably isn't necessary if clients wait for 2f+1 RO responses
  if (last_tentative_execute == last_executed && !state.in_fetch_state())
  {
    // Create a new A_Reply message.
    A_Reply *rep = new A_Reply(view(), req->request_id(), node_id);

    // Obtain "in" and "out" buffers to call exec_command
    Byz_req inb;
    Byz_rep outb;

    inb.contents = req->command(inb.size);
    outb.contents = rep->store_reply(outb.size);

    // Execute command.
    int cid = req->client_id();
    A_Principal *cp = i_to_p(cid);
    int error = exec_command(&inb, &outb, 0, cid, true, (long int) 0);

    if (outb.size % ALIGNMENT_BYTES)
      for (int i = 0; i < ALIGNMENT_BYTES - (outb.size % ALIGNMENT_BYTES); i++)
        outb.contents[outb.size + i] = 0;

    if (!error)
    {
      // Finish constructing the reply and send it.
      rep->authenticate(cp, outb.size, true);
      if (outb.size < 50 || req->replier() == node_id || req->replier() < 0)
      {
        // Send full reply.
#ifdef MSG_DEBUG
        fprintf(stderr, "Replica %i (primary=%i): Sending a reply to client %i\n", id(), primary(), cid);
#endif
        send(rep, cid);
      }
      else
      {
        // Send empty reply.
        A_Reply
            empty(view(), req->request_id(), node_id, rep->digest(), cp, true);
#ifdef MSG_DEBUG
        fprintf(stderr, "Replica %i (primary=%i): Sending an empty reply to client %i\n", id(), primary(), cid);
#endif
        send(&empty, cid);
      }
    }

    delete rep;
    return true;
  }
  else
  {
    return false;
  }
}

#ifdef THROUGHPUT_ADAPTIVE

// display the expectations
void A_Replica::display_expectations(A_Time current)
{
#ifdef ORIGINAL_AARDVARK_THROUGHPUT_ADAPTIVE

  fprintf(
      stderr,
      "ADAPTIVE:  time: %qd\tview: %qd\tnb_checkpoints: %qd\tobs_thr_in_cpt: %f\treq_thr: %f\n",
      A_diffTime(current, 0), view(), total_nb_checkpoints,
      obs_throughput_in_checkpoint*1e6, req_throughput*1e6);

#else

  fprintf(
      stderr,
      "ADAPTIVE:  time: %qd\tview: %qd\tnb_checkpoints: %qd\tobs_thr_in_sliding_window: %f\tobs_thr_in_cpt: %f\treq_thr: %f\n",
      A_diffTime(current, 0), view(), total_nb_checkpoints, obs_throughput*1e6,
      obs_throughput_in_checkpoint*1e6, req_throughput*1e6);

#endif
}

// Adapt Aardvark throughput
void A_Replica::adapt_throughput(A_Time current)
{
  // display periodically the expectations
  if (total_nb_checkpoints % PERIODIC_EXPECTATIONS_DISPLAY == 0)
  {
    display_expectations(current);
  }

  if (obs_throughput < req_throughput && !vc_already_triggered)
  {
    if (id() != primary())
    {
      //fprintf(stderr, "ADAPTIVE view change! --- obs_throughput: %f, req_throughput: %f, increment: %f\n", obs_throughput, req_throughput, req_throughput_increment);
      vc_already_triggered = true;
    }

    // there is a view change, thus display the expectations
    display_expectations(current);

    last_throughput_of_replica[primary()] = req_throughput
        - req_throughput_increment;

    float max_throughput = last_throughput_of_replica[0];

    for (int j = 1; j < num_replicas; j++)
      max_throughput = MAX(max_throughput, last_throughput_of_replica[j]);
    req_throughput = max_throughput * new_req_throughput_scaling_factor;
    req_throughput_increment
        = MAX(req_throughput * req_throughput_increment_scaling_factor, 0.000001);

    if (id() != primary())
    {
      throughput_timer->restart();
    }
  }

  if (obs_throughput > req_throughput && time_to_increment)
  {
#ifdef ORIGINAL_AARDVARK_THROUGHPUT_ADAPTIVE
    // increment the required throughput
    if ((obs_throughput * new_req_throughput_scaling_factor) > (req_throughput
            + req_throughput_increment))
    {
      req_throughput = obs_throughput * new_req_throughput_scaling_factor;
      req_throughput_increment
      = MAX(req_throughput * req_throughput_increment_scaling_factor, 0.000001);
    }
    else
    {
#endif
    req_throughput += req_throughput_increment;
#ifdef ORIGINAL_AARDVARK_THROUGHPUT_ADAPTIVE
  }
#endif
  }
}
#endif

void A_Replica::execute_prepared(bool committed)
{
  A_Time start1,start2,start3,end1,end2,end3, start4, end4;
  long long outbSize =0;
  //  if(!committed) return;
  if (last_tentative_execute < last_executed + 1 && last_executed < last_stable
      + max_out && !state.in_fetch_state() && has_new_view())
  {
    A_Pre_prepare *pp = prepared(last_executed + 1);

    if (pp && pp->view() == view())
    {
      // Can execute the requests in the message with sequence number
      // last_executed+1.
      last_tentative_execute = last_executed + 1;
      th_assert(pp->seqno() == last_tentative_execute, "Invalid execution");

      // Iterate over the requests in the message, calling execute for
      // each of them.
      A_Pre_prepare::Requests_iter iter(pp);
      A_Request req;

      while (iter.get(req))
      {
        int cid = req.client_id();
        if (replies.req_id(cid) >= req.request_id())
        {
          // A_Request has already been executed and we have the reply to
          // the request. Resend reply and don't execute request
          // to ensure idempotence.
          replies.send_reply(cid, view(), id());
          continue;
        }
#ifdef THROUGHPUT_ADAPTIVE
        // increment request count only for "new" requests
        //fprintf(stderr, "ADAPTIVE incrementing req_count\n");

        req_count_vc++;
        req_count++;
#endif

#ifdef PERIODICALLY_MEASURE_THROUGHPUT
        if (first_request_time == 0) {
          first_request_time = A_diffTime(currentTime(), 0);
        }
        nb_requests_4_periodic_thr_measure++;
#endif

        // Obtain "in" and "out" buffers to call exec_command
        Byz_req inb;
        Byz_rep outb;
        Byz_buffer non_det;
        inb.contents = req.command(inb.size);
        outb.contents = replies.new_reply(cid, outb.size);
        non_det.contents = pp->choices(non_det.size);

        if (is_replica(cid))
        {
          // Handle recovery requests, i.e., requests from replicas,
          // differently.  TODO: make more general to allow other types
          // of requests from replicas.
          //    fprintf(stderr, "\n\n\nExecuting recovery request seqno= %qd rep id=%d\n", last_tentative_execute, cid);

          if (inb.size != sizeof(Seqno))
          {
            // Invalid recovery request.
            continue;
          }

          // Change keys. TODO: could change key only for recovering A_replica.
          if (cid != node_id)
            send_new_key();

          // Store seqno of execution.
          max_rec_n = last_tentative_execute;

          // A_Reply includes sequence number where request was executed.
          outb.size = sizeof(last_tentative_execute);
          memcpy(outb.contents, &last_tentative_execute, outb.size);
        }
        else
        {
          // Execute command in a regular request.
          //exec_command(&inb, &outb, &non_det, cid, false, exec_command_delay);
          nb_executed++;
          //start1 = A_currentTime();
          exec_command(&inb, &outb, &non_det, cid, false, exec_command_delay);
          //end1 = A_currentTime();
          //elapsed_sum1 += A_diffTime(end1, start1);

          //start2 = A_currentTime();
          if (outb.size % ALIGNMENT_BYTES)
            for (int i = 0; i < ALIGNMENT_BYTES - (outb.size % ALIGNMENT_BYTES); i++)
              outb.contents[outb.size + i] = 0;
          //end2 = A_currentTime();
          //if (last_tentative_execute%100 == 0)
          //  fprintf(stderr, "%s - %qd\n",((node_id == primary()) ? "P" : "B"), last_tentative_execute);
          //elapsed_sum2 += A_diffTime(end2, start2);
        }

        // Finish constructing the reply.
        outbSize+=outb.size;
        //start3 = A_currentTime();
        replies.end_reply(cid, req.request_id(), outb.size);
        //end3 = A_currentTime();
        //elapsed_sum3 += A_diffTime(end3, start3);
        // Send the reply. Replies to recovery requests are only sent
        // when the request is committed.
        //start4 = A_currentTime();
        if (outb.size != 0 && !is_replica(cid))
        {
          if (outb.size < 50 || req.replier() == node_id || req.replier() < 0)
          {
            // Send full reply.
            replies.send_reply(cid, view(), id(), !committed);
          }
          else
          {
            // Send empty reply.
            A_Reply empty(view(), req.request_id(), node_id, replies.digest(cid),
                i_to_p(cid), !committed);
#ifdef MSG_DEBUG
            fprintf(stderr, "Replica %i (primary=%i): Sending an empty reply to client %i\n", id(), primary(), cid);
#endif
            send(&empty, cid);
          }
        }
        //end4 = A_currentTime();
        //elapsed_sum4 += A_diffTime(end4, start4);
        /*
        if(nb_executed==100000){
          fprintf(stderr,"Number of executed requests = %d , Number of retransmitted requests = %d \n",nb_executed, nb_retransmissions);
          fprintf(stderr,"Average time elapsed in various parts of the executor thread are %qd, %qd, %qd, %qd \n", elapsed_sum1,elapsed_sum2, elapsed_sum3, elapsed_sum4);
          fprintf(stderr,"Outb size = %qd\n",outbSize);
        }
        */
      }

      if ((last_executed + 1) % checkpoint_interval == 0)
      {
        state.checkpoint(last_executed + 1);
        if (status_messages_badly_needed && (last_executed
            > not_deprioritize_status_before_this + max_out))
        {
          //fprintf(stderr, "****** Deprioritizing status messages\n");
          status_messages_badly_needed = false;
        }

        // moved at the exterior of the macro because is used to count the number of checkpoints in the view
        // and is displayed when changing view
        checkpoints_in_new_view++;

#if defined(THROUGHPUT_ADAPTIVE) && !defined(THROUGHPUT_ADAPTIVE_PERIODIC_COMPUTATION)
        A_Time current = A_rdtsc();
        total_nb_checkpoints++;

        if ((checkpoints_in_new_view > number_of_checkpoints_before_adpating)
            && has_new_view())
        {
          long long cp_duration = A_diffTime(current, last_cp_time);
          obs_throughput_in_checkpoint = ((float) req_count)
              / ((float) cp_duration);

#ifdef ORIGINAL_AARDVARK_THROUGHPUT_ADAPTIVE
          obs_throughput = obs_throughput_in_checkpoint;
#else
          if (sliding_window_size == 0)
          {
            long long view_duration = A_diffTime(current, last_view_time);
            float obs_throughput_in_view = ((float) req_count_vc)
                / ((float) view_duration);
            obs_throughput = obs_throughput_in_view;
          }
          else
          {
            // update the sliding window
            sliding_window[sliding_window_cur_idx]
                = obs_throughput_in_checkpoint;
            sliding_window_cur_idx = (sliding_window_cur_idx + 1)
                % sliding_window_size;
            sliding_window_nb_got
                = MIN(sliding_window_nb_got+1, sliding_window_size);

            // compute the mean
            float obs_throughput_in_sliding_window = 0;
            for (int i = 0; i < sliding_window_nb_got; i++)
            {
              obs_throughput_in_sliding_window += sliding_window[i];
            }
            obs_throughput_in_sliding_window /= sliding_window_nb_got;
            obs_throughput = obs_throughput_in_sliding_window;
          }
#endif

          adapt_throughput(current);

#ifdef RRBFT_ATTACK
          // compute the next expected throughput (if the malicious primary remains undetected)
          if (id() == MALICIOUS_PRIMARY_ID and id() == primary())
          {
#ifdef ORIGINAL_AARDVARK_THROUGHPUT_ADAPTIVE
            // increment the required throughput
            if ((obs_throughput * new_req_throughput_scaling_factor) > (req_throughput
                    + req_throughput_increment))
            {
              next_expected_throughput = obs_throughput * new_req_throughput_scaling_factor;
            }
            else
            {
#endif
              next_expected_throughput = req_throughput + req_throughput_increment;
#ifdef ORIGINAL_AARDVARK_THROUGHPUT_ADAPTIVE
            }
#endif
          }
#endif
        }

        last_throughput_of_replica[primary()] = obs_throughput;

        if (first_checkpoint_after_view_change && has_new_view())
        {
          first_checkpoint_after_view_change = false;
          vc_already_triggered = false;
        }
        req_count = 0;
        last_cp_time = current;

#ifdef RRBFT_ATTACK
          nb_pp_since_checkpoint_start = 0;
          nb_req_in_pp_since_checkpoint_start = 0;
#endif
#endif
      }
    }
  }
}

void A_Replica::execute_committed()
{
  if (!state.in_fetch_state() && has_new_view())
  {
    while (1)
    {
      if (last_executed >= last_stable + max_out || last_executed < last_stable)
        return;

      A_Pre_prepare *pp = committed(last_executed + 1);
      //      if (print_stuff)
      //                         fprintf(stderr, "\t\t\tlast executed: %qd\n", last_executed);
      //fprintf(stderr, "\t\t\tlast stable: %qd\n", last_stable);
      //if (pp) fprintf(stderr, "\t\t\tpp->seqno: %qd\n", pp->seqno());
      print_stuff = false;

      if (pp && pp->view() == view())
      {
        // Tentatively execute last_executed + 1 if needed.
        execute_prepared(true);

        // Can execute the requests in the message with sequence number
        // last_executed+1.
        last_executed = last_executed + 1;
        //fprintf(stderr, "\t\t\tlast executed: %qd\n", last_executed);
        //fprintf(stderr, "\t\t\tlast stable: %qd\n", last_stable);
        //fprintf(stderr, "\t\t\tpp->seqno: %qd\n", pp->seqno());
        //  fprintf(stderr, ":):):):):):):):) Set le = %d\n", last_executed);
        th_assert(pp->seqno() == last_executed, "Invalid execution");

        // Execute any buffered read-only requests
        for (A_Request *m = ro_rqueue.remove(); m != 0; m = ro_rqueue.remove())
        {
          execute_read_only(m);
          delete m;
        }
        // Iterate over the requests in the message, marking the saved replies
        // as committed (i.e., non-tentative for each of them).
        A_Pre_prepare::Requests_iter iter(pp);
        A_Request req;
        while (iter.get(req))
        {
          int cid = req.client_id();
          replies.commit_reply(cid);
          if (is_replica(cid))
          {
            // Send committed reply to recovery request.
            if (cid != node_id)
              replies.send_reply(cid, view(), id(), false);
            else
              handle(replies.reply(cid)->copy(cid), true);
          }
#ifdef FAIRNESS_ADAPTIVE
          if ((executed_snapshot[cid] != -1) && (executed_snapshot[cid]
              + fairness_bound < last_executed) && has_new_view())
          {
            fprintf(stderr, "ADAPTIVE: ****** Fairness violation!****** \n");
            if (!vc_already_triggered)
            {
              vc_already_triggered = true;
              throughput_timer->restart(); //starting throughput timer should work...
            }
          }

          executed_snapshot[cid] = -1;
#endif
          // Remove the request from rqueue if present.
          if (rqueue.remove(cid, req.request_id()))
            vtimer->stop();
        }

        // Send and log A_Checkpoint message for the new state if needed.
        if (last_executed % checkpoint_interval == 0)
        {
          Digest d_state;
          state.digest(last_executed, d_state);
          A_Checkpoint *e = new A_Checkpoint(last_executed, d_state);
          A_Certificate<A_Checkpoint> &cc = elog.fetch(last_executed);
          cc.add_mine(e);
          if (cc.is_complete())
          {
            mark_stable(last_executed, true);
            //      fprintf(stderr, "EXEC call MS %qd!!!\n", last_executed);
          }
          //    else
          //      fprintf(stderr, "CP exec %qd not yet. ", last_executed);

          //e = cc.mine(); //not in PBFT
          //th_assert(e, "I just added my checkpoint"); //not in PBFT
#ifdef MSG_DEBUG
          fprintf(stderr, "Sending a checkpoint to all replicas\n");
#endif
          send(e, All_replicas);
          //    fprintf(stderr, ">>>>Checkpointing "); d_state.print(); fprintf(stderr, " <<<<\n"); fflush(stdout);
        }
      }
      else
      {
        // No more requests to execute at this point.
        break;
      }
    }

    if (rqueue.size() > 0)
    {
      if (primary() == node_id)
      {
        // Send a pre-prepare with any buffered requests
        send_pre_prepare();
      }
      else
      {
        // If I am not the primary and have pending requests restart the
        // timer.
        //vtimer->start(); //necessary? useful?
      }
    }
  }
}

void A_Replica::update_max_rec()
{
  // Update max_rec_n to reflect new state.
  bool change_keys = false;
  for (int i = 0; i < num_replicas; i++)
  {
    if (replies.reply(i))
    {
      int len;
      char *buf = replies.reply(i)->reply(len);
      if (len == sizeof(Seqno))
      {
        Seqno nr;
        memcpy(&nr, buf, sizeof(Seqno));

        if (nr > max_rec_n)
        {
          max_rec_n = nr;
          change_keys = true;
        }
      }
    }
  }

  // Change keys if state fetched reflects the execution of a new
  // recovery request.
  if (change_keys)
    send_new_key();
}

void A_Replica::new_state(Seqno c)
{
  //  fprintf(stderr, ":n)e:w):s)t:a)t:e) ");
  if (vi.has_new_view(v) && c >= low_bound)
    has_nv_state = true;

  if (c > last_executed)
  {
    last_executed = last_tentative_execute = c;
    //    fprintf(stderr, ":):):):):):):):) (new_state) Set le = %d\n", last_executed);
    if (replies.new_state(&rqueue))
      vtimer->stop();

    update_max_rec();

    if (c > last_prepared)
      last_prepared = c;

    if (c > last_stable + max_out)
    {
      mark_stable(c - max_out,
          elog.within_range(c - max_out) && elog.fetch(c - max_out).mine());
    }

    // Send checkpoint message for checkpoint "c"
    Digest d;
    state.digest(c, d);
    A_Checkpoint* ck = new A_Checkpoint(c, d);
    elog.fetch(c).add_mine(ck);
    //ck = elog.fetch(c).mine(); //not in PBFT
    //th_assert(ck, " I just added my checkpoint"); //not in PBFT
#ifdef MSG_DEBUG
    fprintf(stderr, "Sending a checkpoint to all replicas\n");
#endif
    send(ck, All_replicas);
  }

  // Check if c is known to be stable.
  int scount = 0;
  for (int i = 0; i < num_replicas; i++)
  {
    A_Checkpoint* ck = sset.fetch(i);
    if (ck != 0 && ck->seqno() >= c)
    {
      th_assert(ck->stable(), "Invalid state");
      scount++;
    }
  }
  if (scount > f())
    mark_stable(c, true);

  if (c > seqno)
  {
    seqno = c;
  }

  // Execute any committed requests
  execute_committed();

  // Execute any buffered read-only requests
  for (A_Request *m = ro_rqueue.remove(); m != 0; m = ro_rqueue.remove())
  {
    execute_read_only(m);
    delete m;
  }

  if (rqueue.size() > 0)
  {
    if (primary() == id())
    {
      // Send pre-prepares for any buffered requests
      send_pre_prepare();
    }
    else
      //fprintf(stderr, "vtimer restart (2)\n");
      vtimer->restart();
  }
}

void A_Replica::mark_stable(Seqno n, bool have_state)
{
  //XXXXXcheck if this should be < or <=

  //  fprintf(stderr, "mark stable n %qd laststable %qd\n", n, last_stable);
  if (n <= last_stable)
    return;

  last_stable = n;
  if (last_stable > low_bound)
  {
    low_bound = last_stable;
  }

  if (have_state && last_stable > last_executed)
  {
    last_executed = last_tentative_execute = last_stable;
    //    fprintf(stderr, ":):):):):):):):) (mark_stable) Set le = %d\n", last_executed);
    replies.new_state(&rqueue);
    update_max_rec();

    if (last_stable > last_prepared)
      last_prepared = last_stable;
  }
  //  else
  //    fprintf(stderr, "OH BASE! OH CLU!\n");

  if (last_stable > seqno)
    seqno = last_stable;

  //  fprintf(stderr, "mark_stable: Truncating plog to %ld have_state=%d\n", last_stable+1, have_state);
  plog.truncate(last_stable + 1);
  clog.truncate(last_stable + 1);
  vi.mark_stable(last_stable);
  elog.truncate(last_stable);
  state.discard_checkpoint(last_stable, last_executed);
  brt.mark_stable(last_stable);

  if (have_state)
  {
    // Re-authenticate my checkpoint message to mark it as stable or
    // if I do not have one put one in and make the corresponding
    // certificate complete.
    A_Checkpoint *c = elog.fetch(last_stable).mine();
    if (c == 0)
    {
      Digest d_state;
      state.digest(last_stable, d_state);
      c = new A_Checkpoint(last_stable, d_state, true);
      elog.fetch(last_stable).add_mine(c);
      elog.fetch(last_stable).make_complete();
    }
    else
    {
      c->re_authenticate(0, true);
    }

  }

  // Go over sset transfering any checkpoints that are now within
  // my window to elog.
  Seqno new_ls = last_stable;
  for (int i = 0; i < num_replicas; i++)
  {
    A_Checkpoint* c = sset.fetch(i);
    if (c != 0)
    {
      Seqno cn = c->seqno();
      if (cn < last_stable)
      {
        c = sset.remove(i);
        delete c;
        continue;
      }

      if (cn <= last_stable + max_out)
      {
        A_Certificate<A_Checkpoint>& cs = elog.fetch(cn);
        cs.add(sset.remove(i));
        if (cs.is_complete() && cn > new_ls)
          new_ls = cn;
      }
    }
  }

  //XXXXXXcheck if this is safe.
  if (new_ls > last_stable)
  {
    //    fprintf(stderr, "@@@@@@@@@@@@@@@               @@@@@@@@@@@@@@@               @@@@@@@@@@@@@@@\n");
    mark_stable(new_ls, elog.within_range(new_ls) && elog.fetch(new_ls).mine());
  }

  // Try to send any Pre_prepares for any buffered requests.
  if (primary() == id())
    send_pre_prepare();
}

void A_Replica::handle(A_Data *m)
{
  //fprintf(stderr, "\t\t\t\t\t\tdata\n");
  //fprintf(stderr, "received data\n");
  state.handle(m);
}

void A_Replica::handle(A_Meta_data *m)
{
  //fprintf(stderr, "\t\t\t\t\t\tmetadata\n");

  state.handle(m);
}

void A_Replica::handle(A_Meta_data_d *m)
{
  //fprintf(stderr, "\t\t\t\t\t\tmetadatad\n");

  state.handle(m);
}

void A_Replica::handle(A_Fetch *m)
{
  //fprintf(stderr, "\t\t\t\t\t\tfetch\n");

  int mid = m->id();
  if (!state.handle(m, last_stable) && last_new_key != 0)
  {
#ifdef MSG_DEBUG
    fprintf(stderr, "Sending a last new key to all replicas\n");
#endif
    send(last_new_key, mid);
  }
}

void A_Replica::send_new_key()
{
  A_Node::send_new_key();
  fprintf(stderr, " call to send new key\n");
  // Cleanup messages in incomplete certificates that are
  // authenticated with the old keys.
  int max = last_stable + max_out;
  int min = last_stable + 1;
  for (Seqno n = min; n <= max; n++)
  {
    if (n % checkpoint_interval == 0)
      elog.fetch(n).mark_stale();
  }

  if (last_executed > last_stable)
    min = last_executed + 1;

  for (Seqno n = min; n <= max; n++)
  {
    plog.fetch(n).mark_stale();
    clog.fetch(n).mark_stale();
  }

  vi.mark_stale();
  state.mark_stale();
}

void A_Replica::send_status(bool force)
{
  // Check how long ago we sent the last status message.
  A_Time cur = A_currentTime();
  if (force || A_diffTime(cur, last_status) > 100000)
  {
    /*
     fprintf(
     stderr,
     "A_Replica %i, view %qd, sending a status message with force=%s and time=%qd\n",
     id(), view(), (force ? "true" : "false"), A_diffTime(cur, last_status));
     */

    //if(1) {
    // Only send new status message if last one was sent more
    // than 100 milliseconds ago.
    // [elwong] ... or if we are forcing it.
    last_status = cur;

    if (rr)
    {
      // Retransmit recovery request if I am waiting for one.
#ifdef MSG_DEBUG
      fprintf(stderr, "Sending a recovery request to all replicas\n");
#endif
      send(rr, All_replicas);
    }

    // If fetching state, resend last fetch message instead of status.
    if (state.retrans_fetch(cur))
    {
      state.send_fetch(true);
      return;
    }

    A_Status s(v, last_stable, last_executed, has_new_view(),
        vi.has_nv_message(v));

    if (has_new_view())
    {
      // Set prepared and committed bitmaps correctly
      int max = last_stable + max_out;
      for (Seqno n = last_executed + 1; n <= max; n++)
      {
        A_Prepared_cert& pc = plog.fetch(n);
        if (pc.is_complete())
        {
          s.mark_prepared(n);
          if (clog.fetch(n).is_complete())
          {
            s.mark_committed(n);
          }
        }
        else
        {
          // Ask for missing big requests
          if (!pc.is_pp_complete() && pc.pre_prepare() && pc.num_correct()
              >= f())
            s.add_breqs(n, pc.missing_reqs());
        }
      }
    }
    else
    {
      vi.set_received_vcs(&s);
      vi.set_missing_pps(&s);
    }

    // Multicast status to all replicas.
    s.authenticate();
#ifdef MSG_DEBUG
    // there are too many status messages
    //fprintf(stderr, "Sending a status to all replicas\n");
#endif
    send(&s, All_replicas);
  }
}

void A_Replica::handle(A_Reply *m, bool mine)
{
  //  th_assert(false,"should not be there...\n");
  int mid = m->id();
  int mv = m->view();

#ifdef MSG_DEBUG
  fprintf(stderr, "Replica %i (primary=%i): handles a Reply of id %i and view %i\n", this->id(), primary(), mid, mv);
#endif

  if (rr && rr->request_id() == m->request_id() && (mine || !m->is_tentative()))
  {
    // Only accept recovery request replies that are not tentative.
    bool added = (mine) ? rr_reps.add_mine(m) : rr_reps.add(m);
    if (added)
    {
      if (rr_views[mid] < mv)
        rr_views[mid] = mv;

      if (rr_reps.is_complete())
      {
        // I have a valid reply to my outstanding recovery request.
        // Update recovery point
        int len;
        const char *rep = rr_reps.cvalue()->reply(len);
        th_assert(len == sizeof(Seqno), "Invalid message");

        Seqno rec_seqno;
        memcpy(&rec_seqno, rep, len);
        Seqno new_rp = rec_seqno / checkpoint_interval * checkpoint_interval
            + max_out;
        if (new_rp > recovery_point)
          recovery_point = new_rp;

        //  fprintf(stderr, "Complete rec reply with seqno %qd rec_point=%qd\n",rec_seqno,  recovery_point);

        // Update view number
        //View rec_view = K_max<View>(f()+1, rr_views, n(), View_max);
        delete rr;
        rr = 0;
      }
    }
    return;
  }
  delete m;
}

void A_Replica::send_null()
{
  //fprintf(stderr, "@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@#@\n");
  th_assert(id() == primary(), "Invalid state");

  Seqno max_rec_point = max_out + (max_rec_n + checkpoint_interval - 1)
      / checkpoint_interval * checkpoint_interval;

  if (max_rec_n && max_rec_point > last_stable && has_new_view())
  {
    if (rqueue.size() == 0 && seqno <= last_executed && seqno + 1 <= max_out
        + last_stable)
    {
      // Send null request if there is a recovery in progress and there
      // are no outstanding requests.
      seqno++;
      A_Req_queue empty;
      A_Pre_prepare* pp = new A_Pre_prepare(view(), seqno, empty);
#ifdef MSG_DEBUG
      fprintf(stderr, "Replica %i (primary=%i): sending pre-prepare to all replicas\n", this->id(), primary());
#endif
      send(pp, All_replicas);
      plog.fetch(seqno).add_mine(pp);
    }
  }
  ntimer->restart();
  // TODO: backups should force view change if primary does not send null requests
  // to allow recoveries to complete.
}

//
// Timeout handlers:
//

void A_vtimer_handler()
{
  //fprintf(stderr, " ###### @@ vtimer expired! @@ ######\n");
  th_assert(A_replica, "replica is not initialized\n");

  if (A_replica->cur_state != replica_state_NORMAL) {
    A_replica->vtimer->restart();
    return;
  }

  if (!A_replica->delay_vc())
  {
#ifdef EXPONENTIAL_VTIMER_TIMEOUT
    // PL: we double the period, to ensure liveness
    // This fits more with the thesis.
    A_replica->vtimer->adjust(A_replica->vtimer->get_period() * 2);
#endif

    fprintf(
        stderr,
        "Replica %i, primary %i, view %qd : sending view change (vtimer expired). New timeout = %i\n",
        A_replica->id(), A_replica->primary(), A_replica->view(),
        A_replica->vtimer->get_period());

    A_replica->send_view_change();
  }
  else
  {
    //fprintf(stderr, "restarting vtimer 4\n");
    //fprintf(stderr, "vtimer restart (3)\n");
    A_replica->vtimer->restart();
  }
}

#ifdef THROUGHPUT_ADAPTIVE
//timer started after having observed a throughput lower than expected
void throughput_timer_handler()
{
  th_assert(A_replica, "replica is not initialized\n");
  if (!A_replica->delay_vc())
  {
    //    fprintf(stderr, "ADAPTIVE: sending view change for view %qd (throughput_timer expired)\n", A_replica->view()+1);
#ifdef DELAY_ADAPTIVE
    A_replica->pre_prepare_timer->stop();
#endif

    fprintf(
        stderr,
        "Replica %i, primary %i, view %qd : sending view change (throughput_timer expired)\n",
        A_replica->id(), A_replica->primary(), A_replica->view());

    A_replica->send_view_change();
  }
  else
  {
    if (A_replica->delay_vc())
    {
      //fprintf(stderr, "ADAPTIVE: restarting throughput_timer \n");
      A_replica->throughput_timer->restart();
    }
  }
}

void increment_timer_handler()
{
  th_assert(A_replica, "replica is not initialized\n");
  A_replica->time_to_increment = true;
  //  fprintf(stderr, "--- time to increment the required throughput! ---\n");
}

#ifdef THROUGHPUT_ADAPTIVE_PERIODIC_COMPUTATION
void compute_expectations_handler()
{
  A_Time current = A_rdtsc();
  A_replica->total_nb_checkpoints++; // I know this is not a checkpoint, but this variable is
  // used to display periodically the expectations

  if ((A_replica->checkpoints_in_new_view > number_of_checkpoints_before_adpating)
      && A_replica->has_new_view())
  {
    long long timer_period = A_diffTime(current, A_replica->start_cycle_4_periodic_thr_adaptive_computation);
    float obs_throughput_in_timer = ((float) A_replica->req_count)
        / ((float) timer_period);

#ifdef ORIGINAL_AARDVARK_THROUGHPUT_ADAPTIVE
    A_replica->obs_throughput = obs_throughput_in_timer;
#else
    if (A_replica->sliding_window_size == 0)
    {
      long long view_duration = A_diffTime(current, A_replica->last_view_time);
      float obs_throughput_in_view = ((float) A_replica->req_count_vc)
          / ((float) view_duration);
      A_replica->obs_throughput = obs_throughput_in_view;
    }
    else
    {
      // update the sliding window
      A_replica->sliding_window[A_replica->sliding_window_cur_idx]
          = obs_throughput_in_timer;
      A_replica->sliding_window_cur_idx = (A_replica->sliding_window_cur_idx + 1)
          % A_replica->sliding_window_size;
      A_replica->sliding_window_nb_got
          = MIN(A_replica->sliding_window_nb_got+1, A_replica->sliding_window_size);

      // compute the mean
      float obs_throughput_in_sliding_window = 0;
      for (int i = 0; i < A_replica->sliding_window_nb_got; i++)
      {
        obs_throughput_in_sliding_window += A_replica->sliding_window[i];
      }
      obs_throughput_in_sliding_window /= A_replica->sliding_window_nb_got;
      A_replica->obs_throughput = obs_throughput_in_sliding_window;
    }
#endif

    A_replica->obs_throughput_in_checkpoint = A_replica->obs_throughput;
    A_replica->adapt_throughput(current);

#ifdef RRBFT_ATTACK
    // compute the next expected throughput (if the malicious primary remains undetected)
    if (A_replica->id() == MALICIOUS_PRIMARY_ID and A_replica->id() == A_replica->primary())
    {
#ifdef ORIGINAL_AARDVARK_THROUGHPUT_ADAPTIVE
      // increment the required throughput
      if ((A_replica->obs_throughput * new_req_throughput_scaling_factor) > (A_replica->req_throughput
              + A_replica->req_throughput_increment))
      {
        A_replica->next_expected_throughput = A_replica->obs_throughput * A_replica->new_req_throughput_scaling_factor;
        }
      }
      else
      {
#endif
        A_replica->next_expected_throughput = A_replica->req_throughput + A_replica->req_throughput_increment;
#ifdef ORIGINAL_AARDVARK_THROUGHPUT_ADAPTIVE
      }
#endif
    }
#endif
  }

A_replica->last_throughput_of_replica[A_replica->primary()] = A_replica->obs_throughput;

  if (A_replica->first_checkpoint_after_view_change && A_replica->has_new_view())
  {
    A_replica->first_checkpoint_after_view_change = false;
    A_replica->vc_already_triggered = false;
  }
  A_replica->req_count = 0;

#ifdef RRBFT_ATTACK
#ifdef THROUGHPUT_ADAPTIVE_PERIODIC_COMPUTATION
  A_replica->nb_req_in_pp_since_last_expectations_computation = 0;
#else
  A_replica->nb_pp_since_checkpoint_start = 0;
  A_replica->nb_req_in_pp_since_checkpoint_start = 0;
#endif
#endif

  A_replica->start_cycle_4_periodic_thr_adaptive_computation = A_currentTime();
  A_replica->compute_expectations_timer->restart();
}
#endif

#endif

#ifdef DELAY_ADAPTIVE
void pre_prepare_timer_handler()
{
  th_assert(A_replica, "replica is not initialized\n");

  if (A_replica->cur_state != replica_state_NORMAL) {
      A_replica->pre_prepare_timer->restart();
      return;
  }

  // A_replica->view() <= 4 is set when you deactivate the heartbeat timer.
  // You need it for the first views, otherwise the expectations are never computed
  if (/*A_replica->view() <= 4 &&*/ A_replica->id() != A_replica->primary() && A_replica->received_pre_prepares
      < expected_pre_prepares)
  {
    if (!A_replica->delay_vc())
    {
#ifdef THROUGHPUT_ADAPTIVE
      A_replica->throughput_timer->restop();
#endif

      A_replica->pre_prepare_timer->adjust(
          A_replica->pre_prepare_timer->get_period() * 2);

      fprintf(
          stderr,
          "ADAPTIVE: sending view change for view %qd (pre_prepare_timer expired). Doubling the period\n",
          A_replica->view() + 1);

      A_replica->send_view_change();
    }
    else
    {
      A_replica->pre_prepare_timer->restart();
    }
  }
  else
  {
    A_replica->pre_prepare_timer->restop();
    //    fprintf(stderr, "we passed the pre-prepare timer check, now we're resetting to the original value and restarting it\n");
    A_replica->pre_prepare_timer->adjust(pre_prepare_timer_duration);
    A_replica->pre_prepare_timer->restart();
    A_replica->received_pre_prepares = 0;
  }
}

#endif

void delay_pre_prepare_timer_handler()
{
  if (A_replica->has_new_view() && A_replica->primary() == A_replica->id())
  {
    A_replica->send_pre_prepare(true);
  }
}

void A_stimer_handler()
{
  //fprintf(stderr, "--- stimer expired --- \n");
  th_assert(A_replica, "replica is not initialized\n");

  if (A_replica->cur_state != replica_state_NORMAL) {
      A_replica->stimer->restart();
      return;
  }

  A_replica->send_status();

  A_replica->stimer->restart();
  //fprintf(stderr, "--- stimer restarted --- \n");
}

void A_ntimer_handler()
{
  //fprintf(stderr, " --- ntimer expired --- \n");
  th_assert(A_replica, "replica is not initialized\n");

  if (A_replica->cur_state != replica_state_NORMAL) {
      A_replica->ntimer->restart();
      return;
  }

  A_replica->send_null();
}

bool A_Replica::has_req(int cid, const Digest &d)
{
  A_Request* req = rqueue.first_client(cid);

  if (req && req->digest() == d)
    return true;

  return false;
}

A_Message* A_Replica::pick_next_status()
{
  for (int identities_checked = 0; identities_checked < num_replicas; identities_checked++)
  {
    status_to_process++;
    status_to_process %= num_replicas;
    if (status_pending[status_to_process] != NULL)
    {
      //fprintf(stderr, "@@@@@@ handling stored status from %d @@@@@@ \n",status_to_process);
      A_Message* m = status_pending[status_to_process];
      status_pending[status_to_process] = NULL;
      return m;
    }
  }
  // status_pending is empty...
  return NULL;
}

// old code which uses the original circular buffer
#if 0
bool A_Replica::cb_write_message(A_Message* message)
{
  //fprintf(stderr, "Writing --- ri: %d, wi: %d \n",cb_read_index, cb_write_index);
  if (((cb_write_index + 1) % circular_buffer_size) == cb_read_index)
    return false; //the buffer is full

  //the buffer is not full
  circular_buffer[cb_write_index] = message;
  cb_write_index = (cb_write_index + 1) % circular_buffer_size;
  return true;
}

A_Message* A_Replica::cb_read_message()
{

  //fprintf(stderr, "Reading --- ri: %d, wi: %d \n",cb_read_index, cb_write_index);
  if (cb_write_index == cb_read_index)
    return NULL; //the buffer is empty

  //the buffer is not empty
  A_Message* temp = circular_buffer[cb_read_index];
  circular_buffer[cb_read_index] = circular_buffer_magic;
  cb_read_index = (cb_read_index + 1) % circular_buffer_size;
  return temp;
}
#endif

void A_Replica::handle(A_View_change *m)
{
#ifdef MSG_DEBUG
  fprintf(
      stderr,
      "Replica %i  (primary=%i) (view %qd) handles a View change from %i for view %qd\n",
      this->id(), primary(), view(), m->id(), m->view());
#endif

  //muting replicas

  if (has_new_view() && m->view() > view())
  {
    // it seems that we get here only at the beginning of the execution.
    //fprintf(stderr, "A_Replica %i  (primary=%i) (view %qd): muted A_replica %d\n",
    //    this->id(), primary(), view(), m->id());

    excluded_replicas[m->id()] = true;
  }

  //  fprintf(stderr, "RECV: view change v=%qd from %d\n", m->view(), m->id());
  bool modified = vi.add(m);
  fflush(NULL);
  if (!modified)
  {
    //fprintf(stderr, "A_Replica %i, vi.add(m) has failed\n", id());
    return;
  }
  // TODO: memoize maxv and avoid this computation if it cannot change i.e.
  // m->view() <= last maxv. This also holds for the next check.
  View maxv = vi.max_view();
  if (maxv > v)
  {
    // A_Replica has at least f+1 view-changes with a view number
    // greater than or equal to maxv: change to view maxv.
    v = maxv - 1;
    vc_recovering = true;
    //fprintf(stderr, "joining a view change\n");
    send_view_change();

    return;
  }

  if (limbo && primary() != node_id)
  {
    maxv = vi.max_maj_view();
    th_assert(maxv <= v, "Invalid state");

    if (maxv == v)
    {
      // A_Replica now has at least 2f+1 view-change messages with view  greater than
      // or equal to "v"

      // Start timer to ensure we move to another view if we do not
      // receive the new-view message for "v".
      //fprintf(stderr, "starting vtimer 1\n");
      //fprintf(stderr, "vtimer restart (6)\n");
      vtimer->restart();
      limbo = false;
      vc_recovering = true;
    }
  }
}

void A_Replica::handle(A_New_view *m)
{
  //fprintf(stderr, "\t\t\t\t\t\tnew view\n");
  //  fprintf(stderr, "RECV: new view v=%qd from %d\n", m->view(), m->id());

#ifdef MSG_DEBUG
  fprintf(
      stderr,
      "Replica %i (primary=%i) (view %qd) handles a New view from %d for view %qd\n",
      this->id(), primary(), view(), m->id(), m->view());
#endif

  vi.add(m);

#ifdef FAIRNESS_ADAPTIVE

  for (int j = 0; j < num_principals; j++)
  {
    //I may have stale informations in my execute_snapshot array... resetting it.
    executed_snapshot[j] = -1;
  }

#endif
}

void A_Replica::handle(A_View_change_ack *m)
{
  //fprintf(stderr, "RECV: view-change ack v=%qd from %d for %d\n", m->view(), m->id(), m->vc_id());

#ifdef MSG_DEBUG
  fprintf(
      stderr,
      "Replica %i (primary=%i) (view %qd) handles a View change ack from %i for %i for view %qd\n",
      this->id(), primary(), view(), m->id(), m->vc_id(), m->view());
#endif

  vi.add(m);
}

void A_Replica::send_view_change()
{
  // Do not send the view change if view changes are deactivated.
  if (view_change_deactivated)
  {
    return;
  }

  // Move to next view.

  //unmuting replicas
  for (int j = 0; j < num_replicas; j++)
    excluded_replicas[j] = false;
  //fprintf(stderr, "all replicas unmuted\n");

#ifdef DELAY_ADAPTIVE
  pre_prepare_timer->restop();
#endif
  v++;
  /*
   fprintf(stderr, "sending a view_change, v: %qd, last_executed: %qd \n", v,
   last_executed);
   */

  status_messages_badly_needed = true; // avoid status message de-prioritization


#ifdef REPLICA_FLOOD_PROTECTION
  for (int j = 0; j < 5; j++)
    rmcount[j] = 0;
#endif

  //muting clients
  //fprintf(stderr,
  //    "A_Replica %i, view %qd. Moving to a new view. Excluding clients\n", id(),
  //    view());
  excluded_clients = true;

#ifdef THROUGHPUT_ADAPTIVE
  first_checkpoint_after_view_change = true;
  vc_already_triggered = false;
  throughput_timer->restop();

#ifdef THROUGHPUT_ADAPTIVE_PERIODIC_COMPUTATION
  start_cycle_4_periodic_thr_adaptive_computation = A_currentTime();
  req_count = 0;
#endif
#endif

  cur_primary = v % num_replicas;
  limbo = true;
  vtimer->stop(); // stop timer if it is still running
  //fprintf(stderr, "stopping ntimer (2)\n");
  ntimer->restop();

  if (last_tentative_execute > last_executed)
  {
    // Rollback to last checkpoint
    th_assert(!state.in_fetch_state(), "Invalid state");
    Seqno rc = state.rollback();
    //    fprintf(stderr, "XXXRolled back in vc to %qd with last_executed=%qd\n", rc, last_executed);
    last_tentative_execute = last_executed = rc;
    //    fprintf(stderr, ":):):):):):):):) Set le = %d\n", last_executed);
  }

  last_prepared = last_executed;

  for (Seqno i = last_stable + 1; i <= last_stable + max_out; i++)
  {
    A_Prepared_cert &pc = plog.fetch(i);
    A_Certificate<A_Commit> &cc = clog.fetch(i);

    if (pc.is_complete())
    {
      vi.add_complete(pc.rem_pre_prepare());
    }
    else
    {
      A_Prepare *p = pc.my_prepare();
      if (p != 0)
      {
        vi.add_incomplete(i, p->digest());
      }
      else
      {
        A_Pre_prepare *pp = pc.my_pre_prepare();
        if (pp != 0)
        {
          vi.add_incomplete(i, pp->digest());
        }
      }
    }

    pc.clear();
    cc.clear();
    // TODO: Could remember info about committed requests for efficiency.
  }

  // Create and send view-change message.
  //  fprintf(stderr, "SND: view change %qd\n", v);
  vi.view_change(v, last_executed, &state);
}

A_Time time_last_view = 0;

void A_Replica::process_new_view(Seqno min, Digest d, Seqno max, Seqno ms)
{
  th_assert(ms >= 0 && ms <= min, "Invalid state");

  A_Time time_view_start = A_rdtsc();

  //fprintf(stderr, "process new view: %qd\tmin: %qd\tmax: %qd\tms: %qd\n", v,
  //    min, max, ms);

  //fprintf(stderr, "START NEW VIEW: %qd\tduration: %qd ms\tnb_chkpt: %i\ttime: %qd usec\n", v, A_diffTime(time_view_start, time_last_view) / 1000, checkpoints_in_new_view, A_diffTime(time_view_start, 0));

  time_last_view = time_view_start;

  not_deprioritize_status_before_this = max + 1; //just to be sure... ;)

  // as we change the view, emtpy the list, because we will never
  // send these PP
  if (id() == 0 && byz_pre_prepare_delay) {
    struct delayed_pp *dpp;
    struct list_head *pos, *n;
    list_for_each_safe(pos, n, &delayed_pps) {
      dpp = list_entry(pos, struct delayed_pp, link);
      delete dpp->pp;
      list_del(pos);
      delete dpp;
    }
  }


#ifdef REPLICA_FLOOD_PROTECTION
  if (!flood_protection_active && view() > flood_protection_view
      + flood_protected_views)
  {
    flood_protection_active = true;
    fprintf(stderr, " ******* FLOOD ******* UNMUTING!!! *****\n");
    for (int i = 0; i < num_replicas; i++)
      excluded_replicas[i] = false;
    //system("./unmute_nic eth5 &");
  }
  for (int i = 0; i < 5; i++)
    rmcount[i] = 0;

#endif

#ifdef EXPONENTIAL_VTIMER_TIMEOUT
  //PL: reset the timeout of the vtimer
  vtimer->adjust(vtimer_initial_timeout);
#endif

  vtimer->restop();
  limbo = false;
  vc_recovering = true;

  if (primary(v) == id())
  {
    A_New_view* nv = vi.my_new_view();
#ifdef MSG_DEBUG
    fprintf(
        stderr,
        "Replica %i (primary %i) in view %qd is sending a new view to all replicas\n",
        id(), primary(), view());
#endif
    send(nv, All_replicas);
  }

  // Setup variables used by mark_stable before calling it.
  seqno = max - 1;
  if (last_stable > min)
    min = last_stable;
  low_bound = min;

  if (ms > last_stable)
  {
    // Call mark_stable to ensure there is space for the pre-prepares
    // and prepares that are inserted in the log below.
    mark_stable(ms, last_executed >= ms);
  }

  // Update pre-prepare/prepare logs.
  th_assert(min >= last_stable && max - last_stable - 1 <= max_out,
      "Invalid state");
  for (Seqno i = min + 1; i < max; i++)
  {
    Digest d;
    A_Pre_prepare* pp = vi.fetch_request(i, d);
    A_Prepared_cert& pc = plog.fetch(i);

    if (primary() == id())
    {
      pc.add_mine(pp);
    }
    else
    {
      A_Prepare* p = new A_Prepare(v, i, d);
      pc.add_mine(p);
#ifdef MSG_DEBUG
      fprintf(stderr, "Replica %i, primary %i, view %qd: Sending a prepare to all replicas from process new view\n", id(), primary(), view());
#endif
      send(p, All_replicas);

      th_assert(pp != 0 && pp->digest() == p->digest(), "Invalid state");
      pc.add_old(pp);
    }
  }

  if (primary() == id())
  {
    send_pre_prepare();
    ntimer->start();
  }

  if (last_executed < min)
  {
    has_nv_state = false;
    state.start_fetch(last_executed, min, &d, min <= ms);
  }
  else
  {
    has_nv_state = true;

    // Execute any buffered read-only requests
    for (A_Request *m = ro_rqueue.remove(); m != 0; m = ro_rqueue.remove())
    {
      execute_read_only(m);
      delete m;
    }
  }

  if (primary() != id() && rqueue.size() > 0)
  {
    //fprintf(stderr, "vtimer restart (0)\n");
    vtimer->restart();
  }

#ifdef DELAY_ADAPTIVE
  pre_prepare_timer->restop();
  received_pre_prepares = 0;
  if (id() != primary())
    pre_prepare_timer->restart();
  //call_pre_prepare_timer->restart();
#endif

#ifdef THROUGHPUT_ADAPTIVE
  vc_already_triggered = false;
  time_to_increment = false;
  increment_timer->restart();
  req_count_vc = 0;
  last_view_time = A_rdtsc();
#endif
  checkpoints_in_new_view = 0;

#ifdef RRBFT_ATTACK
  nb_pp_since_checkpoint_start = 0;
  nb_req_in_pp_since_checkpoint_start = 0;

  if (id() == primary() && id() == MALICIOUS_PRIMARY_ID) {
      fprintf(stderr, "The malicious primary is in the place. Make your time, hahaha!!!\n");
  }
#endif

  //unmuting clients
  //fprintf(stderr, "A_Replica %i, view %qd. New view, clients can come back.\n",
  //    id(), view());
  excluded_clients = false;

  print_stuff = true;
  //fprintf(stderr, "DONE:process new view: %qd, has new view: %d\n", v, has_new_view());
}

#ifdef PERIODICALLY_MEASURE_THROUGHPUT
void periodic_thr_measure_handler(void)
{
  if (A_replica->next_measure_idx < MAX_NB_MEASURES)
  {
    float elapsed_usec = A_diffTime(currentTime(),
        A_replica->start_cycle_4_periodic_thr_measure);
    float throughput = A_replica->nb_requests_4_periodic_thr_measure
        / elapsed_usec * 1000000.0;
    A_replica->measured_throughput[A_replica->next_measure_idx] = throughput;

    /*
    fprintf(
        stderr,
        "Calling periodic_thr_measure_handler after %f sec. %d reqs have been executed, for a throughput of %f req/s\n",
        elapsed_usec / 1000000.0, A_replica->nb_requests_4_periodic_thr_measure,
        throughput);
*/

    A_replica->next_measure_idx++;
    A_replica->nb_requests_4_periodic_thr_measure = 0;
    A_replica->start_cycle_4_periodic_thr_measure = A_currentTime();

    A_replica->periodic_thr_measure->restart();
  }
}
#endif

static int stats_already_printed = 0;

void print_statistics_before_dying(int sig)
{
  if (!stats_already_printed)
  {
    stats_already_printed = 1;

#ifdef PERIODICALLY_MEASURE_THROUGHPUT
	fprintf(stderr, "First request: %qu usec\n", A_replica->first_request_time);

    fprintf(stderr, "Periodic throughputs:\n");
    for (int i = 0; i < A_replica->next_measure_idx; i++)
    {
      fprintf(stderr, "%f\n", A_replica->measured_throughput[i]);
    }
#endif

#ifdef RRBFT_ATTACK
    if (A_replica->id() == MALICIOUS_PRIMARY_ID)
    {
      fprintf(stderr, "total_nb_sent_pp = %ld, total_nb_delayed_pp = %ld\n", A_replica->total_nb_sent_pp, A_replica->total_nb_delayed_pp);
      fprintf(stderr, "Sleeping times:\n");
      for (int i = 0; i < A_replica->next_sleeping_time_idx; i++)
      {
        fprintf(stderr, "%d\n", A_replica->sleeping_times[i]);
      }
    }
#endif

    sleep(3600);
  }
}

void A_Replica::enable_replica(bool state)
{
    fprintf(stderr, "A_Replica[%d]: Time %lld will try to switch state, switch %d, when in state %d\n", id(), A_diffTime(A_currentTime(), 0), state, cur_state);
    if (cur_state == replica_state_NORMAL || cur_state == replica_state_STOP) {
        if (state) {
            if (cur_state != replica_state_NORMAL) {
              req_count_switch = 0;
            }
            cur_state = replica_state_NORMAL;
        } else {
            cur_state = replica_state_STOP;
            robust_monitor->set_aardvark_req_throughput(obs_throughput*1e6);
        }
    }
}
