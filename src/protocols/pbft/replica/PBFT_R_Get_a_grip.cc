#include <strings.h>
#include "th_assert.h"
#include "PBFT_R_Message_tags.h"
#include "PBFT_R_Replica.h"
#include "PBFT_R_Get_a_grip.h"
#include "PBFT_R_Request.h"
#include "PBFT_R_Principal.h"

#define MAX_NON_DET_SIZE 8

PBFT_R_Get_a_grip::PBFT_R_Get_a_grip(PBFT_R_Get_a_grip_rep *cont)
{
   th_assert(ALIGNED(cont), "Improperly aligned pointer");
   msg = cont;
   max_size = -1; // To prevent contents from being deallocated or trimmed
}

PBFT_R_Get_a_grip::PBFT_R_Get_a_grip(int cid, Request_id req_id, int replica, Seqno s, PBFT_R_Request *r) :
   PBFT_R_Message(PBFT_R_Get_a_grip_tag, Max_message_size)
{
   rep().cid = cid;
   rep().req_id = req_id;
   rep().replica = replica;
   rep().seqno = s;

   int size = sizeof(PBFT_R_Get_a_grip_rep) + r->size();

   set_size(size);
   memcpy(stored_request(), r->contents(), r->size());

   //   fprintf(stderr, "PBFT_R_Get_a_grip: setting size to %d (sizeof(PBFT_R_Get_a_grip) = %d)\n", size,
   //   sizeof(PBFT_R_Get_a_grip_rep));
   //   fprintf(stderr, "The stored reply has size %d\n", ((PBFT_R_Message_rep *)reply())->size);
}

bool PBFT_R_Get_a_grip::convert(PBFT_R_Message *m1, PBFT_R_Get_a_grip *&m2)
{
   if (!m1->has_tag(PBFT_R_Get_a_grip_tag, sizeof(PBFT_R_Get_a_grip_rep)))
      return false;

   //  m1->trim(); We trim the OR message after authenticating the message
   m2 = (PBFT_R_Get_a_grip*)m1;
   return true;
}

