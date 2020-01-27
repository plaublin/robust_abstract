#ifndef _TCP_network_h
#define _TCP_network_h 1

#define NIPQUAD(addr)   ((unsigned char *)&addr)[0], \
                            ((unsigned char *)&addr)[1], \
                        ((unsigned char *)&addr)[2], \
                        ((unsigned char *)&addr)[3]

#define NIPQUADi(addr, i)   ((unsigned char *)&addr)[i]

int createClientSocket(struct sockaddr_in addr);
int createClientSocketBind(struct sockaddr_in caddr, struct sockaddr_in addr);

int createNonBlockingServerSocket(int port_no);

int createServerSocket(int in_port);

int recv_all_blocking(int s, void *buf, size_t len, int flags);

int recv_all_non_blocking(int s, void *buf, size_t len, int flags);

int send_all(int s, void *buf, int *len);

void setnonblocking(int sock);

#endif
