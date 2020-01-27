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
#include "R_Message.h"
#include "R_Principal.h"
#include "R_Rep_info.h"
#include "R_Node.h"
#include "pthread.h"

#include "R_BaseRequest.h"
#include "R_Request.h"
#include "R_ACK.h"

#include "R_Time.h"

#define max(a,b) (a>=b?a:b)
#define min(a,b) (a<=b?a:b)

#define AVI_A_SIZE 2
#define AVI_V_SIZE (f()+1)

#define AVI_A_AT(X,dest,entry,pos) (((char*)X)[(dest)*(3*f()+1)*AVI_A_SIZE+(entry)*AVI_A_SIZE+pos])
#define AVI_V_AT(X,dest,entry,pos) (((char*)X)[(dest)*(3*f()+1)*AVI_V_SIZE+(entry)*AVI_V_SIZE+pos])

void DUMP(avi_ventry *aviv, int size = 2) {
    for (int i = 0; i < size; i++) {
    	fprintf(stderr, "aviv[%d], size %d = [", i, aviv[i].len);
	if (aviv[i].src == NULL) {
	    fprintf(stderr, "]\n");
	    continue;
	}
	for (int j = 0; j < aviv[i].len; j++) {
	    fprintf(stderr, "%02hhx ", aviv[i].src[j]);
	}
	fprintf(stderr, "]\n");
    }
}

void R_Node::create_avi_auth_table(avi_table* avi_a)
{
    *avi_a = (avi_table)malloc(2*(3*f()+1)*2*(3*f()+1));
    char *begin = *avi_a;
    for (int dist=0; dist < 2*(3*f()+1); dist++) {
    	for (int entry=0; entry < 3*f()+1; entry++) {
	    if (dist >= 3*f()) {
	    	*begin = AVI_T; // on sequencer, ACK is without seqno
#ifndef USE_CLIENT_SIGNATURES
		if (entry == 0 && dist == 3*f())
		    *begin = AVI_T0;
#endif
		begin++;
		*begin = AVI_NONE;
		if (dist >= 2*(3*f()+1)-(f()+1))
		    *begin = AVI_hC;
		begin++;
		continue;
	    }
	    if (entry == 0) {
#ifdef USE_CLIENT_SIGNATURES
		*begin = AVI_H;
#else
		*begin = AVI_h0;
#endif
		begin++;
		*begin = AVI_NONE;
		begin++;
		continue;
	    }
	    if (dist < (3*f()+1)-entry) {
	    	*begin = AVI_h0;
		begin++;
		*begin = AVI_NONE;
		begin++;
	    } else {
	    	*begin = AVI_H;
		begin++;
		*begin = AVI_NONE;
		begin++;
	    }
	}
    }
}

void R_Node::create_avi_verify_table(avi_table avi_a, avi_table* avi_v)
{
    *avi_v = (avi_table)malloc((f()+1)*(3*f()+1)*2*(3*f()+1));
    char *begin = *avi_v;
    for (int dist=0; dist < 2*(3*f()+1); dist++) {
	for (int entry=0; entry < 3*f()+1; entry++) {
	    for (int pos=0; pos < f()+1; pos++) {
		*begin = AVI_NONE;
		begin++;
	    }
	}
    }
    begin = *avi_v;
    for (int dist=0; dist < 2*(3*f()+1); dist++) {
	for (int entry=0; entry < 3*f()+1; entry++) {
	    char *bb = & AVI_V_AT(*avi_v, dist, entry, 0);
	    begin = & AVI_V_AT(*avi_v, dist, entry, 0);
	    if (dist < f()+1) {
		*begin = AVI_CLIENT;
		begin++;
	    }
	    if (dist==0)
	    	continue;
	    for (int tdist=max(0,dist-(f()+1)); tdist <= max(0, dist-1); tdist++) {
		*begin = AVI_A_AT(avi_a, tdist, entry, 0);
		begin++;
	    }
	}
    }
}

int R_Node::avi_distance(R_BaseRequest *req) const
{
    if (req->has_tag(R_Request_tag, sizeof(R_Request_rep)))
	return avi_distance((R_Request*)req);
    else
	return avi_distance((R_ACK*)req);
}

