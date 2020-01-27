#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/param.h>
#include <unistd.h>
#include <time.h>

//ADDED.ALI
#define OWNER 1
extern "C"
{
#include "ldap_search.h"
}

#include "Traces.h"
#include "libmodular_BFT.h"

#include "benchmarks.h"

using  namespace std;
int req_order=0;
int reply_size;
long sleep_time_ns;

// Service specific functions.
/*
   This function parses request message recieved from BFTclient in *inb and calls the
   fucntion do_ldap_search that stands as an ldap API client  to request from LDAP server.
   The result resturned from the LDAP server is a single string composed of
   entries(attributes and values)
For now we just pass all the values in one string and load into *outb, in its way to BFT Clients again.
*/

/*
int ROUNDS=0;
int ip_switch=0;
char ips_first[]="127.0.0.1";
char ips_sec[]="127.0.1.1";	
*/

int exec_command(Byz_req *inb, Byz_rep *outb, Byz_buffer *non_det, int client,
		bool ro)
{

#define USE_LDAP
#ifdef USE_LDAP
	// ADDED.ALI
	char *base,*filter,*LDAP_result;
	const char delimiters[] = "$";
	char *token,*request;
	int args_nb,rc;

// THe problem before ws that TCP sockets are being used all 
// and hence we used the NIC aliases
// these lines commented out afteer using unix sockets on LDAP
	/*if(!((ROUNDS++)%10000))
	{
	ip_switch=(ip_switch+1)%2;
	if(ip_switch)
		ldap_host=ips_first;
	else ldap_host=ips_sec;
	fprintf(stderr,"%s\n",ldap_host);
	}
*/

	//fprintf(stderr,"%d",req_order++);

	//fprintf(stderr, "replica_main.cc: We are executing a request.\
	//The client is %d, size %d\n", client, reply_size);
	// Parsing request message into tokens in var

	if(inb->contents==NULL || inb->size<0)
	{
		fprintf( stderr,"replica_min.cc:exec_command wrong request message; exiting ... \n");
		return -1;
	}
	
	request=strdup(inb->contents);//hard copy
	token = strtok(request, delimiters);//first token parse
	args_nb=0;
	
	if(token!=NULL)
	{
		//base=(char*)malloc(strlen(token)*sizeof(char)+1);
		//strcpy(base,token);
		base = request;
		//args_nb++;
	}

	token = strtok(NULL, delimiters);//get new token
	if(token!=NULL)
	{
		//filter=(char*)malloc(strlen(token)*sizeof(char)+1);
		//strcpy(filter,token);
		//args_nb++;
		filter = token;
	}
	
	if(base==NULL || filter==NULL)	
	{
		fprintf(stderr,"replica_main.cc: error in exec_command; wrong command...");
		return -1;
	}
	//fprintf(stderr,"\n This is the LDAPRequest:[%s] [%s] (%d,%d)\n",base,filter,strlen(base),strlen(filter));
	//fprintf(stderr, "The last char is %02hhX\n", filter[strlen(filter)-1]);
	//call LDAP Tool ldapsearch
	LDAP_result=NULL;
	rc=do_ldap_search(base,filter,&LDAP_result);
//	free(request);	
	if(rc!= EXIT_SUCCESS )
	{
		fprintf( stderr,"replica_main:exec_command error calling ldapsearch_main, exiting ... \n");
		return -1;
	}

	//fprintf(stderr,"\n This is the LDAPresult:\n[%s][%d]\n",LDAP_result, strlen(LDAP_result));
	// Adding  result to BFTClient

	bzero(outb->contents, reply_size);
	if (reply_size < strlen(LDAP_result))
	{
	//	fprintf(stderr,"replica_main:exec_command reply size does not fit ..");
	//	return -1;
	}

	memcpy(outb->contents, LDAP_result, strlen(LDAP_result)+1);
	//memcpy(outb->contents, LDAP_result, reply_size);
	outb->size = reply_size;
	//free(base);
	//free(filter);
	free(LDAP_result);
	free(request);
	/*
	   In this implementation we are just considering the string 
	   result returned by LDAP server, other result types might 
	   be added later
	   */
#endif
	// now, it is time to sleep.
	if (sleep_time_ns != 0) 
	{
		struct timespec tts;
		tts.tv_sec = 0;
		tts.tv_nsec = sleep_time_ns;

		nanosleep(&tts, NULL);
	}
	
//	fprintf(stderr,"%d",req_order--);
	//fprintf(stderr, "Created ldap answer\n");
	return 0;
}

int main(int argc, char **argv)
{
	short port_quorum;
	short port_pbft;
	short port_chain;
	char config_quorum[PATH_MAX];
	char config_pbft[PATH_MAX];
	char config_priv_pbft[PATH_MAX];
	char config_priv_tmp_pbft[PATH_MAX];
	char config_chain[PATH_MAX];
	char host_name[MAXHOSTNAMELEN+1];
	int argNb = 1;
	ssize_t init_history_size = 0;
	int percent_misses = 0;
	strcpy(host_name, argv[argNb++]);
	port_quorum = atoi(argv[argNb++]);
	port_pbft = atoi(argv[argNb++]);
	port_chain = atoi(argv[argNb++]);
	reply_size = atoi(argv[argNb++]);
	sleep_time_ns = atol(argv[argNb++]);
	strcpy(config_quorum, argv[argNb++]);
	strcpy(config_pbft, argv[argNb++]);
	strcpy(config_priv_tmp_pbft, argv[argNb++]);
	strcpy(config_chain, argv[argNb++]);
	init_history_size = atoi(argv[argNb++]);
	percent_misses= atoi(argv[argNb++]);

	// Priting parameters
	fprintf(stderr, "********************************************\n");
	fprintf(stderr, "*             Replica parameters           *\n");
	fprintf(stderr, "********************************************\n");
	fprintf(
			stderr,
			"Host name = %s\nPort_quorum = %d\nPort_pbft = %d\nPort_chain = %d\nReply size = %d \nConfiguration_quorum file = %s\nConfiguration_chain file = %s\nConfiguration_pbft file = %s\nConfig_private_pbft directory = %s\n",
			host_name, port_quorum, port_pbft, port_chain, reply_size,
			config_quorum, config_chain, config_pbft, config_priv_tmp_pbft);
	fprintf(stderr, "********************************************\n\n");

	char hname[MAXHOSTNAMELEN];
	gethostname(hname, MAXHOSTNAMELEN);
	// Try to open default file
	sprintf(config_priv_pbft, "%s/%s", config_priv_tmp_pbft, hname);

	int mem_size = 2048 * 8192;
	char *mem = (char*) valloc(mem_size);
	bzero(mem, mem_size);

	MBFT_init_replica(host_name, config_quorum, config_pbft,
			config_priv_pbft, config_chain, config_quorum, mem, mem_size, exec_command,
			port_quorum, port_pbft, port_chain, port_quorum);
}

