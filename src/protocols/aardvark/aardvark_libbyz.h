#ifndef _A_LIBBYZ_H
#define _A_LIBBYZ_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __cplusplus
typedef unsigned long bool; 
#endif

/* Because of FILE parameter */
#include <stdio.h>

#include "libbyz.h"
#include "types.h"
#include "A_Modify.h"
  //#include "Digest.h"
#include "A_State_defs.h"


/*
 * A_Client
 */

int aardvark_init_client(char *conf, char *conf_priv, short port);
/* Effects: Initializes a libbyz client process using the information in the file 
   named by "conf" and the private key in the file named by "conf_priv". 
   If port is 0 the library will select the first line matching this
   host in "conf". Otherwise, it selects the line with port value "port". */

int aardvark_alloc_request(Byz_req *req);
/* Requires: "req" points to a Byz_req structure
   Effects: If successful returns 0 and initializes "req" by allocating internal 
   memory for the request, making "req->contents" point to that memory, and "req->size" 
   contain the number of bytes that can be used starting from "req->contents". If it fails
   it returns -1. */

void aardvark_free_request(Byz_req *req);
/* Requires: "req" points to a Byz_req structure whose "req->contents" value
   was obtained by calling aardvark_alloc_req.
   Effects: Frees the internal memory associated with "req". */

void aardvark_free_reply(Byz_rep *rep);
/* Requires: "rep" points to a Byz_rep structure whose "req->contents" value
   was obtained by calling aardvark_recv_reply.
   Effects: Frees the internal memory associated with "rep". */

  int aardvark_send_request(Byz_req *req, bool ro, bool faultyClient=false);
/* Requires: "req" points to a Byz_req structure whose "req->contents"
   value was obtained by calling aardvark_alloc_req and whose "req->size"
   value is the actual number of bytes in the request. 
   "read_only" is true iff the request
   does not modify the service state. All previous request have been
   followed by an invocation of aardvark_recv_reply.

   Effects: Invokes the request. If successful, returns 0.
   Otherwise returns -1. */

long long aardvark_recv_reply(Byz_rep *rep);
/* Requires: "rep" points to an uninitialized Byz_rep structure.
   There was a previous request for which there was not an invocation
   of aardvark_recv_reply.
   
   If successful, initializes "rep" to
   point to the reply and returns 0. ("rep" must be deallocated by the
   caller using aardvark_free_reply.) Otherwise, does not initialize "rep"
   and returns -1. */

  int aardvark_invoke(Byz_req *req, Byz_rep *rep, bool ro, bool faultyClient=false);
/* Requires: "req" points to a Byz_req structure whose "req->contents"
   value was obtained by calling aardvark_alloc_req and whose "req->size"
   value is the actual number of bytes in the request. 
   "read_only" is true iff the request
   does not modify the service state. All previous request have been
   followed by an invocation of aardvark_recv_reply.
   "rep" points to an uninitialized Byz_rep structure.

   Effects: Invokes the request. If successful, initializes "rep" to
   point to the reply and returns 0. ("rep" must be deallocated by the
   caller using aardvark_free_reply.) Otherwise, does not initialize "rep"
   and returns -1. */

  void aardvark_flood_replicas();
  /* if called, then the client will flood the replicas forever */

  // recv reply. If there is no reply, wait for at most timeout usec.
  // Used to empty the TCP buffer.
  // return the number of received messages
  int aardvark_recv_reply_noblock(long timeout);

/* OPEN LOOP */

// send the request req
int aardvark_open_loop_send_request(Byz_req *req, bool ro, bool faultyClient);

// Recv a reply. Return the view number. Set *rid to the id of the received reply.
int aardvark_open_loop_recv_reply(unsigned long long *rid, Byz_rep *rep);


/*
 * A_Replica 
 */

// flag to determine if the primary should corrupt client macs
//  bool _corruptClientMAC;
void setCorruptClientMAC(bool b);
bool corruptClientMAC();