int R_Node::avi_distance(R_Request *req) const
{
    return distance(req->replica_id(), id());
}

int R_Node::avi_distance(R_ACK *req) const
{
    // this is only important for the replica which will send request back to client
    if (unlikely(req->just_created))
	return distance(req->replica_id(), id());
    else
	return distance(req->replica_id(), id()) + num_replicas;
}

// Pointer to global R_node instance.
R_Node *R_node = 0;
R_Principal *R_my_principal = 0;

R_Node::R_Node(FILE *config_file, FILE *config_priv, char *host_name, short req_port) :
	incoming_queue()
{
	R_node = this;

	// Intialize random number generator
	random_init();

	init_clock_mhz();

	fprintf(stderr, "R_Node: clock_mhz is %llu\n", clock_mhz);

	// Read private configuration file:
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
	    fprintf(stderr, "R_Node: Will not init priv_key\n");
	    priv_key = NULL;
	}

	// read max_faulty and compute derived variables
	fscanf(config_file, "%d\n", &max_faulty);
	num_replicas = 3 * max_faulty + 1;
	if (num_replicas > Max_num_replicas)
	{
		th_fail("Invalid number of replicas");
	}

	// read in all the principals
	char addr_buff[257];

	if (fscanf(config_file, "%d\n", &num_principals) != 1) {
	    th_fail("Could not read number of principals");
	}
	if (num_replicas > num_principals)
	{
		th_fail("Invalid argument");
	}

	// read the multicast address
	short port;
	fscanf(config_file, "%256s %hd\n", addr_buff, &port);
	Addr a;
	bzero((char*)&a, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = inet_addr(addr_buff);
	a.sin_port = htons(port);
	group = new R_Principal(num_principals + 1, num_principals,  a, a, NULL);


		// read in remaining principals' addresses and figure out my principal
	//char host_name[MAXHOSTNAMELEN+1];
	/*	if (gethostname(host_name, MAXHOSTNAMELEN))
	 {
	 perror("Unable to get hostname");
	 exit(1);
	 }
	 */
	fprintf(stderr, "R_Node: hostname = %s\n", host_name);
	struct hostent *hent = gethostbyname(host_name);
	if (hent == 0)
	{
		th_fail("Could not get hostent");
	}
	struct in_addr my_address = *((in_addr*)hent->h_addr_list[0]);
	node_id = -1;

	principals = (R_Principal**)malloc(num_principals*sizeof(R_Principal*));

	Addr TR_TCP_a;
	bzero((char*)&TR_TCP_a, sizeof(TR_TCP_a));
	TR_TCP_a.sin_family = AF_INET;
	Addr TR_TCP_a_for_clients;
	bzero((char*)&TR_TCP_a_for_clients, sizeof(TR_TCP_a_for_clients));
	TR_TCP_a_for_clients.sin_family = AF_INET;
	short TR_TCP_port;
	short TR_TCP_port_for_clients;

	long int fconf_pos = ftell(config_file);
	char pk[1024];
	for (int i=0; i < num_principals; i++)
	{
		char hn[MAXHOSTNAMELEN+1];
		fscanf(config_file, "%256s %32s %hd %hd %1024s\n", hn, addr_buff,
				&TR_TCP_port, &TR_TCP_port_for_clients, pk);
		TR_TCP_a.sin_addr.s_addr = inet_addr(addr_buff);
		TR_TCP_a.sin_port = htons(TR_TCP_port);
		TR_TCP_a_for_clients.sin_addr.s_addr = inet_addr(addr_buff);
		TR_TCP_a_for_clients.sin_port = htons(TR_TCP_port_for_clients);

		if (my_address.s_addr == TR_TCP_a.sin_addr.s_addr && node_id == -1
				&& (req_port == 0 || req_port == TR_TCP_port))
		{
			node_id = i;
			fprintf(stderr, "We have parsed entry %s and assigned node_id = %d\n", hn, node_id);
		}
	}
	fseek(config_file, fconf_pos, SEEK_SET);
	for (int i=0; i < num_principals; i++)
	{
		char hn[MAXHOSTNAMELEN+1];
		fscanf(config_file, "%256s %32s %hd %hd %1024s \n", hn, addr_buff,
				&TR_TCP_port, &TR_TCP_port_for_clients, pk);
		TR_TCP_a.sin_addr.s_addr = inet_addr(addr_buff);
		TR_TCP_a.sin_port = htons(TR_TCP_port);
		TR_TCP_a_for_clients.sin_addr.s_addr = inet_addr(addr_buff);
		TR_TCP_a_for_clients.sin_port = htons(TR_TCP_port_for_clients);

		principals[i] = new R_Principal(i, num_principals, TR_TCP_a, TR_TCP_a_for_clients, pk);
		if (node_id == i)
		{
			R_my_principal = principals[i];
		}
	}

	if (node_id < 0)
	{
		th_fail("R_Node: could not find my principal");
	}

	for (int i=0; i<NUM_SCRATCHPADS; i++)
	    scratchpad[i] = (char*)malloc(R_Max_message_size);

	create_avi_auth_table(&avi_a);
	create_avi_verify_table(avi_a, &avi_v);

	sleep(2);

	// Compute new timestamp for cur_rid
	new_tstamp();
	pthread_mutex_init(&incoming_queue_mutex, NULL);
	pthread_cond_init(&not_empty_incoming_queue_cond, NULL) ;

}

