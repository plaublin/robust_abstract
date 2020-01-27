#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>

#include "libmodular_BFT.h"
#include "zlight_libbyz.h"
#include "quorum_libbyz.h"
#include "chain_libbyz.h"
#include "ring_libbyz.h"
#include "PBFT_R_pbft_libbyz.h"
#include "PBFT_C_pbft_libbyz.h"
#include "aardvark_libbyz.h"

#include "sfslite/crypt.h"

#include "Switcher.h"
#include "RobustMonitor.h"

//#define TRACES

#undef RUN_CHAIN
#undef RUN_RING
#undef RUN_QUORUM
#undef RUN_ZLIGHT
#undef RUN_PBFT
#undef RUN_AARDVARK

#include "libmodular_BFT_choice.h"

#if defined(RUN_PBFT) && defined(RUN_AARDVARK)
#error "You must define either PBFT or Aardvark, but not both"
#endif

#if !defined(RUN_CHAIN) && !defined(RUN_RING) && !defined(RUN_QUORUM) && !defined(RUN_ZLIGHT) && !defined(RUN_PBFT) && !defined(RUN_AARDVARK)
#error "You must define at least one protocol"
#endif

#if 0
#ifdef RUN_RING
#ifdef RUN_CHAIN
#warning "RUN_CHAIN defined while RUN_RING is defined. undefining it"
#undef RUN_CHAIN
#endif
enum protocols_e current_protocol = ring;
enum protocols_e next_protocol_instance = ring;
#endif
#ifdef RUN_CHAIN
enum protocols_e current_protocol = chain;
enum protocols_e next_protocol_instance = chain;
#endif

#if defined(RUN_PBFT) && !defined(RUN_CHAIN) && !defined(RUN_RING) && !defined(RUN_QUORUM) && !defined(RUN_ZLIGHT)
enum protocols_e current_protocol = pbft;
enum protocols_e next_protocol_instance = pbft;
#endif

#if !defined(RUN_CHAIN) && !defined(RUN_RING)
#if defined(RUN_QUORUM)
enum protocols_e current_protocol = quorum;
enum protocols_e next_protocol_instance = pbft;
#endif
#if defined(RUN_ZLIGHT)
enum protocols_e current_protocol = zlight;
enum protocols_e next_protocol_instance = pbft;
#endif

//#define RUN_PBFT
#endif
#endif

#if 0
#ifdef RUN_PBFT
#ifdef RUN_ZLIGHT
enum protocols_e current_protocol = zlight;
#endif
#ifdef RUN_QUORUM
enum protocols_e current_protocol = quorum;
#endif
enum protocols_e next_protocol_instance = pbft;
#endif
#endif

enum protocols_e current_protocol = chain;
enum protocols_e next_protocol_instance = pbft;

int switching_number = 0;

Switcher *great_switcher = NULL;
RobustMonitor *robust_monitor = NULL;

int (*exec_command)(Byz_req*, Byz_rep*, Byz_buffer*, int, bool);

// Service specific functions.
int _exec_command(Byz_req *inb, Byz_rep *outb, Byz_buffer *non_det, int client,
		bool ro)
{
	//		fprintf(stderr, "Client %d: [%c %c %c %c]\n", client, inb->contents[0], inb->contents[1],
	//				inb->contents[2], inb->contents[3]);
	if (inb->contents[0] == 'z' && inb->contents[1] == 'o' && inb->contents[2]
			== 'r' && inb->contents[3] == 'r' && inb->contents[4] == 'o')
	{
		fprintf(stderr, "This is a maintenance message from client %d\n", client);
	}
	exec_command(inb, outb, non_det, client, ro);
	return 0;
}

