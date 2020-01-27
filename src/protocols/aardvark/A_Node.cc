#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>  
#include <sys/socket.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#include "sfslite/crypt.h"
#include "sfslite/rabin.h"

#include "th_assert.h"
#include "A_parameters.h"
#include "A_Message.h"
#include "A_Time.h"
#include "A_ITimer.h"
#include "A_Principal.h"
#include "A_New_key.h"
#include "A_Node.h"
#include "tcp_net.h"
#include "types.h"

#ifndef NDEBUG
#define NDEBUG
#endif

// Pointer to global A_node instance.
A_Node *A_node = 0;

//Enable statistics
#include "A_Statistics.h"

#define MAXHOSTNAMELEN2 256
 
A_Node::A_Node(FILE *config_file, FILE *config_priv, short req_port)
{
  A_node = this;

  view_change_check = false;

  // Intialize random number generator
  random_init();

#ifndef USE_GETTIMEOFDAY
  // Compute clock frequency.
  init_clock_mhz();
#endif

  // Read private configuration file:
  char pk1[1024], pk2[1024];
  fscanf(config_priv, "%s %s\n", pk1, pk2);
  bigint n1(pk1, 16);
  bigint n2(pk2, 16);
  if (n1 >= n2)
    th_fail("Invalid private file: first number >= second number");

  priv_key = new rabin_priv(n1, n2);
  // TODO: this file should be encrypted under some passphrase and user
  // should be prompted for that passphrase.


  // Read public configuration file:
  // TODO: this should be more robust
  fscanf(config_file, "%256s\n", service_name);

  // read max_faulty and compute derived variables
  fscanf(config_file, "%d\n", &max_faulty);
  num_replicas = 3 * max_faulty + 1;
  if (num_replicas > Max_num_replicas)
    th_fail("Invalid number of replicas");
  threshold = num_replicas - max_faulty;

  // Read authentication timeout
  int at;
  fscanf(config_file, "%d\n", &at);

  // read in all the principals
  char addr_buff[100];
  char fake_addr_buff[100]; // used to store fields that are ignored by nodes and relevant only for replicas
  char pk[1024];
  short port;
  short fake_port; // used to store fields that are ignored by nodes and relevant only for replicas
  short main_thread_bind_port;
  short verifier_thread_bind_port;

  fscanf(config_file, "%d\n", &num_principals);
  //  if (num_replicas > num_principals) 
  //  th_fail("Invalid argument");

  // read in group principal's address
  fscanf(config_file, "%256s %hd\n", addr_buff, &port);
  Addr a;
  bzero((char*) &a, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = inet_addr(addr_buff);
  a.sin_port = htons(port);
  //a.sin_port = port;
  //fprintf(stderr, "port read from file= %d, %d\n",a.sin_port,port);
  group = new A_Principal(num_principals + 1, a);

  // read in remaining principals' addresses and figure out my principal
  char my_hostname[MAXHOSTNAMELEN2 + 1];
  if (gethostname(my_hostname, MAXHOSTNAMELEN2))
  {
    perror("Unable to get hostname");
    exit(1);
  }
  struct hostent *hent = gethostbyname(my_hostname);
  if (hent == 0)
    th_fail("Could not get hostent");
  struct in_addr my_address = *((in_addr*) hent->h_addr_list[0]);
  node_id = -1;

  principals = (A_Principal**) malloc(num_principals * sizeof(A_Principal*));

  replicas_hostname = (char**) malloc(sizeof(char*) * num_replicas);
  replicas_ipaddr = (char**) malloc(sizeof(char*) * num_replicas);
  clients_hostname = (char**) malloc(sizeof(char*) * num_clients());
  clients_ipaddr = (char**) malloc(sizeof(char*) * num_clients());

  for (int i = 0; i < num_replicas; i++)
  {
    replicas_hostname[i] = (char*) malloc(sizeof(char) * (MAXHOSTNAMELEN2 + 1));
    replicas_ipaddr[i] = (char*) malloc(sizeof(char) * (MAXHOSTNAMELEN2 + 1));
  }

  for (int i = 0; i < num_clients(); i++)
  {
    clients_hostname[i] = (char*) malloc(sizeof(char) * (MAXHOSTNAMELEN2 + 1));
    clients_ipaddr[i] = (char*) malloc(sizeof(char) * (MAXHOSTNAMELEN2 + 1));
  }

  replicas_ports = (int*) malloc(num_replicas * sizeof(int));
  clients_ports = (int*) malloc(num_clients() * sizeof(int));

  for (int i = 0; i < num_principals; i++)
  {
    char hostname[MAXHOSTNAMELEN2 + 1];
    if (i < num_replicas)
    {
      //replicas have a different configuration line... some fields to skip at this point...
      //all the unnecessary fields are stored in fake_*
      fscanf(
          config_file,
          "%256s %32s %hd %32s %hd %32s %hd %32s %hd %32s %hd %hd %hd %1024s \n",
          hostname, addr_buff, &port, fake_addr_buff, &fake_port,
          fake_addr_buff, &fake_port, fake_addr_buff, &fake_port,
          fake_addr_buff, &fake_port, &main_thread_bind_port,
          &verifier_thread_bind_port, pk);
      //      fprintf(stderr, "scanned A_replica %s %s %d %s\n",hostname, addr_buff, port,pk);

      strcpy(replicas_hostname[i], hostname);
      strcpy(replicas_ipaddr[i], addr_buff);
      replicas_ports[i] = port;
      fprintf(stderr, "New replica %i: %s || %s || %i\n", i,
          replicas_hostname[i], replicas_ipaddr[i], replicas_ports[i]);
    }
    else
    {
      // normal client, no fields to skip there
      fscanf(config_file, "%256s %32s %hd %1024s \n", hostname, addr_buff,
          &port, pk);

      strcpy(clients_hostname[i - num_replicas], hostname);
      strcpy(clients_ipaddr[i - num_replicas], addr_buff);
      clients_ports[i - num_replicas] = port;

      //fprintf(stderr, "New client %i: %s || %s || %i\n", i - num_replicas,
      //clients_hostname[i - num_replicas], clients_ipaddr[i - num_replicas],
      //clients_ports[i - num_replicas]);
    }

    a.sin_addr.s_addr = inet_addr(addr_buff);
    a.sin_port = htons(port);
    principals[i] = new A_Principal(i, a, pk);
    principals[i]->set_main_thread_bind_port(main_thread_bind_port);

    if ((my_address.s_addr == a.sin_addr.s_addr
        || strcmp(hostname, my_hostname) == 0) && node_id == -1 && (req_port
        == 0 || req_port == port))
    {
      node_id = i;
    }
  }

#ifdef MULTICORE
  fscanf(config_file, "%d\n", &node_id);
#endif

  if (node_id < 0)
    th_fail("Could not find my principal");

  unsigned k[Nonce_size_u];
  A_Principal *p;
  for (int i = 0; i < num_principals; i++)
  {
    p = i_to_p(i);

    /*
     int base_in = 1000 * id() + i;
     k[0] = 0;
     k[1] = 0;
     k[2] = 0;
     k[3] = 0;
     */
    for (int j = 0; j < Nonce_size_u; j++)
      k[j] = 0;
    p->set_in_key(k);

    /*
     int base_out = id() + 1000*i;
     k[0] = 0;
     k[1] = 0;
     k[2] = 0;
     k[3] = 0;
     */
    for (int j = 0; j < Nonce_size_u; j++)
      k[j] = 0;
    p->set_out_key(k);
  }

  // Initialize current view number and primary.
  v = 0;
  cur_primary = 0;

  // Initialize memory allocator for messages.
  A_Message::init();

  // Sleep for more than a second to ensure strictly increasing
  // timestamps.
  sleep(2);

  // Compute new timestamp for cur_rid
  new_tstamp();
  cur_rid = 1;

  last_new_key = 0;
  atimer = new A_ITimer(at, atimer_handler);
}

A_Node::~A_Node()
{
  for (int i = 0; i < num_principals; i++)
    delete principals[i];

  free(principals);
  delete group;
}

void A_Node::send(A_Message *m, int i)
{
  th_assert(i == All_replicas || (i >= 0 && i < num_principals),
      "Invalid argument");

  m->check_msg();

  if (i == All_replicas)
  {
    //  if(m->tag() == A_View_change_tag) fprintf(stderr, "A_node sending view change of size %d to all replicas\n",m->size());
    for (int x = 0; x < num_replicas; x++)
      //  if (x != id()) //some code relies on receiving self-created messages
      send(m, x);
    return;
  }

  const Addr *to = (i == All_replicas) ? group->address()
      : principals[i]->address();

  int error = 0;
  int size = m->size();
  //if(m->tag() == A_View_change_tag) fprintf(stderr, "A_node sending view change of size %d to %d\n",size,i);
  while (error < size)
  {
    //    INCR_OP(num_sendto);
    //    INCR_CNT(bytes_out,size);
    //    START_CC(sendto_cycles);
    error = sendto(sock, m->contents(), size, 0, (struct sockaddr*) to,
        sizeof(Addr));

    //if(m->tag() == A_View_change_tag) fprintf(stderr, "A_node sending view change returned %d\n",error);

    if (error == EAGAIN)
      fprintf(stderr, "need to send this again...\n");
    //    if (error < 0 && error != EAGAIN)
    //    STOP_CC(sendto_cycles);
#ifndef NDEBUG
    if (error < 0 && error != EAGAIN)
    perror("Node::send: sendto");
#endif
  }
}

void A_Node::sendTCP(A_Message *m, int i, int* sockets)
{
  th_assert(i == All_replicas || (i >= 0 && i < num_principals),
      "Invalid argument");

  m->check_msg();

  if (i == All_replicas)
  {
    //  if(m->tag() == A_View_change_tag) fprintf(stderr, "A_node sending view change of size %d to all replicas\n",m->size());
    for (int x = 0; x < num_replicas; x++)
      //  if (x != id()) //some code relies on receiving self-created messages
      sendTCP(m, x, sockets);
    return;
  }

  sendMsg(sockets[i], m->contents(), m->size());
}

void A_Node::send_on_client_port(A_Message *m, int i, int port)
{
  th_assert(i >= 0 && i < num_principals,
      "Invalid argument");

  m->check_msg();

  Addr *to = principals[i]->addressNotConst();
  // send on the client port
  to->sin_port = port;

  int error = 0;
  int size = m->size();
  //if(m->tag() == A_View_change_tag) fprintf(stderr, "A_node sending view change of size %d to %d\n",size,i);
  while (error < size)
  {
    //    INCR_OP(num_sendto);
    //    INCR_CNT(bytes_out,size);
    //    START_CC(sendto_cycles);
    error = sendto(sock, m->contents(), size, 0, (struct sockaddr*) to,
        sizeof(Addr));

    //if(m->tag() == A_View_change_tag) fprintf(stderr, "A_node sending view change returned %d\n",error);

    if (error == EAGAIN)
      fprintf(stderr, "need to send this again...\n");
    //    if (error < 0 && error != EAGAIN)
    //    STOP_CC(sendto_cycles);
#ifndef NDEBUG
    if (error < 0 && error != EAGAIN)
    perror("Node::send: sendto");
#endif
  }
}

bool A_Node::has_messages(long to)
{
  //  START_CC(handle_timeouts_cycles);
  A_ITimer::handle_timeouts();
  //  STOP_CC(handle_timeouts_cycles);

  // Timeout period for select. It puts a lower bound on the timeout
  // granularity for other timers.
  //  START_CC(select_cycles);
  struct timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = to;
  fd_set fdset;
  FD_ZERO(&fdset);
  FD_SET(sock, &fdset);
  int ret = select(sock + 1, &fdset, 0, 0, &timeout);
  if (ret > 0 && FD_ISSET(sock, &fdset))
  {
    //    STOP_CC(select_cycles);
    //    INCR_OP(select_success);
    return true;
  }
  //  STOP_CC(select_cycles);
  //  INCR_OP(select_fail);
  return false;
}

A_Message* A_Node::recv()
{
    return recv(sock);
}

A_Message* A_Node::recv(int s)
{
  A_Message* m = new A_Message(A_Max_message_size);

  while (1)
  {
#ifndef ASYNC_SOCK
    //while (!has_messages(5000));
#endif

    //INCR_OP(num_recvfrom);
    //START_CC(recvfrom_cycles);

    int ret = recvfrom(s, m->contents(), m->msize(), 0, 0, 0);

    //STOP_CC(recvfrom_cycles);

#ifdef LOOSE_MESSAGES
    if (lrand48()%100 < 4)
    {
      ret = 0;
    }
#endif

    if (ret >= (int) sizeof(A_Message_rep) && ret >= m->size())
    {
#ifdef ASYNC_SOCK
      A_ITimer::handle_timeouts();
      INCR_OP( num_recv_success);INCR_CNT(bytes_in, m->size());
#endif
      return m;
    }
#ifdef ASYNC_SOCK
    while (!has_messages(20000))
    ;
#endif
  }
}

void A_Node::gen_auth(char *s, unsigned l, bool in, char *dest,
    bool faultyClient, bool to_print) const
{
  //INCR_OP(num_gen_auth);
  //START_CC(gen_auth_cycles);

  long long unonce = A_Principal::new_umac_nonce();
  memcpy(dest, (char*) &unonce, UNonce_size);
  dest += UNonce_size;

  for (int i = 0; i < num_replicas; i++)
  {
    // Skip myself.
    if (i == node_id)
      continue;

    if (in)
      principals[i]->gen_mac_in(s, l, dest, (char*) &unonce, to_print);
    else
      principals[i]->gen_mac_out(s, l, dest, (char*) &unonce, to_print);

    // following code "breaks" the MAC
    if (faultyClient /*&& i != 0*/)
    {
      dest[0] = 'a';
      dest[1] = 'a';
    }

    //fprintf(stderr, "For A_replica %i the mac is %x %x and unonce = %x %x\n", i,
    //    dest[0], dest[1], (int)*((char*) &unonce), (int)*((char*) &unonce+4));

    dest += UMAC_size;
  }

  // following code "breaks" the MAC
  if (faultyClient)
  {
    A_Message::set_mac_unvalid((A_Message_rep*)s);
  }

  //STOP_CC(gen_auth_cycles);
}

bool A_Node::verify_auth(int i, char *s, unsigned l, bool in, char *dest,
    bool print) const
{
  th_assert(node_id < num_replicas, "Called by non-replica");

  //INCR_OP(num_ver_auth);
  //START_CC(ver_auth_cycles);

  A_Principal *p = i_to_p(i);

  // A_Principal never verifies its own authenticator.
  if (p != 0 && i != node_id)
  {

#ifdef USE_SECRET_SUFFIX_MD5
    int offset = node_id*MAC_size;
    if (node_id > i) offset -= MAC_size;
    bool ret = (in) ? p->verify_mac_in(s, l, dest+offset) : p->verify_mac_out(s, l, dest+offset);
#else
    long long unonce;
    memcpy((char*) &unonce, dest, UNonce_size);
    dest += UNonce_size;

    int offset = node_id * UMAC_size;
    if (node_id > i)
      offset -= UMAC_size;
    bool ret = (in) ? p->verify_mac_in(s, l, dest + offset, (char*) &unonce,
        print) : p->verify_mac_out(s, l, dest + offset, (char*) &unonce, print);

#endif

    /*
     bool ret = false;

     for (int idx = 0; idx < num_replicas; idx++)
     {
     int offset = idx * UMAC_size;
     ret = (in) ? p->verify_mac_in(s, l, dest + offset, (char*) &unonce,
     print) : p->verify_mac_out(s, l, dest + offset, (char*) &unonce,
     print);

     if (ret == false)
     {
     printf(
     "\t VERIFY MAC is FALSE at A_Node %i for A_Node id %i with unonce %x %x at offset %x with idx=%i\n",
     node_id, i, (int) *((char*) &unonce), (int) *((char*) &unonce + 4),
     offset, idx);
     }
     else
     {
     printf(
     "\t VERIFY MAC is TRUE at A_Node %i for A_Node id %i with unonce %x %x at offset %x with idx=%i\n",
     node_id, i, (int) *((char*) &unonce), (int) *((char*) &unonce + 4),
     offset, idx);
     }

     }
     */

    /*
     if (ret == false)
     {
     printf(
     "\t VERIFY MAC is FALSE at A_Node %i for A_Node id %i with unonce %x %x at offset %x (1)\n",
     node_id, i, (int) *((char*) &unonce), (int) *((char*) &unonce + 4),
     offset);
     }
     */

    //STOP_CC(ver_auth_cycles);
    return ret;
  }

  //STOP_CC(ver_auth_cycles);
  return false;
}

void A_Node::gen_signature(const char *src, unsigned src_len, char *sig)
{
  //INCR_OP(num_sig_gen);
  //START_CC(sig_gen_cycles);
  //th_assert(false, "should never sign anything");
  bigint bsig = priv_key->sign(str(src, src_len));
  int size = mpz_rawsize(&bsig);
  if (size + sizeof(unsigned) > sig_size())
    th_fail("Signature is too big");

  memcpy(sig, (char*) &size, sizeof(unsigned));
  sig += sizeof(unsigned);

  mpz_get_raw(sig, size, &bsig);

  //STOP_CC(sig_gen_cycles);
}

unsigned A_Node::decrypt(char *src, unsigned src_len, char *dst, unsigned dst_len)
{
  if (src_len < 2 * sizeof(unsigned))
    return 0;

  bigint b;
  unsigned csize, psize;
  memcpy((char*) &psize, src, sizeof(unsigned));
  src += sizeof(unsigned);
  memcpy((char*) &csize, src, sizeof(unsigned));
  src += sizeof(unsigned);

  if (dst_len < psize || src_len < csize)
    return 0;

  mpz_set_raw(&b, src, csize);

  str ptext = priv_key->decrypt(b, psize);
  memcpy(dst, ptext.cstr(), ptext.len());

  return psize;
}

Request_id A_Node::new_rid()
{
  //PL: old code. Now the rid starts at 0
  /*
  Request_id old = cur_rid;
  if ((unsigned) cur_rid == (unsigned) 0xffffffff)
  {
    new_tstamp();
  }
  if (old >= cur_rid + 1)
    cur_rid = old;
  return ++cur_rid;
  */

  return cur_rid++;
}

void A_Node::new_tstamp()
{
  struct timeval t;
  gettimeofday(&t, 0);
  th_assert(sizeof(t.tv_sec) <= sizeof(int), "tv_sec is too big");
  Long tstamp = t.tv_sec;
  long int_bits = sizeof(int) * 8;
  cur_rid = tstamp << int_bits;
}

void atimer_handler()
{
  th_assert(A_node, "replica is not initialized\n");
}

void A_Node::send_new_key()
{
}

