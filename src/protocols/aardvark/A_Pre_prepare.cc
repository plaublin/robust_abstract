#include "th_assert.h"
#include "A_Message_tags.h"
#include "A_Prepare.h"
#include "A_Pre_prepare.h"
#include "A_Replica.h"
#include "A_Request.h"
#include "A_Req_queue.h"
#include "A_Principal.h"
#include "MD5.h"


A_Pre_prepare::A_Pre_prepare(View v, Seqno s, A_Req_queue &reqs) :
  A_Message(A_Pre_prepare_tag, A_Max_message_size)
{
  rep().view = v;
  rep().seqno = s;

  //START_CC(pp_digest_cycles);
  //INCR_OP(pp_digest);

  // Fill in the request portion with as many requests as possible
  // and compute digest.
  Digest big_req_ds[big_req_max];
  int n_big_reqs = 0;
  char *next_req = requests();
#ifndef USE_PKEY
  char *max_req = next_req + msize() - A_replica->max_nd_bytes()
      - A_node->auth_size();
#else 
  char *max_req = next_req+msize()-A_replica->max_nd_bytes()-A_node->sig_size();
#endif
  MD5_CTX context;
  MD5Init(&context);
  int count = 0;
  for (A_Request *req = reqs.first(); req != 0 && count < batch_size_limit; req
      = reqs.first(), count++)
  {
    // corruptClientMac() returns true if teh primary is corrupting
    // the client MAC
    if (corruptClientMAC())
      req->corrupt(corruptClientMAC());
    if (req->size() <= A_Request::big_req_thresh)
    {
      // Small requests are inlined in the pre-prepare message.
      if (next_req + req->size() <= max_req)
      {
        memcpy(next_req, req->contents(), req->size());
        MD5Update(&context, (char*) &(req->digest()), sizeof(Digest));
        next_req += req->size();
#ifdef FAIRNESS_ADAPTIVE
        // increment the number of requests in the current batch
        //A_replica->increment_req_count();
#endif
        delete reqs.remove();
      }
      else
      {
        break;
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
        break;
      }
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
    int non_det_size = A_replica->max_nd_bytes();
    A_replica->compute_non_det(s, non_det_choices(), &non_det_size);
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
  int old_size = sizeof(A_Pre_prepare_rep) + rep().rset_size + rep().n_big_reqs
      * sizeof(Digest) + rep().non_det_size;

#ifndef USE_PKEY
  set_size(old_size + A_node->auth_size());
  A_node->gen_auth_out(contents(), sizeof(A_Pre_prepare_rep), contents() + old_size);
#else 
  set_size(old_size+A_node->sig_size());
  A_node->gen_signature(contents(), sizeof(A_Pre_prepare_rep), contents()+old_size);
#endif

  trim();
}

#ifdef RRBFT_ATTACK

// create a fake PP. Used for the RRBFT_ATTACK
A_Pre_prepare::A_Pre_prepare(bool fake) :
          A_Message(A_Pre_prepare_tag, A_Max_message_size)
{
}

// given a A_Req_queue, assess the size of the PP
int A_Pre_prepare::assess_size(A_Req_queue &reqs) {
  int size_max, rsize;

#ifndef USE_PKEY
  size_max = msize() - A_replica->max_nd_bytes()
      - A_node->auth_size();
#else
  size_max = msize()-A_replica->max_nd_bytes()-A_node->sig_size();
#endif

  // we use nelems and nbytes. It does not work well if the requests have different sizes
  // and if the requests are big requests, for which only the digest is sent.
  if (reqs.size() == 0) {
      return 0;
  } else {
      rsize = reqs.num_bytes() / reqs.size();
      return (int) (size_max / rsize);
  }
}

#endif

A_Pre_prepare* A_Pre_prepare::clone(View v) const
{
  A_Pre_prepare *ret = (A_Pre_prepare*) new A_Message(max_size);
  memcpy(ret->msg, msg, msg->size);
  ret->rep().view = v;
  return ret;
}

void A_Pre_prepare::re_authenticate(A_Principal *p)
{
#ifndef USE_PKEY 
  A_node->gen_auth_out(contents(), sizeof(A_Pre_prepare_rep), non_det_choices()
      + rep().non_det_size);
#endif 
}

int A_Pre_prepare::id() const
{
  return A_replica->primary(view());
}

bool A_Pre_prepare::check_digest()
{
  // Check sizes
#ifndef USE_PKEY
  int min_size = sizeof(A_Pre_prepare_rep) + rep().rset_size + rep().n_big_reqs
      * sizeof(Digest) + rep().non_det_size + A_node->auth_size(A_replica->primary(
      view()));
#else
  int min_size = sizeof(A_Pre_prepare_rep)+rep().rset_size+rep().n_big_reqs*sizeof(Digest)
  +rep().non_det_size+A_node->sig_size(A_replica->primary(view()));
#endif
  if (size() >= min_size)
  {
    //START_CC(pp_digest_cycles);
    //INCR_OP(pp_digest);

    // Check digest.
    MD5_CTX context;
    MD5Init(&context);
    Digest d;
    A_Request req;
    char *max_req = requests() + rep().rset_size;
    for (char *next = requests(); next < max_req; next += req.size())
    {
      if (A_Request::convert(next, max_req - next, req))
      {
        MD5Update(&context, (char*) &(req.digest()), sizeof(Digest));
      }
      else
      {
        //STOP_CC(pp_digest_cycles);
        return false;
      }
    }

    // Finalize digest of requests and non-det-choices.
    MD5Update(&context, (char*) big_reqs(), rep().n_big_reqs * sizeof(Digest)
        + rep().non_det_size);
    MD5Final(d.udigest(), &context);

    //STOP_CC(pp_digest_cycles);
    return d == rep().digest;
  }
  return false;
}

bool A_Pre_prepare::verify(int mode)
{
  int sender = A_replica->primary(view());

  // Check sizes and digest.
  int sz = rep().rset_size + rep().n_big_reqs * sizeof(Digest)
      + rep().non_det_size;
#ifndef USE_PKEY
  int min_size = sizeof(A_Pre_prepare_rep) + sz + A_node->auth_size(
      A_replica->primary(view()));
#else
  int min_size = sizeof(A_Pre_prepare_rep)+sz+A_node->sig_size(A_replica->primary(view()));
#endif
  if (size() >= min_size)
  {
    //INCR_OP(pp_digest);

    // Check digest.
    Digest d;
    MD5_CTX context;
    MD5Init(&context);
    A_Request req;
    char* max_req = requests() + rep().rset_size;
    for (char *next = requests(); next < max_req; next += req.size())
    {
      if (A_Request::convert(next, max_req - next, req) && (mode == NRC
          || A_replica->has_req(req.client_id(), req.digest()) || req.verify()))
      {
        //START_CC(pp_digest_cycles);

        MD5Update(&context, (char*) &(req.digest()), sizeof(Digest));

        //STOP_CC(pp_digest_cycles);
      }
      else
      {
        return false;
      }

      // TODO: If we batch requests from different clients. We need to
      // change this a bit. Otherwise, a good client could be denied
      // service just because its request was batched with the request
      // of another client.  A way to do this would be to include a
      // bitmap with a bit set for each request that verified.
    }

    //START_CC(pp_digest_cycles);

    // Finalize digest of requests and non-det-choices.
    MD5Update(&context, (char*) big_reqs(), rep().n_big_reqs * sizeof(Digest)
        + rep().non_det_size);
    MD5Final(d.udigest(), &context);

    //STOP_CC(pp_digest_cycles);

#ifndef USE_PKEY
    if (d == rep().digest)
    {
      return mode == NAC || A_node->verify_auth_in(sender, contents(),
          sizeof(A_Pre_prepare_rep), requests() + sz);
    }
#else
    if (d == rep().digest)
    {
      A_Principal* ps = A_node->i_to_p(sender);
      return mode == NAC
      || ps->verify_signature(contents(), sizeof(A_Pre_prepare_rep), requests()+sz);
    }

#endif
  }
  return false;
}

A_Pre_prepare::Requests_iter::Requests_iter(A_Pre_prepare *m)
{
  msg = m;
  next_req = m->requests();
  big_req = 0;
}

bool A_Pre_prepare::Requests_iter::get(A_Request &req)
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

bool A_Pre_prepare::convert(A_Message *m1, A_Pre_prepare *&m2)
{
  if (!m1->has_tag(A_Pre_prepare_tag, sizeof(A_Pre_prepare_rep)))
    return false;

  m2 = (A_Pre_prepare*) m1;
  m2->trim();
  return true;
}

