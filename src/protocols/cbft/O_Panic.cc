#include <strings.h>
#include "th_assert.h"
#include "O_Message_tags.h"
#include "O_Replica.h"
#include "O_Panic.h"
#include "O_Request.h"
#include "O_Principal.h"

#define MAX_NON_DET_SIZE 8

O_Panic::O_Panic(O_Panic_rep *cont)
{
   th_assert(ALIGNED(cont), "Improperly aligned pointer");
   msg = cont;
   max_size = -1; // To prevent contents from being deallocated or trimmed
}

O_Panic::O_Panic(O_Request *req) :
   O_Message(O_Panic_tag, O_Max_message_size)
{
   rep().cid = req->client_id();
   rep().req_id = req->request_id();

   set_size(sizeof(O_Panic_rep));
}

bool O_Panic::convert(O_Message *m1, O_Panic *&m2)
{
   if (!m1->has_tag(O_Panic_tag, sizeof(O_Panic_rep)))
      return false;

   //  m1->trim(); We trim the OR message after authenticating the message
   m2 = (O_Panic*)m1;
   return true;
}