int nb_checkpoints = 0;
// Service specific functions.
int perform_checkpoint()
{
	nb_checkpoints++;
	if (nb_checkpoints % 100 == 0)
	{
		fprintf(stderr, "Perform checkpoint %d\n", nb_checkpoints);

		// Allocate request
		Byz_req req;
		if (PBFT_C_pbft_alloc_request(&req) != 0)
		{
			fprintf(stderr, "allocation failed");
			exit(-1);
		}
		//		fprintf(stderr, "request size = %d\n", req.size);
		req.size = 4096;

		req.contents[0] = 'z';
		req.contents[1] = 'o';
		req.contents[2] = 'r';
		req.contents[3] = 'r';
		req.contents[4] = 'o';

		Byz_rep rep;

		PBFT_C_pbft_invoke(&req, &rep, false);
		PBFT_C_pbft_free_reply(&rep);
		PBFT_C_pbft_free_request(&req);

	}
	return 0;
}

int MBFT_alloc_request(Byz_req *req)
{
  switch (current_protocol)
  {
  case quorum:
  {
    return quorum_alloc_request(req);
  }
  break;
  case ring:
  {
    return ring_alloc_request(req);
  }
  break;
  case chain:
  {
    return chain_alloc_request(req);
  }
  break;
  case pbft:
  {
    return PBFT_R_pbft_alloc_request(req);
  }
  break;
  case zlight:
  {
    return zlight_alloc_request(req);
  }
  break;
  case aardvark:
  {
    return aardvark_alloc_request(req);
  }
  break;
  default:
  {
    fprintf(stderr, "MBFT_alloc_request: Unknown protocol\n");
  }
  break;
  }

	return 0;
}

void MBFT_free_request(Byz_req *req)
{
	switch (current_protocol)
	{
		case quorum:
		{
			quorum_free_request(req);
		}
			break;
		case ring:
		{
			ring_free_request(req);
		}
			break;
		case chain:
		{
			chain_free_request(req);
		}
			break;
		case pbft:
		{
			PBFT_R_pbft_free_request(req);
		}
			break;
		case zlight:
		{
			zlight_free_request(req);
		}
			break;
    case aardvark:
    {
      aardvark_free_request(req);
    }
      break;
		default:
		{
			fprintf(stderr, "MBFT_free_request: Unknown protocol\n");
		}
			break;
	}
}

void MBFT_free_reply(Byz_rep *rep)
{
	switch (current_protocol)
	{
		case quorum:
		{
			quorum_free_reply(rep);
		}
			break;
		case ring:
		{
			ring_free_reply(rep);
		}
			break;
		case chain:
		{
			chain_free_reply(rep);
		}
			break;
		case pbft:
		{
			PBFT_R_pbft_free_reply(rep);
		}
			break;
		case zlight:
		{
			zlight_free_reply(rep);
		}
        break;
        case aardvark:
        {
            aardvark_free_reply(rep);
        }
        break;
		default:
		{
			fprintf(stderr, "MBFT_free_reply: Unknown protocol\n");
		}
			break;
	}
}

int MBFT_init_replica(char *host_name, char *conf_quorum, char *conf_pbft,
		char *conf_priv_pbft, char *conf_chain, char *conf_zlight, char *mem,
		unsigned int mem_size, int(*exec)(
				Byz_req*, Byz_rep*, Byz_buffer*, int, bool), short port,
		short port_pbft, short port_chain, short port_zlight)
{
    fprintf(stderr, "MBFT_init_replica called\n");
    great_switcher = new Switcher();
    robust_monitor = new RobustMonitor((const char*)conf_quorum, (const char*)host_name, (const short)port);

    exec_command = exec;

#ifdef RUN_QUORUM
	quorum_init_replica(conf_quorum, conf_priv_pbft, host_name, _exec_command, perform_checkpoint, port);
#endif
#ifdef RUN_ZLIGHT
	zlight_init_replica(conf_zlight, conf_priv_pbft, host_name, _exec_command, perform_checkpoint, port_zlight);
#endif
#ifdef RUN_CHAIN
	chain_init_replica(host_name, conf_chain, conf_priv_pbft, _exec_command, port_chain);
#endif
#ifdef RUN_RING
	ring_init_replica(host_name, conf_chain, conf_priv_pbft, _exec_command, port_chain);
#endif
#ifdef RUN_AARDVARK
  int byz_pre_prepare_delay = 0;
  int exec_command_delay = 0;
  int delay_every_p_pp = 1;
  int sliding_window_size = 0;
	aardvark_init_replica(conf_pbft, conf_priv_pbft, mem, mem_size, _exec_command, 0, 0, byz_pre_prepare_delay, delay_every_p_pp, exec_command_delay, sliding_window_size);
    if (current_protocol == aardvark) {
        great_switcher->switch_state(current_protocol, true);
    }
    robust_monitor->switch_protocol(current_protocol);
    Byz_replica_run();
#endif
#ifdef RUN_PBFT
	PBFT_R_pbft_init_replica(conf_pbft, conf_priv_pbft, host_name, mem, mem_size, _exec_command, 0, 0);
	robust_monitor->switch_protocol(current_protocol);
	PBFT_R_pbft_replica_run();
#else
	robust_monitor->switch_protocol(current_protocol);
#endif

	fprintf(stderr, "PBFT_R_Replica is initialized\n");
	//PBFT_C_pbft_init_client(conf_pbft, conf_priv_pbft, 3000);
	while (1)
	{
		sleep(100);
	}
	return 0;
}