R_Node::~R_Node()
{
	for (int i=0; i < num_principals; i++)
	{
		delete principals[i];
	}

	free(principals);
	for (int i=0; i<8; i++)
	    free(scratchpad[i]);
}

void R_Node::send(R_Message *m, int socket)
{
	int size = m->size();
	send_all(socket, m->contents(), &size);
}

R_Message* R_Node::recv(int socket, int *status)
{
	R_Message* m = new R_Message(R_Max_message_size);

	int err = recv_all_blocking(socket, (char *)m->contents(), sizeof(R_Message_rep), 0);

	if (err == -1) {
	    delete m;
	    if (status != NULL)
		*status = err;
	    return NULL;
	}
	if (err == 0) {
	    // this means recv returned 0, which is a signal for closed connection
	    delete m;
	    if (status != NULL)
		*status = err;
	    return NULL;
	}
	int payload_size = m->size() - sizeof(R_Message_rep);
	if (payload_size < 0) {
	    fprintf(stderr, "R_Node[%d]: weird packet received with size < R_Message_rep\n", id());
	    delete m;
	    if (status != NULL)
		*status = err;
	    return NULL;
	}
	//fprintf(stderr, "Waiting for payload %d for tag %d\n", payload_size, m->tag());
	err = recv_all_blocking(socket, m->contents() + sizeof(R_Message_rep),
			payload_size, 0);
	if (err == -1) {
	    delete m;
	    if (status != NULL)
		*status = err;
	    return NULL;
	}
	if (status != NULL)
	    *status = err;
	return m;
}

#define HIGHLY_EXPERIMENTAL_FEATURE
#ifdef HIGHLY_EXPERIMENTAL_FEATURE

void R_Node::gen_auth(R_BaseRequest *req, R_Rep_info* rr) const
{
    char *source = NULL;
    size_t source_len = 0;
    char *dest = NULL;

#ifdef USE_CLIENT_SIGNATURES
    // immediately process if client is asking for authentication
    if (!is_replica(id())) {
		gen_signature(req->toauth_begin(), req->toauth_size(), req->tosign_pos());
		return;
    }
#endif

    bool has_piggyback = false;

    int dist = avi_distance(req);

#ifdef TRACES
    //fprintf(stderr, "R_Node[%d]: gen_auth dist %d single %d , batched %d, pb %d, double pb %d\n", id(), dist, req->is_single(), req->is_batched(), req->is_piggybacked(), req->is_double_piggybacked());
    fprintf(stderr, "R_Node[%d]: gen_auth dist %d\n", id(), dist);
#endif

    // there is nothing attached: a single request
	{
		source = req->toauth_begin();
		source_len = req->toauth_size();
		dest = req->toauth_pos();

		// and exit immediatelly...
		gen_auth_core(source, source_len, dest, req, rr, false, 0);
		return;
    };
}

