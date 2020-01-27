#include <strings.h>
#include "th_assert.h"
#include "PBFT_R_Message_tags.h"
#include "PBFT_R_Replica.h"
#include "PBFT_R_Panic.h"
#include "PBFT_R_Request.h"
#include "PBFT_R_Principal.h"

#define MAX_NON_DET_SIZE 8

PBFT_R_Panic::PBFT_R_Panic(PBFT_R_Panic_rep *cont)
{
   th_assert(ALIGNED(cont), "Improperly aligned pointer");
   msg = cont;
   max_size = -1; // To prevent contents from being deallocated or trimmed
}

PBFT_R_Panic::PBFT_R_Panic(PBFT_R_Request *req) :
   PBFT_R_Message(PBFT_R_Panic_tag, Max_message_size)
{
   rep().cid = req->client_id();
   rep().req_id = req->request_id();

   set_size(sizeof(PBFT_R_Panic_rep));
}

bool PBFT_R_Panic::convert(PBFT_R_Message *m1, PBFT_R_Panic *&m2)
{
   if (!m1->has_tag(PBFT_R_Panic_tag, sizeof(PBFT_R_Panic_rep)))
      return false;

   //  m1->trim(); We trim the OR message after authenticating the message
   m2 = (PBFT_R_Panic*)m1;
   return true;
}

