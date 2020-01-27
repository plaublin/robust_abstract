#include <strings.h>
#include "th_assert.h"
#include "C_Message_tags.h"
// #include "C_Replica.h"
#include "C_Abort.h"
#include "C_Request.h"
#include "C_Principal.h"
#include "C_Node.h"
#include "Array.h"
#include "Digest.h"

#define MAX_NON_DET_SIZE 8

C_Abort::C_Abort(int p_replica, Request_id p_rid, Req_history_log<C_Request> &req_h) :
   C_Message(C_Abort_tag, C_Max_message_size)
{
   rep().rid = p_rid;
   rep().replica = p_replica;
   int req_h_size = req_h.array().size();

   //fprintf(stderr, "Constructor of C_Abort for request (cid = %d, reC_id = %llu, replica = %d)\n", p_cid, p_rid, p_replica);

   // We don't know whether [cid, reC_id] is in the request history or not
   int offset = sizeof(C_Abort_rep);
   int i = 0;

   for (i=0; i<req_h_size; i++)
   {
      Rh_entry<C_Request> rhe = req_h.array()[i];
      if (rhe.req == NULL) {
	  continue;
      }
      if (offset + (sizeof(int)+sizeof(Request_id) + sizeof(Digest))
            > /*C_Max_message_size*/ size() - C_node->sig_size())
      {
         //fprintf(stderr, "Too many messages in the abort message (%d)\n", req_h_size);
         rep().size *= 2;
         trim();
      }

      int cur_cid = rhe.req->client_id();
      Request_id cur_rid = rhe.req->request_id();
      Digest cur_d = rhe.req->digest();

      /*
      if (cur_cid == p_cid && cur_rid == p_rid)
      {
         rep().aborted_request_digest = cur_d;
         break;
      }
      */

      memcpy(contents()+offset, (char *)&cur_cid, sizeof(int));
      offset+=sizeof(int);
      memcpy(contents()+offset, (char *)&cur_rid, sizeof(Request_id));
      offset+=sizeof(Request_id);
      memcpy(contents()+offset, cur_d.digest(), sizeof(Digest));
      offset+=sizeof(Digest);
   }

   //fprintf(stderr, "history size = %d\n", i);
   //fprintf(stderr, "offset = %d\n", offset);
   rep().hist_size = i;

   set_size(offset);

   digest() = Digest(contents() + sizeof(C_Abort_rep), offset - sizeof(C_Abort_rep));
   //fprintf(stderr, "end of creation\n");
}

C_Abort::C_Abort(C_Abort_rep *cont)
{
   th_assert(ALIGNED(cont), "Improperly aligned pointer");
   msg = cont;
   max_size = -1; // To prevent contents from being deallocated or trimmed
}

bool C_Abort::convert(C_Message *m1, C_Abort *&m2)
{
   if (!m1->has_tag(C_Abort_tag, sizeof(C_Abort_rep)))
      return false;

   m1->trim();
   m2 = (C_Abort*)m1;
   return true;
}

void C_Abort::sign()
{
   int old_size = sizeof(C_Abort_rep) + (rep().hist_size * ((sizeof(int) + sizeof(Request_id) + sizeof(Digest))));
   if (msize() - old_size < C_node->sig_size())
   {
      fprintf(stderr, "C_Abort: Not enough space to sign message\n");
      exit(1);
   }
   set_size(old_size + C_node->sig_size());
   // We need to add a digest here of the full content...
   C_node->gen_signature(contents(), sizeof(C_Abort_rep), contents() +old_size);
   //fprintf(stderr, "signature is made at offset %d\n", old_size);
}

// This method must be called by a backup-BFT replica
bool C_Abort::verify()
{
   // Replies must be sent by replicas.
   if (!C_node->is_replica(id()))
   {
      return false;
   }

   C_Principal *abstract_replica = C_node->i_to_p(rep().replica);

   unsigned int size_wo_MAC = sizeof(C_Abort_rep) + (rep().hist_size * ((sizeof(int) + sizeof(Request_id) + sizeof(Digest))));

   // Check digest
   Digest d(contents() + sizeof(C_Abort_rep), size_wo_MAC - sizeof(C_Abort_rep));

   if (d != digest())
   {
      fprintf(stderr, "C_Abort::verify: digest is not equal\n");
      return false;
   }

   // Check signature (enough space)
   if (size()-size_wo_MAC < abstract_replica->sig_size())
   {
      return false;
   }

   // Check signature.
   bool ret = abstract_replica->verify_signature(contents(), sizeof(C_Abort_rep),
         contents() + size_wo_MAC, true);

   return ret;
}