// has_piggyback is true if request has piggybacked data... if request is double-piggybacked, it will also be true
// pb_index is the value at which this pb data starts, for double-piggybacked it is index where this data starts
void R_Node::gen_auth_core(const char *s, int l, char *dest, R_BaseRequest* m, R_Rep_info* replies, bool has_piggyback, int pb_index, bool processing_dp) const
{
	int entry_replica = m->replica_id();

	long long unonce;
	unonce = id(); //R_Principal::new_umac_nonce();
	int dist = avi_distance(m);
	int dest_dist = min(f(), dist+1);

	if (is_replica(id()))
	    dest += dest_dist * (R_UNonce_size+(f()+1)*R_UMAC_size);
	char *sdest = dest;

	memcpy(dest, (char*)&unonce, R_UNonce_size);
	dest += R_UNonce_size;
	// we will put all MACs for the client at the END of the record
	char *for_client_dest = dest + f()*R_UMAC_size;

	//fprintf(stderr, "R_Node[%d]: gen_auth_single for pid %d, cid %d, entry_replica %d, unonce %lld\n", id(), id(),  cid, entry_replica, unonce);

	int edistance;
	edistance = 2*num_replicas-1 - avi_distance(m);

	int nb_MACs_for_replicas= min(f()+1, edistance);
	//int nb_MACs_for_replicas= f()+1;
	int start = (id() + 1)%num_replicas;
	if (!is_replica(id()))
	    start = entry_replica;
	for (int j = 0; j < nb_MACs_for_replicas; j++)
	{
#ifdef TRACES
		fprintf(stderr, "R_Node[%d]: generating MAC for pid %d, unonce %lld\n", id(), (start+j)%num_replicas,  unonce);
#endif
		R_my_principal->gen_mac(s, l, dest, (start + j)%num_replicas,
				(char*)&unonce);
		dest += R_UMAC_size;
	}

#ifdef TRACES
    const int to_remove = (R_UNonce_size + (f()+1)*R_UMAC_size);
	    fprintf(stderr, "R_Node[%d]: these are the macs I created:\n[", id());
	    for (int k=0; k<to_remove; k++)
		fprintf(stderr, "%02hhX ", sdest[k]);
	    fprintf(stderr, "]\n");
#endif

	if (nb_MACs_for_replicas < f() + 1)
	{
	}
#ifdef TRACES
	    fprintf(stderr, "R_Node[%d]: this is the input:\n[", id());
	    for (int k=0; k<l; k++)
		fprintf(stderr, "%02hhX ", s[k]);
	    fprintf(stderr, "]\n");
	    fprintf(stderr, "R_Node[%d]: these are the macs I created:\n[", id());
	    for (int k=0; k<to_remove; k++)
		fprintf(stderr, "%02hhX ", sdest[k]);
	    fprintf(stderr, "]\n");
#endif

#ifdef TRACES
	fprintf(stderr, "R_Replica[%d]: wrote this\n[", id());
	for (int i=0; i<(R_UNonce_size+(f()+1)*R_UMAC_size); i++)
	    fprintf(stderr, "%02hhX ", sdest[i]);
	fprintf(stderr, "]\n");
#endif
}

// generates auth for client only...
// ie. writes one MAC at the last position
void R_Node::gen_auth_for_client(R_BaseRequest *req, R_Rep_info *replies) const
{
    int cid = req->client_id();
    long long unonce;
    unonce = id(); //R_Principal::new_umac_nonce();
    R_Reply* rep = replies->reply(cid);
    R_Reply_rep* rr = (R_Reply_rep*)(rep?rep->contents():NULL);

    char *d = (char *)(&rr->digest);

    char *tmppos = req->toauth_pos();

    tmppos += f()*(R_UNonce_size+(f()+1)*R_UMAC_size);

    memcpy(tmppos, (char*)&unonce, R_UNonce_size);
    R_my_principal->gen_mac(d, sizeof(Digest), tmppos+R_UNonce_size+f()*R_UMAC_size, cid, (char*)&unonce);
#ifdef TRACES
    fprintf(stderr, "R_Node[%d]: (gafc) generating MAC for client %d, unonce %lld\n", id(), cid, unonce);
    fprintf(stderr, "R_Node[%d]: these are the macs I created:\n[", id());
    char *xxx_p = tmppos+R_UNonce_size+f()*R_UMAC_size;
    for (int k=0; k<R_UMAC_size; k++)
	fprintf(stderr, "%02hhX ", xxx_p[k]);
    fprintf(stderr, "]\n");
#endif
}

