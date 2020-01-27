#include "zl_Message.h"
#include "zl_Mes_queue.h"

zl_Mes_queue::zl_Mes_queue() :
   head(0), tail(0), nelems(0), nbytes(0)
{
}

bool zl_Mes_queue::append(zl_Message *m)
{
   PC_Node *cn = new PC_Node(m);

   nbytes += m->size();
   nelems++;

   if (head == 0)
   {
      head = tail = cn;
      cn->prev = cn->next = 0;
   }
   else
   {
      tail->next = cn;
      cn->prev = tail;
      cn->next = 0;
      tail = cn;
   }
   return true;
}

zl_Message *zl_Mes_queue::remove()
{
   if (head == 0)
      return 0;

   zl_Message *ret = head->m;
   th_assert(ret != 0, "Invalid state");

   PC_Node* old_head = head;
   head = head->next;
   delete old_head;

   if (head != 0)
      head->prev = 0;
   else
      tail = 0;

   nelems--;
   nbytes -= ret->size();

   return ret;
}
