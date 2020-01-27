#include <strings.h>
#include "th_assert.h"
#include "C_Message_tags.h"
// #include "C_Replica.h"
#include "C_Missing.h"
#include "C_Request.h"
#include "C_Principal.h"
#include "C_Node.h"
#include "Array.h"
#include "Digest.h"

C_Missing::C_Missing(int p_replica, AbortHistory *missing) :
   C_Message(C_Missing_tag, C_Max_message_size)
{
   rep().replica = p_replica;
   int req_h_size = missing->size();

   //fprintf(stderr, "Constructor of C_Missing for request (cid = %d, req_id = %llu, replica = %d)\n", p_cid, p_rid, p_replica);

   // We don't know whether [cid, req_id] is in the request history or not
   ssize_t offset = sizeof(C_Missing_rep);
   int i = 0;

   for (i=0; i<req_h_size; i++)
   {
      if (offset + (sizeof(int)+sizeof(Request_id))
            > (signed)C_Max_message_size - C_node->sig_size())
      {
         //fprintf(stderr, "C_Missing: Too many messages in the abort message\n");
         rep().size *= 2;
         trim();
      }
      int cur_cid = missing->slot(i)->cid;
      Request_id cur_rid = missing->slot(i)->rid;

      memcpy(contents()+offset, (char *)&cur_cid, sizeof(int));
      offset+=sizeof(int);
      memcpy(contents()+offset, (char *)&cur_rid, sizeof(Request_id));
      offset+=sizeof(Request_id);
   }

   //fprintf(stderr, "history size = %d\n", i);
   //fprintf(stderr, "offset = %d\n", offset);
   rep().hist_size = i;

   digest() = Digest(contents() + sizeof(C_Missing_rep), offset - sizeof(C_Missing_rep));

   set_size(offset);
   //fprintf(stderr, "end of creation\n");
}

C_Missing::C_Missing(C_Missing_rep *cont)
{
   th_assert(ALIGNED(cont), "Improperly aligned pointer");
   msg = cont;
   max_size = -1; // To prevent contents from being deallocated or trimmed
}

bool C_Missing::convert(C_Message *m1, C_Missing *&m2)
{
   if (!m1->has_tag(C_Missing_tag, sizeof(C_Missing_rep)))
      return false;

   m1->trim();
   m2 = (C_Missing*)m1;
   return true;
}

void C_Missing::sign()
{
   int old_size = sizeof(C_Missing_rep) + (rep().hist_size * ((sizeof(int) + sizeof(Request_id) + sizeof(Digest))));
   if (msize() - (unsigned)old_size < C_node->sig_size())
   {
      fprintf(stderr, "C_Missing: Not enough space to sign message\n");
      exit(1);
   }
   set_size(old_size + C_node->sig_size());
   // We need to add a digest here of the full content...
   C_node->gen_signature(contents(), sizeof(C_Missing_rep), contents() +old_size);
   //fprintf(stderr, "signature is made at offset %d\n", old_size);
}

// This method must be called by a backup-BFT replica
bool C_Missing::verify()
{
   // Replies must be sent by replicas.
   if (!C_node->is_replica(id()))
   {
      return false;
   }

   C_Principal *abstract_replica = C_node->i_to_p(rep().replica);

   unsigned int size_wo_MAC = sizeof(C_Missing_rep) + (rep().hist_size * ((sizeof(int) + sizeof(Request_id) + sizeof(Digest))));

   // Check digest
   Digest d(contents() + sizeof(C_Missing_rep), size_wo_MAC - sizeof(C_Missing_rep));

   if (d != digest())
   {
      fprintf(stderr, "C_Missing::verify: digest is not equal\n");
      return false;
   }

   // Check signature (enough space)
   if ((signed)(size()-size_wo_MAC) < (signed)abstract_replica->sig_size())
   {
      return false;
   }

   // Check signature.
   bool ret = abstract_replica->verify_signature(contents(), sizeof(C_Missing_rep),
         contents() + size_wo_MAC, true);

   return ret;
}