int MBFT_init_client(char *host_name, char *conf_quorum, char *conf_pbft,
		char *conf_chain, char *conf_zlight, char *conf_priv_pbft, short port_quorum,
		short port_pbft, short port_chain, short port_zlight)
{
#ifdef RUN_QUORUM
	quorum_init_client(conf_quorum, host_name, port_quorum);
#endif
#ifdef RUN_ZLIGHT
	zlight_init_client(conf_zlight, host_name, port_zlight);
#endif
#ifdef RUN_CHAIN
	chain_init_client(host_name, conf_chain, port_chain);
#endif
#ifdef RUN_RING
	ring_init_client(host_name, conf_chain, conf_priv_pbft, port_chain);
#endif
#ifdef RUN_PBFT
	PBFT_R_pbft_init_client(conf_pbft, conf_priv_pbft, host_name, port_pbft);
#endif
#ifdef RUN_AARDVARK
	aardvark_init_client(conf_pbft, conf_priv_pbft, port_pbft);
#endif
	return 0;
}

int MBFT_close_client()
{
#ifdef RUN_QUORUM
#endif
#ifdef RUN_ZLIGHT
#endif
#ifdef RUN_CHAIN
#endif
#ifdef RUN_RING
	ring_close_client();
#endif
#ifdef RUN_PBFT
#endif
#ifdef RUN_AARDVARK
#endif
	return 0;
}

void MBFT_set_malicious_client(bool be_malicious)
{
#ifdef RUN_QUORUM
#endif
#ifdef RUN_ZLIGHT
#endif
#ifdef RUN_CHAIN
#endif
#ifdef RUN_RING
	ring_set_malicious(be_malicious);
#endif
#ifdef RUN_PBFT
#endif
#ifdef RUN_AARDVARK
#endif
}

int MBFT_invoke(Byz_req *req, Byz_rep *rep, int size, bool ro)
{
	int retval = 0;
	switch (current_protocol)
	{
		case quorum:
		{
			retval = quorum_invoke(req, rep, size, ro);
		}
			break;
		case ring:
		{
			retval = ring_invoke(req, rep, size, ro);
		}
			break;
		case chain:
		{
			retval = chain_invoke(req, rep, size, ro);
		}
			break;
		case pbft:
		{
			req->size = size;
			retval = PBFT_R_pbft_invoke(req, rep, ro);
		}
			break;
		case zlight:
		{
			retval = zlight_invoke(req, rep, size, ro);
		}
			break;
    case aardvark:
    {
      req->size = size;
      retval = aardvark_invoke(req, rep, ro, false);
    }
      break;
		default:
		{
			fprintf(stderr, "MBFT_invoke: unknown protocol\n");
		}
			break;
	}
	if (retval == -127) {
	    // time to switch to another protocol
	    fprintf(stderr, "MBFT_invoke: will switch to %d\n", next_protocol_instance);
	    MBFT_free_request(req);
	    MBFT_free_reply(rep);
	    //req = NULL;
	    //rep = NULL;
	    current_protocol = next_protocol_instance;
	}
	return retval;
}
