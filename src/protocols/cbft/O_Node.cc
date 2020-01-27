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
#include "O_Message.h"
#include "O_Principal.h"
#include "O_Node.h"

#include "O_Time.h"
#include "O_ITimer.h"


//#define TRACES

#include <task.h>
// Pointer to global node instance.
O_Node *O_node = 0;
O_Principal *O_my_principal = 0;

O_Node::O_Node(FILE *config_file, FILE *config_priv, char *host_name, short req_port)
{
	O_node = this;

	init_clock_mhz();

	fprintf(stderr, "O_Node: clock_mhz is %llu\n", clock_mhz);

	// Read public configuration file:
	// read max_faulty and compute derived variables
	fscanf(config_file, "%d\n", &max_faulty);
	//Added by Maysam Yabandeh
	//fscanf(config_file, "%d \n", &num_replicas);
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
	group = new O_Principal(num_principals+1, num_principals, a, NULL);

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

	principals = (O_Principal**)malloc(num_principals*sizeof(O_Principal*));

	fprintf(stderr, "num_principals = %d\n", num_principals);
	char temp_host_name[MAXHOSTNAMELEN+1];
	for (int i=0; i < num_principals; i++)
	{
		fscanf(config_file, "%256s %32s %hd %1024s \n", temp_host_name,
				addr_buff, &port, pk);

		a.sin_addr.s_addr = inet_addr(addr_buff);
		a.sin_port = htons(port);

		principals[i] = new O_Principal(i, num_principals, a, addr_buff, port, pk);
		//principals[i] = new O_Principal(i, num_principals, a, pk);
		//fprintf(stderr, "We have parsed entry %s\n", temp_host_name);

		//Added by Maysam Yabandeh - use this when port is unique and there are multiple NIC
		//if (node_id == -1
		if (my_address.s_addr == a.sin_addr.s_addr && node_id == -1
				&& (req_port == 0 || req_port == port))
		{
			node_id = i;
			O_my_principal = principals[i];
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
	    fprintf(stderr, "O_Node: Will not init priv_key\n");
	    priv_key = NULL;
	}

	// Initialize memory allocator for messages.
//	O_Message::init();

	// Initialize socket.
	//sock = socket(AF_INET, SOCK_DGRAM, 0);

	// name the socket
	Addr tmp;
	tmp.sin_family = AF_INET;
	tmp.sin_addr.s_addr = htonl(INADDR_ANY);
	tmp.sin_port = principals[node_id]->address()->sin_port;
	//int error = bind(sock, (struct sockaddr*)&tmp, sizeof(Addr));
	//int sock = netannounce(UDP, 0, principals[node_id]->address()->sin_port); 
	//if (error < 0)
	//{
		//perror("Unable to name socket");
		//exit(1);
	//}
	//fdnoblock(sock);

	/*
	// Set TTL larger than 1 to enable multicast across routers.
	u_char i = 20;
	error = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, (char *)&i,
			sizeof(i));
	if (error < 0)
	{
		perror("unable to change TTL value");
		exit(1);
	}

	error = fcntl(sock, F_SETFL, O_NONBLOCK);
	if (error < 0)
	{
		perror("unable to set socket to asynchronous mode");
		exit(1);
	}
	*/

	// Sleep for more than a second to ensure strictly increasing
	// timestamps.
	sleep(2);

	// Compute new timestamp for cur_rid
	new_tstamp();
}

O_Node::~O_Node()
{
	for (int i=0; i < num_principals; i++)
	{
		delete principals[i];
	}

	free(principals);
	delete group;
}

void O_Node::send(O_Message *m, int i)
{
	th_assert(i == O_All_replicas || (i >= 0&& i < num_principals),
			"Invalid argument");

	if (i == O_All_replicas)
	{
		//send it to all replicas except the primary
		for (int x=0; x<num_replicas; x++)
		if (x != primary())
		send(m,x);
		return;
	}

   int &remotefd = principals[i]->remotefd;
	if (remotefd < 0)
	{
		if((remotefd = netdial(TCP, principals[i]->ip, principals[i]->port)) < 0){
			fprintf(stderr, "Error: netdial failed on calling %d", i);
			close(remotefd);
			return;
		}
		fprintf(stderr, "netdial socket %d\n", remotefd);
		fdnoblock(remotefd);
		taskcreate(recvtask, (void*)(remotefd), STACK);
		//taskyield();
	}

	int error = 0;
	while (error < (int)m->size())
	{
		//error = sendto(sock, m->contents(), m->size(), 0, (struct sockaddr*)to,	sizeof(Addr));
		error = fdwrite(remotefd, m->contents(), m->size());
		//fprintf(stderr, "=%d fdwrite on %d error is %d\n", id(), remotefd, error);
		//int ret = fdread(remotefd, m->contents(), m->msize());
		//fprintf(stderr, "%d ======================================== %d \n", id(), ret);
	}
}

// Added by Maysam Yabandeh
// Note: this function is changed based on TCP
// For UDP we need a different implementation
// This function can receive messages that are divided into multiple packets and
// hence are recevied gradually
void recvtask(void* v)
{
   int remotefd = (int) v;
	taskname("recvtask(%d)",remotefd);
//fprintf(stderr,"=recvtask on socket %d\n", remotefd);
	while (1)
	{
#ifdef TRACES
		fprintf(stderr, "O_Node::recvtask(): reading the socket\n");
#endif
		O_Message* m= new O_Message(O_Max_message_size);
		//int ret = recvfrom(O_node->sock, m->contents(), m->msize(), 0, 0, 0);
		unsigned total = 0;
		int ret;
		do 
		{
			ret = fdread(remotefd, m->contents() + total, O_Max_message_size);
			total += ret;
#ifdef TRACES
			fprintf(stderr, "ret=%d total=%d m->size()=%d\n", ret, total, m->size());
#endif
			if (total >= (int)sizeof(O_Message_rep) && total >= (int)m->size())
			{
#ifdef TRACES
				fprintf(stderr, "O_Node::recvtask(): one message received on the socket\n");
#endif
				//O_ITimer::handle_timeouts();
				O_node->processReceivedMessage(m, remotefd);
				break;//from reading parts of the message
			}
			else if (ret < 0)
			{
#ifdef TRACES
				fprintf(stderr, "Error at O_Node::recvtask(): fdread id=? ret=%d\n", /*id(),*/ ret);
#endif
				perror("READ");
				delete m;
			}
			else if (ret == 0)//there is no data ready yet, give the turn to the others
				taskyield();

		} while ( ret == 0 || (ret > 0 && total < (int)m->size()) );//ret==0 means end of file, and in our case implies that there is no data received yet
	}//while (1)
}

void listentask(void* v)
{
   int cfd,fd;
	int rport,lport=(int)v;
	taskname("listentask(%d)",lport);
	char remote[16];
	if((fd = netannounce(TCP, 0, lport)) < 0){
		fprintf(stderr, "cannot announce on tcp port %d: %s\n", lport, strerror(errno));
		taskexitall(1);
	}
	fprintf(stderr, "listening on tcp port %d\n", lport);
	fdnoblock(fd);
	while((cfd = netaccept(fd, remote, &rport)) >= 0){
		//fprintf(stderr, "connection from %s:%d in socket %d\n", remote, rport, cfd);
		fdnoblock(cfd);
		taskcreate(recvtask, (void*)cfd, STACK);
	}
}

void O_Node::serverListen()
{
	int lport = principals[id()]->port;
	taskcreate(listentask, (void*)(lport), STACK);
}

bool O_Node::has_messages(long to)
{
th_assert(0, "Why I am here? O_Node::has_messages");
	O_ITimer::handle_timeouts();
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

O_Message* O_Node::recv()
{
th_assert(0, "Why I am here? O_Node::recv");
fprintf(stderr,"=%d O_Node::recv\n", id());
	O_Message* m= new O_Message(O_Max_message_size);
	while (1)
	{
#ifdef TRACES
		fprintf(stderr, "=%d O_Node::recv(): reading the socket\n", id());
#endif
		int ret = recvfrom(sock, m->contents(), m->msize(), 0, 0, 0);
		if (ret >= (int)sizeof(O_Message_rep) && ret >= (int)m->size())
		{
#ifdef TRACES
			fprintf(stderr, "=%d O_Node::recv(): one message received on the socket\n", id());
#endif
			O_ITimer::handle_timeouts();
			return m;
		}
		while (!has_messages(20000))
			;
	}
}

void O_Node::gen_auth(char *s, unsigned l, char *dest) const
{
	long long unonce = O_Principal::new_umac_nonce();
	memcpy(dest, (char*)&unonce, O_UNonce_size);
	dest += O_UNonce_size;

	O_Principal *p =i_to_p(id());

	for (int i=0; i < num_replicas; i++)
	{
		// Skip myself.
		if (i == node_id)
		{
			continue;
		}

		p->gen_mac(s, l, dest, i, (char*)&unonce);
		dest += O_UMAC_size;
	}
}

bool O_Node::verify_auth(int src_pid, char *s, unsigned l, char *dest) const
{
	th_assert(node_id < num_replicas, "Called by non-replica");

	O_Principal *p = i_to_p(src_pid);

	// Principal never verifies its own authenticator.
	if (p != 0 && src_pid != node_id)
	{
		long long unonce;
		memcpy((char*)&unonce, dest, O_UNonce_size);
		dest += O_UNonce_size;
		int offset = node_id*O_UMAC_size;
		if (node_id > src_pid)
		{
			offset -= O_UMAC_size;
		}

		return p->verify_mac(s, l, dest + offset, (char*)&unonce);
	}
	return false;
}

void O_Node::gen_signature(const char *src, unsigned src_len, char *sig)
{
	bigint bsig = priv_key->sign(str(src, src_len));
	int size = mpz_rawsize(&bsig);
	if (size + sizeof(unsigned) > sig_size())
		th_fail("Signature is too big");

	memcpy(sig, (char*) &size, sizeof(unsigned));
	sig += sizeof(unsigned);

	mpz_get_raw(sig, size, &bsig);
}

unsigned O_Node::decrypt(char *src, unsigned src_len, char *dst,
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


Request_id O_Node::new_rid()
{
	if ((unsigned)cur_rid == (unsigned)0xffffffff)
	{
		new_tstamp();
	}
	return ++cur_rid;
}

void O_Node::new_tstamp()
{
	struct timeval t;
	gettimeofday(&t, 0);
	th_assert(sizeof(t.tv_sec) <= sizeof(int), "tv_sec is too big");
	Long tstamp = t.tv_sec;
	long int_bits = sizeof(int)*8;
	cur_rid = tstamp << int_bits;
}
