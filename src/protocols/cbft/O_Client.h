#ifndef _Q_Client_h
#define _Q_Client_h 1

#include <stdio.h>
#include "types.h"
#include "O_Node.h"
#include "O_Request.h"
#include "O_Reply.h"
#include "O_Certificate.h"
#include "O_ITimer.h"

//Added by Maysam Yabandeh
#include <vector>

extern void O_rtimeQ_handler();

class O_Client: public O_Node {
public:
	O_Client(FILE *config_file, char *host_name, short port = 0);
	// Effects: Creates a new Client object using the information in
	// "config_file". The line of config assigned to
	// this client is the first one with the right host address (if
	// port==0) or the first with the right host address and port equal
	// to "port".

	virtual ~O_Client();
	// Effects: Deallocates all storage associated with this.

	bool send_request(O_Request *req, int size, bool ro);
	// Effects: Sends request m to the primary. Returns FALSE iff two
	// consecutive request were made without waiting for a reply between
	// them.

	bool send_ordered_request(O_Request *req, int size, bool ro);
	// Effects: Sends the ordered request received from the primary to 
	// the other replicas. Returns FALSE iff two consecutive request were
	// made without waiting for a reply between them.

	O_Reply *recv_reply();
	// Effects: Blocks until it receives enough reply messages for
	// the previous request. returns a pointer to the reply. The caller is
	// responsible for deallocating the request and reply messages.

	void retransmit();
	// Effects: Retransmits any outstanding request

private:
	Request_id out_rid; // Identifier of the outstanding request
	O_Request *out_req; // Outstanding request

	int n_retrans;        // Number of retransmissions of out_req
	int rtimeout;         // PBFT_R_Timeout period in msecs
	// Maximum retransmission timeout in msecs
	static const int Max_rtimeout = 1000;
	// Minimum retransmission timeout after retransmission 
	// in msecs
	static const int Min_rtimeout = 10;
	O_ITimer *rtimer;       // Retransmission timer

	O_Certificate<O_Reply> r_reps; // Certificate with application replies
	bool is_panicking;
	bool bail_out;

	//Added by Maysam Yabandeh
	static bool recievedOrder;
	static int order;
	static int lastRequestSize;
   //TODO 100 -> num_replicas
	bool  responders[100];
	void processReceivedMessage(O_Message* m, int sock);
};

#endif // _Q_Client_h
