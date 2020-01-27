#include "A_Request.h"
#include "A_Req_queue.h"
#include "A_Pre_prepare.h"
#include "A_Node.h"
#include "A_Replica.h" //congestion_window
#include "Array.h"

A_Req_queue::A_Req_queue() :
  reqs(PNode(), A_node->np()), head(0), tail(0), nelems(0), nbytes(0), nadded(0),
      nremoved(0)
{
}

bool A_Req_queue::wouldAppend(A_Request *r)
{
  int cid = r->client_id();
  Request_id rid = r->request_id();
  PNode& cn = reqs[cid];
  if (cn.r)
  {
    // check if the message is not null and different than
    // a magic number meaning "message deleted"
    cn.r->check_msg();
  }

  if (cn.r != 0)
  {
    // There is a request from client cid in reqs.                                                                 
    if (rid > cn.r->request_id())
    {
      ;
    }
    else
    {
      // there is a request in reqs whose id is >= rid
      /* printf(
       "[A_replica %i][primary %i][A_Request %qd] request %qd already present\n",
       A_replica->id(), A_replica->primary(), rid, cn.r->request_id());
       */
      return false;
    }
  }
  return true;
}

bool A_Req_queue::append(A_Request *r)
{
  int cid = r->client_id();
  Request_id rid = r->request_id();
  PNode& cn = reqs[cid];

  if (cn.r)
  {
    // check if the message is not null and different than
    // a magic number meaning "message deleted"
    cn.r->check_msg();
  }

  if (cn.r != 0)
  {
    // There is a request from client cid in reqs.
    if (rid > cn.r->request_id())
    {
      remove(cid, rid);
    }
    else
    {
      return false;
    }
  }

  // Append request to queue.
  cn.r = r;
  nbytes += r->size();
  nelems++;
  nadded++;

  if (head == 0)
  {
    head = tail = &cn;
    cn.prev = cn.next = 0;
  }
  else
  {
    tail->next = &cn;
    cn.prev = tail;
    cn.next = 0;
    tail = &cn;
  }

  return true;
}

A_Request *A_Req_queue::remove()
{
  if (head == 0)
    return 0;

  A_Request *ret = head->r;
  th_assert(ret != 0, "Invalid state");

  head->r = 0;
  head = head->next;
  if (head != 0)
    head->prev = 0;
  else
    tail = 0;

  nelems--;
  nremoved++;
  nbytes -= ret->size();

  return ret;
}

bool A_Req_queue::remove(int cid, Request_id rid)
{
  bool ret = false;
  PNode& cn = reqs[cid];
  if (cn.r && cn.r->request_id() <= rid)
  {
    nelems--;
    nremoved++;
    nbytes -= cn.r->size();

    delete cn.r;
    cn.r = 0;

    if (cn.prev == 0)
    {
      th_assert(head == &cn, "Invalid state");
      head = cn.next;
      ret = true;
    }
    else
    {
      cn.prev->next = cn.next;
    }

    if (cn.next == 0)
    {
      th_assert(tail == &cn, "Invalid state");
      tail = cn.prev;
    }
    else
    {
      cn.next->prev = cn.prev;
    }
  }
  return ret;
}

void A_Req_queue::clear()
{
  for (int i = 0; i < A_node->np(); i++)
    reqs[i].clear();
  nremoved += nelems;
  head = tail = 0;
  nelems = nbytes = 0;
}

void A_Req_queue::PNode::clear()
{
  delete r;
  r = 0;
  next = prev = 0;
  out_rid = 0;
  out_v = -1;
}
