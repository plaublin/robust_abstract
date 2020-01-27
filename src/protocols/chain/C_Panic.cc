#include <strings.h>
#include "th_assert.h"
#include "C_Message_tags.h"
#include "C_Replica.h"
#include "C_Panic.h"
#include "C_Request.h"
#include "C_Principal.h"

#define MAX_NON_DET_SIZE 8

C_Panic::C_Panic(C_Panic_rep *cont)
{
   th_assert(ALIGNED(cont), "Improperly aligned pointer");
   msg = cont;
   max_size = -1; // To prevent contents from being deallocated or trimmed
}

C_Panic::C_Panic(C_Request *req) :
   C_Message(C_Panic_tag, C_Max_message_size)
{
   rep().cid = req->client_id();
   rep().req_id = req->request_id();

   set_size(sizeof(C_Panic_rep));
}

bool C_Panic::convert(C_Message *m1, C_Panic *&m2)
{
   if (!m1->has_tag(C_Panic_tag, sizeof(C_Panic_rep)))
      return false;

   //  m1->trim(); We trim the OR message after authenticating the message
   m2 = (C_Panic*)m1;
   return true;
}

