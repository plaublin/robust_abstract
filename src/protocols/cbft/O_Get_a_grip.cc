#include <strings.h>
#include "th_assert.h"
#include "O_Message_tags.h"
#include "O_Replica.h"
#include "O_Get_a_grip.h"
#include "O_Request.h"
#include "O_Principal.h"

#define MAX_NON_DET_SIZE 8

O_Get_a_grip::O_Get_a_grip(O_Get_a_grip_rep *cont)
{
   th_assert(ALIGNED(cont), "Improperly aligned pointer");
   msg = cont;
   max_size = -1; // To prevent contents from being deallocated or trimmed
}

O_Get_a_grip::O_Get_a_grip(int cid, Request_id req_id, int replica, Seqno s, O_Request *r) :
   O_Message(O_Get_a_grip_tag, O_Max_message_size)
{
   rep().cid = cid;
   rep().req_id = req_id;
   rep().replica = replica;
   rep().seqno = s;

   int size = sizeof(O_Get_a_grip_rep) + r->size();

   set_size(size);
   memcpy(stored_request(), r->contents(), r->size());

   //   fprintf(stderr, "O_Get_a_grip: setting size to %d (sizeof(O_Get_a_grip) = %d)\n", size,
   //   sizeof(O_Get_a_grip_rep));
   //   fprintf(stderr, "The stored reply has size %d\n", ((O_Message_rep *)reply())->size);
}

bool O_Get_a_grip::convert(O_Message *m1, O_Get_a_grip *&m2)
{
   if (!m1->has_tag(O_Get_a_grip_tag, sizeof(O_Get_a_grip_rep)))
      return false;

   //  m1->trim(); We trim the OR message after authenticating the message
   m2 = (O_Get_a_grip*)m1;
   return true;
}