int aardvark_init_replica(char *conf, char *conf_priv, char *mem, unsigned int size,
    int(*exec)(Byz_req *, Byz_rep *, Byz_buffer *, int, bool, long int),
    void(*comp_ndet)(Seqno, Byz_buffer *), int ndet_max_len,
    long int byz_pre_prepare_delay, long int delay_every_p_pp, long int exec_command_delay, int sliding_window_size);
/*
 * sliding_window_size is the size of the sliding window (in number of checkpoints), when using E-Aardvark.
 * If set to 0, then compute the expectations since the start of the view.
*/
/* Requires: "mem" is vm page aligned and "size" is a multiple of the vm page size.

   Effects: Initializes a libbyz A_replica process using the information in the file 
   named by "conf" and the private key in the file named by "conf_priv". 
   The state managed by the A_replica is set to the "size" contiguous bytes starting
   at "mem", and the A_replica will call the "exec" upcall to execute requests and 
   the "comp_ndet" upcall to compute non-deterministic choices for each request.
   "ndet_max_len" must be the maximum number of bytes comp_ndet places in its argument
   buffer. The replication code uses the begining of "mem" to store protocol data.
   If successful, the function returns the number of bytes used which is guaranteed 
   to be a multiple of the vm page size. Otherwise, the function returns -1.

   The specs for the upcalls are:
   int exec(Byz_req *req, Byz_rep *rep, Byz_buffer *ndet, int client, bool read_only);

   Effects: 
   - "req->contents" is a character array with a request with
   "req->size" bytes
   
   - "rep->contents" is a character array where exec should place the
   reply to the request. This reply cannot excede the value of  
   "rep->size" on entry to the exec. On exit from exec, "rep->size"
   must contain the actual number of bytes in the reply.

   - "ndet->contents" is a character array with non-deterministic
   choices associated with the request and is "ndet->size" bytes long

   - "client" is the identifier of the client that executed the
   request (index of client's public key in configuration file)

   - "read_only" is true iff the request should execute only if it does
   not modify the A_replica's state.
   
   If "read_only" is true "exec" should not execute the request in
   "req" unless it is in fact read only. If the request is not read
   only it should return -1 without modifying the service
   state. Except for this case exec should execute the request in req
   using the non-deterministic choices and place the replies in
   rep. The execution of the request will typically require access
   control checks using the client identifier. If the request executes
   successfully exec should return 0.
   

   void comp_ndet(Seqno seqno, Byz_buffer *ndet);
   Effects: "ndet->contents" is a character array where comp_ndet
   should place the non-deterministic choices (e.g., time) associated
   with the request with sequence number seqno. These choices cannot
   excede the value of "ndet->size" on entry to the comp_ndet. On exit
   from comp_ndet, "ndet->size" must contain the actual number of
   bytes in the choices.

*/

/*
 * The service code should call one of the following functions before
 * it modifies the state managed by the A_replica.
 *
 */

void aardvark_modify(char *mem, int size);
/* Requires: "mem" and "mem+size-1" are within the A_replica's state.
   Effects: Informs library that the bytes between "mem" and
   "mem+size" are about to be modified. */

#define aardvark_modify1(mem) _A_aardvark_modify1(mem)
/* void aardvark_modify1(char *mem, unsigned int size);
   Requires: "mem" is within the A_replica's state and modified memory
   can not span more than one "Block_size" block of state.
   Effects: Informs library that the "Block_size" block containing mem
   was modified. (When applicable, it is more efficient than aardvark_modify or
   aardvark_modify2.) */

#define aardvark_modify2(mem,size) _A_aardvark_modify2(mem,size)
/* void aardvark_modify2(char *mem, unsigned int size);
   Requires: Same as aardvark_modify and modified memory can not span more
   than two "Block_size" block of state.
   Effects: Same as aardvark_modify and more efficient than aardvark_modify and less efficient
   than aardvark_modify1. */



void Byz_replica_run();
/* Effects: Loops executing requests. */

void aardvark_reset_stats();
/* Effects: Resets library's statistics counters */

void aardvark_print_stats();
/* Effects: Print library statistics to stdout */

void aardvark_reset_client();
/* Reverts client to its initial state to ensure independence of experimental
   points */


#ifdef __cplusplus
}
#endif

#endif /* _LIBBYZ_H */
