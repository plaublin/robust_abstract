/*
 * A_Circular_buffer.cpp
 *
 *  Created on: Sep 9, 2011
 *      Author: benmokhtar
 */

#include "A_Circular_buffer.h"
#include "A_Message.h"


A_Circular_buffer::A_Circular_buffer(int size)
{
  circular_buffer_magic = (A_Message*) 0x12344321; //I don't want to read that value
  length = size;
  circular_buffer = (A_Message**) malloc(size * sizeof(A_Message*));
  for (int i = 0; i < size; i++)
    circular_buffer[i] = circular_buffer_magic;
  cb_write_index = 0;
  cb_read_index = 0;
  fd = eventfd(0, EFD_SEMAPHORE);
//  nb_read_messages = 0;
//  nb_write_messages = 0;

}

A_Circular_buffer::~A_Circular_buffer()
{
}

bool A_Circular_buffer::cb_write_msg(A_Message* message)
{
  //fprintf(stderr, "Writing --- ri: %d, wi: %d \n",cb_read_index, cb_write_index);
  if (((cb_write_index + 1) % length) == cb_read_index)
    return false; //the buffer is full

  //the buffer is not full
  circular_buffer[cb_write_index] = message;
  cb_write_index = (cb_write_index + 1) % length;
  notif = 1;
  write(fd, &notif, sizeof(notif));
//  nb_write_messages++;
  return true;
}

A_Message* A_Circular_buffer::cb_read_msg()
{
  //fprintf(stderr, "Reading --- ri: %d, wi: %d \n",cb_read_index, cb_write_index);
  if (cb_write_index == cb_read_index)
    return NULL; //the buffer is empty

  //the buffer is not empty
  A_Message* temp = circular_buffer[cb_read_index];
  circular_buffer[cb_read_index] = circular_buffer_magic;
  cb_read_index = (cb_read_index + 1) % length;
//  nb_read_messages++;
  read(fd, &notif, sizeof(notif));
  return temp;
}
