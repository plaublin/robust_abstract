#include "th_assert.h"
#include "A_Message_tags.h"
#include "A_Prepare.h"
#include "A_Pre_prepare.h"
#include "A_Replica.h"
#include "A_Request.h"
#include "A_Req_queue.h"
#include "A_Principal.h"
#include "MD5.h"
#include "A_Wrapped_request.h"
#include "A_parameters.h"

A_Wrapped_request::A_Wrapped_request(View v, Seqno s, A_Req_queue &reqs,
    bool faultyClient) :
  A_Message(A_Wrapped_request_tag, A_Max_message_size)
{
  rep().view = A_node->id(); // view is used to store the client id!!!
  rep().seqno = s;

  //START_CC(pp_digest_cycles);
  //INCR_OP(pp_digest);

  // Fill in the request portion with as many requests as possible
  // and compute digest.
  Digest big_req_ds[big_req_max];
  int n_big_reqs = 0;
  char *next_req = requests();
#ifndef USE_PKEY
  char *max_req = next_req + msize()
      - /*A_replica->max_nd_bytes()-*/A_node->auth_size();
#else 
  char *max_req = next_req+msize()-/*A_node->max_nd_bytes()-*/A_node->sig_size();
#endif
  MD5_CTX context;
  MD5Init(&context);

  A_Request *req = reqs.first();
  //fprintf(stderr, "!inside the iterator!");
  // corruptClientMac() returns true if teh primary is corrupting
  // the client MAC
  if (req->size() <= A_Request::big_req_thresh)
  {
    // Small requests are inlined in the pre-prepare message.
    if (next_req + req->size() <= max_req)
    {
      memcpy(next_req, req->contents(), req->size());
      MD5Update(&context, (char*) &(req->digest()), sizeof(Digest));
      next_req += req->size();
      delete reqs.remove();
    }
    else
    {
      //break;
    }
  }
  else
  {
    // Big requests are sent offline and their digests are sent
    // with pre-prepare message.
    if (n_big_reqs < big_req_max && next_req + sizeof(Digest) <= max_req)
    {
      big_req_ds[n_big_reqs++] = req->digest();

      // Add request to A_replica's big reqs table.
      A_replica->big_reqs()->add_pre_prepare(reqs.remove(), s, v);
      max_req -= sizeof(Digest);
    }
    else
    {
      //break;
    }
  }
  rep().rset_size = next_req - requests();
  th_assert(rep().rset_size >= 0, "Request too big");

  // Put big requests after regular ones.
  for (int i = 0; i < n_big_reqs; i++)
    *(big_reqs() + i) = big_req_ds[i];
  rep().n_big_reqs = n_big_reqs;

  if (rep().rset_size > 0 || n_big_reqs > 0)
  {
    // Fill in the non-deterministic choices portion.
    //int non_det_size = A_replica->max_nd_bytes();
    int non_det_size = 0;
    //A_replica->compute_non_det(s, non_det_choices(), &non_det_size);
    rep().non_det_size = non_det_size;
  }
  else
  {
    // Null request
    rep().non_det_size = 0;
  }

  // Finalize digest of requests and non-det-choices.
  MD5Update(&context, (char*) big_reqs(), n_big_reqs * sizeof(Digest)
      + rep().non_det_size);
  MD5Final(rep().digest.udigest(), &context);

  //STOP_CC(pp_digest_cycles);

  // Compute authenticator and update size.
  int old_size = sizeof(A_Wrapped_request_rep) + rep().rset_size
      + rep().n_big_reqs * sizeof(Digest) + rep().non_det_size;

#ifndef USE_PKEY
  set_size(old_size + A_node->auth_size());
  if (A_node->is_replica(A_node->id()))
    A_node->gen_auth_out(contents(), sizeof(A_Wrapped_request_rep), contents()
        + old_size); // A_replica -> A_replica
  else
    A_node->gen_auth_in(contents(), sizeof(A_Wrapped_request_rep), contents()
        + old_size, faultyClient); // client -> A_replica
#else 
  set_size(old_size+A_node->sig_size());
  A_node->gen_signature(contents(), sizeof(A_Wrapped_request_rep), contents()+old_size);
#endif

  trim();

  /*
  fprintf(stderr, "The generated MAC is ");
  rep().digest.print();
  fprintf(stderr, "\n");
  */
}

void A_Wrapped_request::re_authenticate(A_Principal *p)
{
#ifndef USE_PKEY 
  A_node->gen_auth_out(contents(), sizeof(A_Wrapped_request_rep), non_det_choices()
      + rep().non_det_size);
#endif 
}

