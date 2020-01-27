/*
 * tcp_net.h
 *
 * Useful functions to use with the TCP sockets
 *
 *  Created on: 29 oct. 2010
 *      Author: pl
 */

#ifndef TCP_NET_H_
#define TCP_NET_H_

#ifdef __cplusplus
extern "C"
{
#endif

int recvMsg(int s, void *buf, size_t len);

void sendMsg(int s, void *msg, int size);

#ifdef __cplusplus
}
#endif

#endif /* TCP_NET_H_ */
