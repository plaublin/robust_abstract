#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#include <sys/param.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

#include "th_assert.h"
#include "libmodular_BFT.h"

#include "benchmarks.h"

#include "umac.h"
#include "sfslite/crypt.h"
#include <sfslite/rabin.h>

const int R_UMAC_size = 8;
const int R_UNonce_size = sizeof(long long);
const int R_MAR_size = R_UMAC_size + R_UNonce_size;

const int R_Nonce_size = 16;
const int R_Nonce_size_u = R_Nonce_size/sizeof(unsigned);
const int R_Key_size = 16;
const int R_Key_size_u = R_Key_size/sizeof(unsigned);

umac_ctx_t *ctxs;
rabin_pub **pubkeys;
rabin_priv **privkeys;
size_t sig_size;


void setup_mac(int num_nodes)
{
	umac_ctx_t *ctxs_t = (umac_ctx_t*)malloc(num_nodes*sizeof(umac_ctx_t*));
	ctxs = ctxs_t;

	for (int index=0; index < num_nodes; index++)
	{
		int temp = 1000 + index;

		unsigned k[R_Key_size_u];
		for (int j = 0; j<R_Key_size_u; j++)
		{
			k[j] = temp + j;
		}
		*ctxs_t = umac_new((char*)k);
		ctxs_t++;
	}
}

void setup_sig_rabin(int num_nodes)
{
		char pubk[] = "bfaa873efc926cb91646a89e45f96582041e3eed35cde0ef60b5c006cfad883781ee807411b0df3c74dc3ebbbce59c21d67711c83ecf596357c23dba33da338fb5577179a3b6188c59590aa1301eb852c0e14fa9225c0b377fee944eb9fa110ad7a316269e4b13b153887426a347c7c3c5feb1e3107bac4c6e29327b3343c405";
		char pk1[] = "d3bf9ada150474e93d21a4818ccf40e97df94f565c0528973a7799fc3e9ee69e0561fff15631850e2c5b8f9accee851cfc170cd0193052d4f75dfee18ab1d24b";
		char pk2[] = "e7b8885fa504355a686140181ae956e726e490ac2f905e52a78bea2ef16acef31788b827f35f0de1343766e6f2cbe44f436d7e5eceeb67791ccd296422fb50ef";

		bigint n1(pk1, 16);
	    bigint n2(pk2, 16);
	    bigint b(pubk,16);
	    sig_size = (mpz_sizeinbase2(&b) >> 3) + 1 + sizeof(unsigned);
	    sig_size += 2*sizeof(size_t);
	    if (sig_size < 256)
	    	sig_size = 256;

		pubkeys = (rabin_pub**)malloc(num_nodes*sizeof(rabin_pub*));
		privkeys = (rabin_priv**)malloc(num_nodes*sizeof(rabin_priv*));

		rabin_priv **privkey = privkeys;
		rabin_pub **pubkey = pubkeys;
		for (int i=0; i < num_nodes; i++)
		{
			privkey[i] = new rabin_priv(n1, n2);
			pubkey[i] = new rabin_pub(b);
		}
}

void do_mac_op(int node_index, size_t data_len, char *pos, char *dst) {
	umac_ctx_t ctx = ctxs[node_index];
	// Do not accept MACs sent with uninitialized keys.
	if (ctx == 0)
	{
		fprintf(stderr,
				"R_Principal::verify_mac: MACs sent with uninitialized keys\n");
		return ;
	}

	char unonce[R_UNonce_size];
	umac(ctx, pos, data_len, dst, (char *)unonce);
	umac_reset(ctx);
}

void do_sig_rabin_op_generate(int node_index, const char *src, size_t src_len, char *dst, size_t *dst_len)
{
	// This is rather inefficient if message is big but messages will
	// be small.
	bigint ctext = privkeys[node_index]->sign(str(src, src_len));
	size_t size = mpz_rawsize(&ctext);
	if (*dst_len < size+2*sizeof(size_t)) {
		fprintf(stderr, "Missmatch %zd %zd\n", *dst_len, (size_t)size+2*sizeof(size_t));
		return;
	}

	char *idest = dst;
	memcpy(dst, (char*)&src_len, sizeof(size_t));
	dst += sizeof(size_t);
	memcpy(dst, (char*)&size, sizeof(size_t));
	dst += sizeof(size_t);

	mpz_get_raw(dst, size, &ctext);
  	*dst_len = size+2*sizeof(size_t);
	//fprintf(stderr, "Source: [");
		//for (int k=0; k<src_len; k++)
		//fprintf(stderr, "%02hhX ", src[k]);
		//fprintf(stderr, "] => ");

		//fprintf(stderr, "I created %zd for %d:\n[", *dst_len, node_index);
		//for (int k=0; k<*dst_len; k++)
		//fprintf(stderr, "%02hhX ", idest[k]);
		//fprintf(stderr, "]\n");
	return;
}