bool A_Wrapped_request::verify_MAC(int mode)
{
  int sender = (int) rep().view; //view is used to store the client id

  /*
   printf(
   "[A_replica %i][primary %i] Verifying mac of request %qd from client %i\n",
   A_replica->id(), A_replica->primary(), this->seqno(), sender);
   */

  // Check sizes and digest.
  int sz = rep().rset_size + rep().n_big_reqs * sizeof(Digest)
      + rep().non_det_size;
#ifndef USE_PKEY
  int min_size = sizeof(A_Wrapped_request_rep) + sz + A_node->auth_size(sender);
#else
  int min_size = sizeof(A_Wrapped_request_rep)+sz+A_node->sig_size(sender);
#endif

  if (size() < min_size)
  {
    /*
     fprintf(stderr, "[A_replica %i][primary %i] request %qd from client %i, %i < %i\n",
     A_replica->id(), A_replica->primary(), this->seqno(), sender, size(),
     min_size);
     */
    return false;
  }

  // Check digest.
  Digest d;
  MD5_CTX context;
  MD5Init(&context);
  A_Request req;
  //START_CC(pp_digest_cycles);

  char* max_req = requests() + rep().rset_size;
  char* next = requests(); //and first, and last

  if (!A_Request::convert(next, max_req - next, req))
  {
    /*
     printf(
     "[A_replica %i][primary %i] request %qd from client %i, convert has failed\n",
     A_replica->id(), A_replica->primary(), this->seqno(), sender);
     */
    return false; //unable to extract requests
  }

  /* 
   * PL: do not test if i would append the request in openloop, otherwise
   * you get a race condition and the assertion "A_Message.h:224: Assertion `msg null msg'"
   * is raised.
   */
  //if i would not append the request, throw it away immediately
  /*
  if (!A_replica->rqueue.wouldAppend(&req))
  {
    //printf(
    // "[A_replica %i][primary %i] request %qd from client %i, wouldAppend false\n",
    // A_replica->id(), A_replica->primary(), this->seqno(), sender);
    return false;
  }
  */

  // Finalize digest of requests and non-det-choices.
  MD5Update(&context, (char*) big_reqs(), rep().n_big_reqs * sizeof(Digest)
      + rep().non_det_size);
  //PL: MD5Final(d.udigest(), &context);

  //STOP_CC(pp_digest_cycles);

#ifndef USE_PKEY

  //START_CC(pp_digest_cycles);
  MD5Update(&context, (char*) &(req.digest()), sizeof(Digest));
  //STOP_CC(pp_digest_cycles);

  MD5Final(d.udigest(), &context);

  if (d == rep().digest)
  {
    bool auth_ok;

    if (A_node->is_replica(sender))
      auth_ok = A_node->verify_auth_in(sender, contents(),
          sizeof(A_Wrapped_request_rep), requests() + sz); // A_replica -> A_replica
    else
      auth_ok = A_node->verify_auth_out(sender, contents(),
          sizeof(A_Wrapped_request_rep), requests() + sz); // client -> A_replica

    /*
     fprintf(stderr, "mode=%i, NAC=%i, auth_ok=%s\n", mode, NAC,
     (auth_ok ? "true" : "false"));
     */

    if (!(mode == NAC || auth_ok))
    {
      /*
       printf(
       "[A_replica %i][primary %i] request %qd from client %i, digest ok but not authenticated\n",
       A_replica->id(), A_replica->primary(), this->seqno(), sender);
       */
      // digest ok, but not authenticated
      return false;
    }

  }
  else
  {
    /*
     fprintf(stderr, "digest is not valid: ");
     d.print();
     fprintf(stderr, " instead of ");
     rep().digest.print();
     fprintf(stderr, "\n");
     */
  }

#else
  if (d == rep().digest)
  {
    A_Principal* ps = A_node->i_to_p(sender);
    if ( ! (mode == NAC
            || ps->verify_signature(contents(), sizeof(A_Wrapped_request_rep), requests()+sz)))
    {
      //digest ok, but not authenticated
      return false;
      {
      }
      else
      {
        //different digest
        return false;
      }

#endif

  // authenticating the signature in the A_verifier thread now!
  // the A_verifier thread will call this function to verity
  // digest and mac. If this function returns false, the request
  // will be dropped without other consequences.
  // If this function returns true, the A_verifier thread knows
  // that digest and MAC are ok. After that, the A_verifier thread
  // will verifi the request's signature. If the signature verifies,
  // then the request is ordered.
  // if the sugnature does not verify, we assume that the client is
  // trying to do something weird, and blacklist the client.
  return true;

  /*

   request verification code, moved inside the A_verifier thread.
   // i made it through there, so both digest and authentication are ok.
   // now verify the request inside (with signatures)

   //INCR_OP(pp_digest);
   if( mode == NRC || req.verify() ) {
   // digest, authentication, and requests are ok
   return true;

   } else {
   //digest and authentication ok, request KO!
   return false;
   }

   return false;
   */

}

bool A_Wrapped_request::verify_request(int mode)
{

  char* max_req = requests() + rep().rset_size;
  char* next = requests(); //and first, and last

  A_Request req;

  if (!A_Request::convert(next, max_req - next, req))
    return false; //unable to extract requests

  if (mode == NRC || req.verify())
  {
    return true; // ok, request verified
  }

  return false; //KO, request NOT verified

}

A_Wrapped_request::Requests_iter::Requests_iter(A_Wrapped_request *m)
{
  msg = m;
  next_req = m->requests();
  big_req = 0;
}

bool A_Wrapped_request::Requests_iter::get(A_Request &req)
{
  if (next_req < msg->requests() + msg->rep().rset_size)
  {
    req = A_Request((A_Request_rep*) next_req);
    next_req += req.size();
    return true;
  }

  if (big_req < msg->num_big_reqs())
  {
    A_Request* r = A_replica->big_reqs()->lookup(msg->big_req_digest(big_req));
    th_assert(r != 0, "Missing big req");
    req = A_Request((A_Request_rep*) r->contents());
    big_req++;
    return true;
  }

  return false;
}

bool A_Wrapped_request::convert(A_Message *m1, A_Wrapped_request *&m2)
{
  if (!m1->has_tag(A_Pre_prepare_tag, sizeof(A_Pre_prepare_rep)))
    return false;

  m2 = (A_Wrapped_request*) m1;
  m2->trim();
  return true;
}

