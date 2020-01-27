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
#include "parameters.h"
#include "Q_Message.h"
#include "Q_Principal.h"
#include "Q_Node.h"

extern "C" {
#include "TCP_network.h"
}

#include "Q_Time.h"
#include "Q_ITimer.h"

// Pointer to global node instance.
Q_Node *Q_node = 0;
Q_Principal *Q_my_principal = 0;

Q_Node::Q_Node(FILE *config_file, FILE *config_priv, char *host_name, short req_port) : incoming_queue()
{
	Q_node = this;

	init_clock_mhz();

	fprintf(stderr, "Q_Node: clock_mhz is %llu\n", clock_mhz);

	// Read public configuration file:
	// read max_faulty and compute derived variables
	fscanf(config_file, "%d\n", &max_faulty);
	num_replicas = 3 * max_faulty + 1;
	if (num_replicas > Max_num_replicas)
	{
		th_fail("Invalid number of replicas");
	}

	// read in all the principals
	char addr_buff[100];
	char pk[1024];
	short port;

	fscanf(config_file, "%d\n", &num_principals);
	if (num_replicas > num_principals)
	{
		th_fail("Invalid argument");
	}

	// read in group principal's address
	fscanf(config_file, "%256s %hd\n", addr_buff, &port);
	Addr a;
	bzero((char*)&a, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = inet_addr(addr_buff);
	a.sin_port = htons(port);
	group = new Q_Principal(num_principals+1, num_principals, a, NULL);

	// read in remaining principals' addresses and figure out my principal
	//char host_name[MAXHOSTNAMELEN+1];
	//if (gethostname(host_name, MAXHOSTNAMELEN))
	//{
	//	perror("Unable to get hostname");
	//	exit(1);
	//}

	struct hostent *hent = gethostbyname(host_name);
	if (hent == 0)
	{
		th_fail("Could not get hostent");
	}

	struct in_addr my_address = *((in_addr*)hent->h_addr_list[0]);
	node_id = -1;

	principals = (Q_Principal**)malloc(num_principals*sizeof(Q_Principal*));

	fprintf(stderr, "num_principals = %d\n", num_principals);
	char temp_host_name[MAXHOSTNAMELEN+1];
	for (int i=0; i < num_principals; i++)
	{
		fscanf(config_file, "%256s %32s %hd %1024s \n", temp_host_name,
				addr_buff, &port, pk);

		a.sin_addr.s_addr = inet_addr(addr_buff);
		a.sin_port = htons(port);

		principals[i] = new Q_Principal(i, num_principals, a, pk);
		//fprintf(stderr, "We have parsed entry %s\n", temp_host_name);

		if (my_address.s_addr == a.sin_addr.s_addr && node_id == -1
				&& (req_port == 0 || req_port == port))
		{
			node_id = i;
			Q_my_principal = principals[i];
			fprintf(stderr, "We have parsed entry %s and assigned node_id = %d\n", temp_host_name, node_id);
		}
	}

	if (node_id < 0)
	{
		th_fail("Could not find my principal");
	}

	if (config_priv != NULL) {
	    // Read private configuration file:
	    char pk1[1024], pk2[1024];
	    fscanf(config_priv, "%s %s\n", pk1, pk2);
	    bigint n1(pk1, 16);
	    bigint n2(pk2, 16);
	    if (n1 >= n2)
		th_fail("Invalid private file: first number >= second number");

	    priv_key = new rabin_priv(n1, n2);
	} else {
	    fprintf(stderr, "Q_Node: Will not init priv_key\n");
	    priv_key = NULL;
	}

	// Initialize memory allocator for messages.
//	Q_Message::init();

	// Initialize socket.
	sock = socket(AF_INET, SOCK_DGRAM, 0);

	// name the socket
	Addr tmp;
	tmp.sin_family = AF_INET;
	tmp.sin_addr.s_addr = htonl(INADDR_ANY);
	//tmp.sin_addr.s_addr = principals[node_id]->address()->sin_addr.s_addr;
	tmp.sin_port = principals[node_id]->address()->sin_port;
	int error = bind(sock, (struct sockaddr*)&tmp, sizeof(Addr));
	if (error < 0)
	{
		perror("Unable to name socket");
		exit(1);
	}

	// Set TTL larger than 1 to enable multicast across routers.
	u_char i = 20;
	error = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, (char *)&i,
			sizeof(i));
	if (error < 0)
	{
		perror("unable to change TTL value");
		exit(1);
	}

	struct in_addr interface_addr = principals[node_id]->address()->sin_addr;
	error = setsockopt (sock, IPPROTO_IP, IP_MULTICAST_IF, &interface_addr, sizeof(interface_addr));
	if (error < 0)
	{
	    perror("Unable to set outgoing interface for multicast");
	    exit(1);
	}

	error = fcntl(sock, F_SETFL, O_NONBLOCK);
	if (error < 0)
	{
		perror("unable to set socket to asynchronous mode");
		exit(1);
	}

	// Sleep for more than a second to ensure strictly increasing
	// timestamps.
	sleep(2);

	// Compute new timestamp for cur_rid
	new_tstamp();
  pthread_mutex_init(&incoming_queue_mutex, NULL);
  pthread_cond_init(&not_empty_incoming_queue_cond, NULL) ;
}