// generates auth for replica only...
// similar to gen_auth_for_client
// however, it writes the MAC at the first position, as it should be in the real request
void R_Node::gen_auth_ro(int replica_id, R_BaseRequest *req) const
{
    long long unonce;
    unonce = id(); //R_Principal::new_umac_nonce();

    char *tmppos = req->toauth_pos();

    memcpy(tmppos, (char*)&unonce, R_UNonce_size);
    R_my_principal->gen_mac(req->toauth_begin(), req->toauth_size(), tmppos+R_UNonce_size, replica_id, (char*)&unonce);
}

bool R_Node::verify_auth_ro(R_BaseRequest *req) const
{
    char *macs = req->toauth_pos();
    if (!principals[req->client_id()]->verify_mac(req->toauth_begin(), req->toauth_size(), macs+R_UNonce_size, macs))
    {
	fprintf(stderr, "R_Node[%d]: unable to verify mac from node %d, i=%d\n", id(), req->client_id(), 0);
	fprintf(stderr, "R_Node[%d]: this is the input:\n[", id());
	char *input = req->toauth_begin();
	for (int k=0; k<req->toauth_size(); k++)
	    fprintf(stderr, "%02hhX ", input[k]);
	fprintf(stderr, "]\n");
    const int to_remove = (R_UNonce_size + (f()+1)*R_UMAC_size);
	fprintf(stderr, "R_Node[%d]: these are the macs I inspected:\n[", id());
	for (int k=0; k<to_remove; k++)
	    fprintf(stderr, "%02hhX ", macs[k]);
	fprintf(stderr, "]\n");
	return false;
    }

    return true;
}

