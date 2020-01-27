/*
 * blocking_circular_buffer.cpp
 *
 *  Created on: 3 mai 2011
 *      Author: benmokhtar
 */

#include "Blocking_circular_buffer.h"
#undef CIRCULAR_BUFFER_DEBUG

Blocking_circular_buffer::Blocking_circular_buffer(int size, char *name)
{
  length = size;
  read_idx = 0;
  write_idx = 0;
  nb_msg = 0;
  pthread_mutex_init(&mutex, NULL);
  pthread_cond_init(&cond_can_write, NULL);
  pthread_cond_init(&cond_can_read, NULL);

  if (size > 0)
  {
    cb = new C_Message*[size];
    for (int i = 0; i < size; i++)
    {
      cb[i] = bcb_magic();
    }
  }
  else
  {
    cb = NULL;
  }

  this->name = name;
  nb_read_messages = 0;
  nb_write_messages = 0;
  read_start_time = currentC_Time();
  write_start_time = currentC_Time();
}

Blocking_circular_buffer::~Blocking_circular_buffer()
{
  delete[] cb;
}

int Blocking_circular_buffer::bcb_write_msg(C_Message *m)
{
  int ret = 0;

  pthread_mutex_lock(&mutex);

  while (nb_msg >= length)
  {
#ifdef CIRCULAR_BUFFER_DEBUG
    printf("Thread %u cannot add a message: nb_msg (%i) >= length (%i)\n", pthread_self(),
        nb_msg, length);
#endif
    pthread_cond_wait(&cond_can_write, &mutex);
  }

#ifdef CIRCULAR_BUFFER_DEBUG
  printf("Thread %u can add a message: nb_msg (%i) < length (%i)\n", pthread_self(),
      nb_msg, length);
#endif

  cb[write_idx] = m;
  write_idx = (write_idx + 1) % length;

  if (++nb_msg == 1)
  {
#ifdef CIRCULAR_BUFFER_DEBUG
    printf("Thread %u signals that messages can be received: nb_msg = %i\n",
        pthread_self(), nb_msg);
#endif
    pthread_cond_signal(&cond_can_read);
  }

  ret = 1;

  pthread_mutex_unlock(&mutex);

#ifdef MSG_DEBUG
  if (++nb_write_messages == PERIODIC_THR_DISPLAY)
  {
    C_Time now = currentC_Time();
    float elapsed_in_sec = diffC_Time(now, write_start_time) / 1000000.0;
    fprintf(stderr, "[%s] write thr: %f msg/sec\n", name, nb_write_messages / elapsed_in_sec);
    nb_write_messages = 0;
    write_start_time = now;
  }
#endif

  return ret;
}

C_Message* Blocking_circular_buffer::bcb_read_msg()
{
  C_Message *m = NULL;

  pthread_mutex_lock(&mutex);
  while (nb_msg < 1)
  {
#ifdef CIRCULAR_BUFFER_DEBUG
    printf("Thread %u cannot get a message: nb_msg (%i) < 1\n", pthread_self(),
        nb_msg);
#endif
    pthread_cond_wait(&cond_can_read, &mutex);
  }

#ifdef CIRCULAR_BUFFER_DEBUG
  printf("Thread %u can get a message: nb_msg (%i) >= 1\n", pthread_self(),
      nb_msg);
#endif

  m = cb[read_idx];
  cb[read_idx] = bcb_magic();
  read_idx = (read_idx + 1) % length;

  if (nb_msg-- == length)
  {
#ifdef CIRCULAR_BUFFER_DEBUG
    printf("Thread %u signals that messages can be added: nb_msg (%i) < length (%i)\n",
        pthread_self(), nb_msg, length);
#endif
    pthread_cond_signal(&cond_can_write);
  }

  pthread_mutex_unlock(&mutex);

#ifdef MSG_DEBUG
  if (++nb_read_messages == PERIODIC_THR_DISPLAY)
  {
    C_Time now = currentC_Time();
    float elapsed_in_sec = diffC_Time(now, read_start_time) / 1000000.0;
    fprintf(stderr, "[%s] read thr: %f msg/sec\n", name, nb_read_messages / elapsed_in_sec);
    nb_read_messages = 0;
    read_start_time = currentC_Time();
  }
#endif

  return m;
}
