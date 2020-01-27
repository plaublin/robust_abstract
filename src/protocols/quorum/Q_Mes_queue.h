#ifndef _Q_Mes_queue_h
#define _Q_Mes_queue_h

#include "types.h"

class Q_Message;

class Q_Mes_queue
{
   // 
   // Implements a bounded queue of messages.
   //
public:
   Q_Mes_queue();
   // Effects: Creates an empty queue that can hold one request per principal.

   bool append(Q_Message *r);
   // Effects: If there is space in the queue and there is no request
   // from "r->client_id()" with timestamp greater than or equal to
   // "r"'s in the queue then it: appends "r" to the queue, removes any
   // other request from "r->client_id()" from the queue and returns
   // true. Otherwise, returns false.

   Q_Message *remove();
   // Effects: If there is any element in the queue, removes the first
   // element in the queue and returns it. Otherwise, returns 0.

   Q_Message* first() const;
   // Effects: If there is any element in the queue, returns a pointer to
   // the first request in the queue. Otherwise, returns 0.

   int size() const;
   // Effects: Returns the current size (number of elements) in queue.

   int num_bytes() const;
   // Effects: Return the number of bytes used by elements in the queue.

private:
   struct PQ_Node
   {
      Q_Message* m;
      PQ_Node* next;
      PQ_Node* prev;

      PQ_Node(Q_Message *msg)
      {
         m = msg;
      }
      ~PQ_Node()
      {

      }
   };

   PQ_Node* head;
   PQ_Node* tail;

   int nelems; // Number of elements in queue
   int nbytes; // Number of bytes in queue

};

inline int Q_Mes_queue::size() const
{
   return nelems;
}

inline int Q_Mes_queue::num_bytes() const
{
   return nbytes;
}

inline Q_Message *Q_Mes_queue::first() const
{
   if (head)
   {
      //th_assert(head->m != 0, "Invalid state");
      return head->m;
   }
   return 0;
}

#endif // _Q_Mes_queue_h
