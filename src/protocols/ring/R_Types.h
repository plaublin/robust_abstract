#ifndef _R_RING_TYPES_H_
#define _R_RING_TYPES_H_
#include "types.h"

#include <utility>

class R_Request;

typedef std::pair<int, Request_id> R_Message_Id;

enum R_State { 
    state_pending = 0, // we just got the request
    state_exec_not_acked = 1, // request is sequenced and executed, yet no ack is received
    state_acked_not_exec = 2, // request is acked, but not executed...
    state_acked = 4, // ack is received, and the request is executed...
    state_stable = 16 // this means the request is executed and forwarded, hence it can be removed from the log
	// XXX: in real implementation, this should happen only after we get the notification that the client received the request, or that the others have received it...
};

typedef struct {
    R_Request *req;
    int pos; // denotes the position in the batched requests;
    enum R_State state;
    unsigned int refcount; // denotes how many other RMS structures point to this one
    // structures point to this one is they are the part of the same batched request
    // RMS can be deleted only if refcount is 0;
    // req can be deleted only if pos == 0, and refcount == 0
    R_Message_Id rmid; // this is used to identify the main structure
    R_Message_Id my_rmid; // this is used to identify this struct (pointer to self). just for speed...
    char pad[24];
} R_Message_State;


#endif