void do_sig_rabin_op_verify(int node_index, const char *src, size_t src_len, char *sig)
{
  char *idest = sig;
  bigint bsig;
  int s_size = sig_size;
  sig += sizeof(size_t);
  memcpy((char*)&s_size, sig, sizeof(size_t));
  sig += sizeof(size_t);
  if (s_size+(int)sizeof(int) > sig_size) {
	  fprintf(stderr, "Nop x %d %d\n", s_size, sig_size);
	return ;
  }

	//fprintf(stderr, "Source: [");
		//for (int k=0; k<src_len; k++)
		//fprintf(stderr, "%02hhX ", src[k]);
		//fprintf(stderr, "] => ");

		//fprintf(stderr, "Checked %zd for node %d:\n[", (size_t)s_size+2*sizeof(size_t), node_index);
		//for (int k=0; k<s_size+2*sizeof(size_t); k++)
		//fprintf(stderr, "%02hhX ", idest[k]);
		//fprintf(stderr, "]\n");

  mpz_set_raw(&bsig, sig, s_size);  
  if (!pubkeys[node_index]->verify(str(src, src_len), bsig))
  {
	  //fprintf(stderr, "Nop %d: %d %d\n", node_index, s_size, sig_size);
  } else {
		//fprintf(stderr, "OK\n");
  }


  return;
}

int main(int argc, char **argv)
{
	double diff_time;
	const int NUM_ITERATIONS = 100000;
	struct timeval t0, t1;

	const int NUM_NODES = 10000;
	const int SLACK = 10;

	setup_mac(NUM_NODES);
	setup_sig_rabin(NUM_NODES);

	int data_len = sizeof(uint32_t)+sizeof(Seqno)+4*sizeof(unsigned int);
	if (argc > 1) {
		data_len = atoi(argv[1]);
	}

	char *scratchpad = (char*)malloc(NUM_NODES*data_len*SLACK);

	for (int i = 0; i<NUM_NODES*data_len*SLACK; i++)
		scratchpad[i] = rand()%255;

	char *sigs = (char*)malloc(NUM_NODES*sig_size*SLACK);
	int *ids = (int*)malloc(sizeof(int)*NUM_NODES*SLACK);
	for (int i=0; i < NUM_NODES*SLACK; i++)
		ids[i] = -1;

	gettimeofday(&t0, 0);
	for (int i=0; i < NUM_ITERATIONS; i++) {
		int id = rand()%NUM_NODES;
		int slack = rand()%(NUM_NODES*SLACK);
		do_mac_op(id, data_len, &scratchpad[(slack)*data_len], &sigs[(slack)*sig_size]);
	}
	gettimeofday(&t1,NULL);
	diff_time = (t1.tv_sec-t0.tv_sec)*1e6+(t1.tv_usec-t0.tv_usec);
	fprintf(stderr, "MAC: [%d] %10gus %10gus\n", data_len, diff_time, diff_time/NUM_ITERATIONS);

	gettimeofday(&t0, 0);
	for (int i=0; i < NUM_ITERATIONS; i++) {
		int id = rand()%NUM_NODES;
		int slack = rand()%(NUM_NODES*SLACK);
		size_t dl = sig_size;
		ids[slack] = id;
		//fprintf(stderr, "sig %d => %d\n", id, slack);
		do_sig_rabin_op_generate(id, &scratchpad[slack*data_len], data_len, &sigs[slack*sig_size], &dl);
	}
	gettimeofday(&t1,NULL);
	diff_time = (t1.tv_sec-t0.tv_sec)*1e6+(t1.tv_usec-t0.tv_usec);
	fprintf(stderr, "SIG_GEN: [%d] %10gus %10gus\n", data_len, diff_time, diff_time/NUM_ITERATIONS);

	size_t count = 0;
	gettimeofday(&t0, 0);
	for (int i=0; i < NUM_ITERATIONS; i++) {
		int slack = rand()%(NUM_NODES*SLACK);
		if (ids[slack] == -1)
			continue;
		do_sig_rabin_op_verify(ids[slack], &scratchpad[slack*data_len], data_len, &sigs[slack*sig_size]);
		count++;
	}
	gettimeofday(&t1,NULL);
	diff_time = (t1.tv_sec-t0.tv_sec)*1e6+(t1.tv_usec-t0.tv_usec);
	fprintf(stderr, "SIG_VERIFY: [%d] %10gus %10gus /%d\n", data_len, diff_time, diff_time/count, count);
}