Q_Node::~Q_Node()
{
	for (int i=0; i < num_principals; i++)
	{
		delete principals[i];
	}

	free(principals);
	delete group;
}

void Q_Node::send(Q_Message *m, int i)
{
	th_assert(i == Q_All_replicas || (i >= 0&& i < num_principals),
			"Invalid argument");

	const Addr *to =(i == Q_All_replicas) ? group->address()
			: principals[i]->address();

	int error = 0;
	while (error >= 0 && error < (int)m->size())
	{
		error = sendto(sock, m->contents(), m->size(), 0, (struct sockaddr*)to,
				sizeof(Addr));
	}
}

bool Q_Node::has_messages(long to)
{
	Q_ITimer::handle_timeouts();
	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = to;
	fd_set fdset;
	FD_ZERO(&fdset);
	FD_SET(sock, &fdset);
	int ret = select(sock+1, &fdset, 0, 0, &timeout);
	if (ret > 0&& FD_ISSET(sock, &fdset))
	{
		return true;
	}
	return false;
}

Q_Message* Q_Node::recv()
{
	Q_Message* m= new Q_Message(Q_Max_message_size);
	while (1)
	{
#ifdef TRACES
		fprintf(stderr, "Q_Node::recv(): reading the socket\n");
#endif
		int ret = recvfrom(sock, m->contents(), m->msize(), 0, 0, 0);
		if (ret >= (int)sizeof(Q_Message_rep) && ret >= (int)m->size())
		{
#ifdef TRACES
			fprintf(stderr, "Q_Node::recv(): one message received on the socket\n");
#endif
			Q_ITimer::handle_timeouts();
			return m;
		}
		while (!has_messages(20000))
			;
	}
}

Q_Message* Q_Node::recv(int socket)
{
  Q_Message* m = new Q_Message(Q_Max_message_size);

  recv_all_blocking(socket, (char *)m->contents(), sizeof(Q_Message_rep),
      0);

  int payload_size = m->size() - sizeof(Q_Message_rep);
  recv_all_blocking(socket, m->contents() + sizeof(Q_Message_rep),
      payload_size, 0);
  return m;
}

void Q_Node::gen_auth(char *s, unsigned l, char *dest) const
{
	long long unonce = Q_Principal::new_umac_nonce();
	memcpy(dest, (char*)&unonce, Q_UNonce_size);
	dest += Q_UNonce_size;

	Q_Principal *p =i_to_p(id());

	for (int i=0; i < num_replicas; i++)
	{
		// Skip myself.
		if (i == node_id)
		{
			continue;
		}

		p->gen_mac(s, l, dest, i, (char*)&unonce);
		dest += Q_UMAC_size;
	}
}

bool Q_Node::verify_auth(int src_pid, char *s, unsigned l, char *dest) const
{
	th_assert(node_id < num_replicas, "Called by non-replica");

	Q_Principal *p = i_to_p(src_pid);

	// Principal never verifies its own authenticator.
	if (p != 0 && src_pid != node_id)
	{
		long long unonce;
		memcpy((char*)&unonce, dest, Q_UNonce_size);
		dest += Q_UNonce_size;
		int offset = node_id*Q_UMAC_size;
		if (node_id > src_pid)
		{
			offset -= Q_UMAC_size;
		}

		return p->verify_mac(s, l, dest + offset, (char*)&unonce);
	}
	return false;
}

void Q_Node::gen_signature(const char *src, unsigned src_len, char *sig)
{
	bigint bsig = priv_key->sign(str(src, src_len));
	int size = mpz_rawsize(&bsig);
	if (size + sizeof(unsigned) > sig_size())
		th_fail("Signature is too big");

	memcpy(sig, (char*) &size, sizeof(unsigned));
	sig += sizeof(unsigned);

	mpz_get_raw(sig, size, &bsig);
}

unsigned Q_Node::decrypt(char *src, unsigned src_len, char *dst,
		unsigned dst_len)
{
	if (src_len < 2* sizeof(unsigned))
		return 0;

	bigint b;
	unsigned csize, psize;
	memcpy((char*)&psize, src, sizeof(unsigned));
	src += sizeof(unsigned);
	memcpy((char*)&csize, src, sizeof(unsigned));
	src += sizeof(unsigned);

	if (dst_len < psize || src_len < csize)
		return 0;

	mpz_set_raw(&b, src, csize);

	str ptext = priv_key->decrypt(b, psize);
	memcpy(dst, ptext.cstr(), ptext.len());

	return psize;
}


Request_id Q_Node::new_rid()
{
	if ((unsigned)cur_rid == (unsigned)0xffffffff)
	{
		new_tstamp();
	}
	return ++cur_rid;
}

void Q_Node::new_tstamp()
{
	struct timeval t;
	gettimeofday(&t, 0);
	th_assert(sizeof(t.tv_sec) <= sizeof(int), "tv_sec is too big");
	Long tstamp = t.tv_sec;
	long int_bits = sizeof(int)*8;
	cur_rid = tstamp << int_bits;
}