// request from client will always be verified against the actual request.
// for everything else, we use scratchpads.
bool R_Node::verify_auth(R_BaseRequest *req) const
{
    const int to_remove = (R_UNonce_size + (f()+1)*R_UMAC_size);

    avi_ventry aviv[(f()+1)];

    int entry_replica = req->replica_id();
    int dist = avi_distance(req);

#ifdef TRACES
    fprintf(stderr, "R_Node[%d]: verify_auth called, entry_replica %d, dist %d\n", id(), entry_replica, dist);
//    fprintf(stderr, "R_Node[%d]: single %d, batched %d, piggybacked %d, double pb %d\n", id(), req->is_single(), req->is_batched(), req->is_piggybacked(), req->is_double_piggybacked());
#endif
    char predecessor_generated_avi = AVI_A_AT(avi_a, dist-1, entry_replica, 0);
    char seen = 0;
    int different = 0;
    char *avi_v_begin = & AVI_V_AT(avi_v, dist, entry_replica, 0);
    // if preceeding in avi_a is H, then for first f+1 we need to nullify
    bool need_to_nullify = !((dist==0)||(AVI_A_AT(avi_a, dist-1, entry_replica, 0)!=AVI_H));

    char *ps[NUM_SCRATCHPADS];
    for (int i=0; i<NUM_SCRATCHPADS; i++)
		ps[i] = scratchpad[i];

    for (int i=0; i < f()+1; i++) {
		if (avi_v_begin[i] == AVI_CLIENT)
	    	continue;
		if (!(seen&avi_v_begin[i]))
	    	different++;
		seen = seen|avi_v_begin[i];
    }

    // number of MACs to check
    int nb_MACs = min(f()+1, dist);

    // if request is single, proceed directly to check
    char previous_avi = 0;
    {
		int pos_i = 0;
		for (int i=0; i < f()+1; i++) {
	    	if (avi_v_begin[i] == AVI_CLIENT)
				continue;

	    	if (previous_avi == avi_v_begin[i]) {
				aviv[pos_i] = aviv[pos_i-1];
				pos_i++;
				continue;
	    	}
	    	switch (avi_v_begin[i]) {
			case AVI_CLIENT: break;
			case AVI_h0: if (need_to_nullify && dist < f()+1) {
				aviv[pos_i].src = scratchpad[0];
			} else if (dist > f()) {
				Seqno os;
				if (predecessor_generated_avi == AVI_H || predecessor_generated_avi == AVI_T)
				    1; //req->nullify_seqno(&os);
				if (predecessor_generated_avi == AVI_T || predecessor_generated_avi == AVI_T0)
				    req->set_type(false);
				memcpy(ps[0], req->toauth_begin(), req->toauth_size());
				if (predecessor_generated_avi == AVI_H || predecessor_generated_avi == AVI_T)
				    1; //req->restore_seqno(os);
				if (predecessor_generated_avi == AVI_T || predecessor_generated_avi == AVI_T0)
				    req->set_type(true);
				aviv[pos_i].src = ps[0];
			} else {
				aviv[pos_i].src = req->toauth_begin();
			}
				 			 aviv[pos_i].len = req->toauth_size();
				 			 break;
			case AVI_H: if (dist < num_replicas) {
				aviv[pos_i].src = req->toauth_begin();
			} else {
				req->set_type(false);
				ps[1] = scratchpad[1];
				memcpy(ps[1], req->toauth_begin(), req->toauth_size());
				req->set_type(true);
				aviv[pos_i].src = ps[1];
			}
			    			 aviv[pos_i].len = req->toauth_size();
							 break;
			case AVI_T0: Seqno os;
			     		 if (predecessor_generated_avi == AVI_T)
				     		 1; //req->nullify_seqno(&os);
			     		 memcpy(ps[2], req->toauth_begin(), req->toauth_size());
			     		 if (predecessor_generated_avi == AVI_T)
				     		 1; //req->restore_seqno(os);
			     		 aviv[pos_i].src = ps[2];
			     		 aviv[pos_i].len = req->toauth_size();
			     		 break;
			case AVI_T: aviv[pos_i].src = req->toauth_begin();
			    		aviv[pos_i].len = req->toauth_size();
			    		break;
			default: break;
	    	}
	    	previous_avi = avi_v_begin[i];
	    	pos_i++;
		}
		int sdist = min(f()+1, dist);
		return verify_auth_core(sdist, entry_replica, dist<f(), nb_MACs, aviv, req->toauth_pos());
    }
}

// Doesn't do individual check in batched requests...
bool R_Node::verify_auth_core(int dist, int entry_replica, bool skip_client_mac, int how_many_to_check, avi_ventry* aviv, char *macs, bool same_level) const
{
#ifdef TRACES
    fprintf(stderr, "R_Node[%d]: vac: dist %d, how_many to check %d, entry_replica %d\n", id(), dist, how_many_to_check, entry_replica);
#endif
    const int to_remove = (R_UNonce_size + (f()+1)*R_UMAC_size);
    int p_id;
    if (is_replica(id()))
	p_id = (id()-dist+num_replicas)%num_replicas;
    else
	p_id = (entry_replica-dist+num_replicas)%num_replicas;

    char *bmacs = macs + (skip_client_mac?to_remove:0);
    for (int i=0; i < how_many_to_check; i++) {
	char *p_id_macs = bmacs+i*to_remove;
#ifdef TRACES
	fprintf(stderr, "R_Node[%d]: verifying MAC from %d\n", id(), p_id);
#endif
	int pdist = dist-i-1;
	if (unlikely(same_level))
	    pdist = dist-1;
	if (!principals[p_id]->verify_mac(aviv[i].src, aviv[i].len,
		    p_id_macs+R_UNonce_size+(pdist)*R_UMAC_size, p_id_macs)
		// this is not a real fix, but it works for f=3...
		&& !principals[p_id]->verify_mac(aviv[i].src, aviv[i].len,
		    p_id_macs+R_UNonce_size+(pdist+1)*R_UMAC_size, p_id_macs))
	{
#ifdef TRACES
	    fprintf(stderr, "R_Node[%d]: unable to verify mac from node %d, i=%d, pdist=%d\n", id(), p_id, i, pdist);
	    fprintf(stderr, "R_Node[%d]: this is the input:\n[", id());
	    for (int k=0; k<aviv[i].len; k++)
	    	fprintf(stderr, "%02hhX ", aviv[i].src[k]);
	    fprintf(stderr, "]\n");
	    fprintf(stderr, "R_Node[%d]: these are the macs I inspected:\n[", id());
	    for (int k=0; k<to_remove; k++)
	    	fprintf(stderr, "%02hhX ", p_id_macs[k]);
	    fprintf(stderr, "]\n");
#endif
	    return false;
	}
	p_id = (p_id+1)%num_replicas;
    }
    if (dist >= f()+1) {
	// this means we had MACs from all f+1 predecessors, so we need to remove first one
	memmove(macs, macs+to_remove, auth_size()-to_remove);
    }

    return true;
}

