#include <strings.h>
#include "th_assert.h"
#include "C_Message_tags.h"
#include "C_Replica.h"
#include "C_Get_a_grip.h"
#include "C_Request.h"
#include "C_Principal.h"

#define MAX_NON_DET_SIZE 8

C_Get_a_grip::C_Get_a_grip(C_Get_a_grip_rep *cont)
{
   th_assert(ALIGNED(cont), "Improperly aligned pointer");
   msg = cont;
   max_size = -1; // To prevent contents from being deallocated or trimmed
}

C_Get_a_grip::C_Get_a_grip(int cid, Request_id req_id, int replica, Seqno s, C_Request *r) :
   C_Message(C_Get_a_grip_tag, C_Max_message_size)
{
   rep().cid = cid;
   rep().req_id = req_id;
   rep().replica = replica;
   rep().seqno = s;

   int size = sizeof(C_Get_a_grip_rep) + r->size();

   set_size(size);
   memcpy(stored_request(), r->contents(), r->size());

   //   fprintf(stderr, "C_Get_a_grip: setting size to %d (sizeof(C_Get_a_grip) = %d)\n", size,
   //   sizeof(C_Get_a_grip_rep));
   //   fprintf(stderr, "The stored reply has size %d\n", ((C_Message_rep *)reply())->size);
}

bool C_Get_a_grip::convert(C_Message *m1, C_Get_a_grip *&m2)
{
   if (!m1->has_tag(C_Get_a_grip_tag, sizeof(C_Get_a_grip_rep)))
      return false;

   //  m1->trim(); We trim the OR message after authenticating the message
   m2 = (C_Get_a_grip*)m1;
   return true;
}

