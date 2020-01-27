/*
 * tcp_net.c
 *
 *  Created on: 29 oct. 2010
 *      Author: pl
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>
#include <errno.h>

#include "A_ITimer.h"
#include "tcp_net.h"

int recvMsg(int s, void *buf, size_t len)
{
  int len_tmp = 0;
  int n;
  do
  {
    n = recv(s, &(((char *) buf)[len_tmp]), len - len_tmp, /*MSG_DONTWAIT*/0);
    if (n == -1 && (errno == EAGAIN || errno == EINTR || errno == EWOULDBLOCK))
    {
        continue;
    }
    if (n == -1)
    {
        perror("recv_all_blocking:");
        return n;
    }
    if (n == 0 /*&& len_tmp == 0*/)
    {
        //         fprintf(stderr, "Shutdown socket %d: len_tmp=%d\n", s, len_tmp);
        return 0;
    }
    len_tmp = len_tmp + n;
  } while (len_tmp < len);

  return len_tmp;
}

void sendMsg(int s, void *msg, int size)
{
  int total = 0; // how many bytes we've sent
  int bytesleft = size; // how many we have left to send
  int n = -1;

  while (total < size)
  {
    n = send(s, (char*) msg + total, bytesleft, /*MSG_DONTWAIT*/0);
    if (n == -1)
    {
      continue;
    }
    total += n;
    bytesleft -= n;
  }
}