bool R_Node::verify_auth_at_client(int entry_replica, char *s, int len, char *macs, bool just_one) const
{
    const int to_remove = (R_UNonce_size + (f()+1)*R_UMAC_size);
    int p_id;

    p_id = (entry_replica-(f()+1)+num_replicas)%num_replicas;

    int num_macs = f()+1;
    char *bmacs = macs;
    if (unlikely(just_one)) {
	num_macs = 1;
	p_id = entry_replica;
	bmacs = macs + f()*to_remove;
    }
    for (int i=0; i < num_macs; i++) {
	char *p_id_macs = bmacs+i*to_remove;
#ifdef TRACES
	fprintf(stderr, "R_Node[%d]: verifying MAC from %d\n", id(), p_id);
#endif
	if (!principals[p_id]->verify_mac(s, len,
		    p_id_macs+R_UNonce_size+(f())*R_UMAC_size, p_id_macs))
	{
	    fprintf(stderr, "R_Node[%d]: unable to verify mac from node %d, i=%d\n", id(), p_id, i);
	    fprintf(stderr, "R_Node[%d]: this is the input:\n[", id());
	    for (int k=0; k<len; k++)
	    	fprintf(stderr, "%02hhX ", s[k]);
	    fprintf(stderr, "]\n");
	    fprintf(stderr, "R_Node[%d]: these are the macs I inspected:\n[", id());
	    for (int k=0; k<to_remove; k++)
	    	fprintf(stderr, "%02hhX ", p_id_macs[k]);
	    fprintf(stderr, "]\n");
	    return false;
	}
	p_id = (p_id+1)%num_replicas;
    }

    return true;
}

#endif

// verifies a message
// if R_Request is passed, this means it is first round
// if R_ACK is passed, it means second round.
// there is no third round anymore
// this takes into account batching in total:
// if everything in R_Request is R_Request, it is a simple batch
// if there is any R_ACK, that R_ACK is piggybacked
// NOTE: this R_ACK may be leaving soon, so check for it
// leaving: R_ACK was piggybacked, then carrying R_Request's got turned into R_ACK
//	    this won't happen here, but in v_m(R_ACK*,....)
// NOTE: both batching and piggybacking can be set

void R_Node::gen_signature(const char *src, unsigned src_len, char *sig) const
{
	bigint bsig = priv_key->sign(str(src, src_len));
	int size = mpz_rawsize(&bsig);
	if (size + sizeof(unsigned) > sig_size())
		th_fail("Signature is too big");

	memcpy(sig, (char*) &size, sizeof(unsigned));
	sig += sizeof(unsigned);

	mpz_get_raw(sig, size, &bsig);
}

unsigned R_Node::decrypt(char *src, unsigned src_len, char *dst,
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

Request_id R_Node::new_rid()
{
	if ((unsigned)cur_rid == (unsigned)0xffffffff)
	{
		new_tstamp();
	}
	return ++cur_rid;
}

void R_Node::new_tstamp()
{
	struct timeval t;
	gettimeofday(&t, 0);
	th_assert(sizeof(t.tv_sec) <= sizeof(int), "tv_sec is too big");
	Long tstamp = t.tv_sec;
	long int_bits = sizeof(int)*8;
	cur_rid = tstamp << int_bits;
}

