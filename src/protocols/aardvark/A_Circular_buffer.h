/*
 * A_Circular_buffer.h
 *
 *  Created on: Sep 9, 2011
 *      Author: benmokhtar
 */

#ifndef A_CIRCULAR_BUFFER_H_
#define A_CIRCULAR_BUFFER_H_
#include "A_Message.h"
#include <sys/eventfd.h> // eventfd()



class A_Circular_buffer
{
public:
  A_Circular_buffer(int size);
  virtual ~A_Circular_buffer();


  bool cb_write_msg(A_Message*);
   // write A_Message* inside circualr_buffer
   // returns true if the message hase been written in the buffer
   // or false if the buffer is full

   A_Message* cb_read_msg();
   // reads a message from the circular buffer
   // returns a pointer to the message if there is something ready to read
   // or NULL if the buffer is empty
   long nb_read_messages;
   long nb_write_messages;
   int fd;
private:
   int length;
   A_Message** circular_buffer; // circuler buffer used to pass messages from the A_verifier thread to the main thread
   int cb_write_index; // points to the first empty position in the buffer (ready to be written)
   int cb_read_index; // points at the oldest unread message stored in the buffer (ready to be read)
   A_Message* circular_buffer_magic;
   uint64_t notif;
};

#endif /* A_CIRCULAR_BUFFER_H_ */
