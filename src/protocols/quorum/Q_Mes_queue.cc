#include "Q_Message.h"
#include "Q_Mes_queue.h"

Q_Mes_queue::Q_Mes_queue() :
   head(0), tail(0), nelems(0), nbytes(0)
{
}

bool Q_Mes_queue::append(Q_Message *m)
{
   PQ_Node *cn = new PQ_Node(m);

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

Q_Message *Q_Mes_queue::remove()
{
   if (head == 0)
      return 0;

   Q_Message *ret = head->m;
   th_assert(ret != 0, "Invalid state");

   PQ_Node* old_head = head;
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
