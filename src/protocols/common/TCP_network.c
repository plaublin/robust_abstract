#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <memory.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <fcntl.h>
#include "TCP_network.h"

int createNonBlockingServerSocket(int port_no)
{
	struct sockaddr_in addr;
	// Used so we can re-bind to our port while a previous connection is still
	// in TIME_WAIT state
	int reuse_addr = 1;
	int so_rcvlowat = -1;
	int so_sndlowat = -1;
	int tcp_nodelay = -1;
	socklen_t res_size2;
	socklen_t res_size3;
	int flag;
	int result;

	// Create server socket
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0)
	{
		perror("Cannot open socket");
		return -1;
	}

	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr));

	setnonblocking(sock);

	res_size2 = sizeof(so_rcvlowat);
	if (getsockopt(sock, SOL_SOCKET, SO_RCVLOWAT, (char *) &so_rcvlowat,
			&res_size2) == 0)
	{
		fprintf(stderr, "Socket SO_RCVLOWAT : %d \n", so_rcvlowat);
	} else
	{
		perror("Cannot get socket SO_RCVLOWAT");
		exit(-1);
	}

	res_size3 = sizeof(so_sndlowat);
	if (getsockopt(sock, SOL_SOCKET, SO_SNDLOWAT, (char *) &so_sndlowat,
			&res_size3) == 0)
	{
		fprintf(stderr, "socket SO_SNDLOWAT : %d \n", so_sndlowat);
	} else
	{
		perror("Cannot get socket SO_SNDLOWAT");
		exit(-1);
	}

	res_size3 = sizeof(tcp_nodelay);
	if (getsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *) &tcp_nodelay,
			&res_size3) == 0)
	{
		fprintf(stderr, "socket TCP_NODELAY : %d \n", tcp_nodelay);
	} else
	{
		perror("Cannot get socket TCP_NODELAY");
		exit(-1);
	}

	flag = 1;
	result = setsockopt(sock, /* socket affected */
	IPPROTO_TCP, /* set option at TCP level */
	TCP_NODELAY, /* name of option */
	(char *) &flag, /* the cast is historical
	 cruft */
	sizeof(int));

	if (getsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *) &tcp_nodelay,
			&res_size3) == 0)
	{
		fprintf(stderr, "socket TCP_NODELAY after : %d \n", tcp_nodelay);
	} else
	{
		perror("Cannot get socket TCP_NODELAY");
		exit(-1);
	}

	// Bind server socket
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port_no);

	if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0)
	{
		perror("Cannot bind port");
		return -1;
	}
	return sock;
}

int createClientSocket(struct sockaddr_in addr)
{
	int cs;
	int flag;
	int result;

	// Try until the connection has been succesfull
	while (1)
	{
		fprintf(stderr, "Trying to connect to %x:%d\n", addr.sin_addr, addr.sin_port);
		cs = socket(AF_INET, SOCK_STREAM, 0);
		if (cs < 0) {
		    perror("client socket:");
		    exit(EXIT_FAILURE);
		}

		flag = 1;
		result = setsockopt(cs, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int));
		result = setsockopt(cs, IPPROTO_TCP, SO_REUSEADDR, (char *) &flag, sizeof(int));
		result = setsockopt(cs, IPPROTO_TCP, SO_KEEPALIVE, (char *) &flag, sizeof(int));

		if (connect(cs, (struct sockaddr *) &(addr), sizeof(addr)) < 0)
		{
		    fprintf(stderr, "Unix really sux\n");
			struct pollfd unix_really_sucks;
			int some_more_junk;
			socklen_t yet_more_useless_junk;

		    if ( errno != EINTR && errno != EINPROGRESS && errno != EALREADY ) {
				perror("Cannot connect");
				close(cs);
				sleep(1);
				continue;
				exit (EXIT_FAILURE);
			}
			unix_really_sucks.fd = cs;
			unix_really_sucks.events = POLLOUT;
			while ( poll (&unix_really_sucks, 1, -1) == -1 )
				if ( errno != EINTR )
				{
					perror ("poll");
					exit (EXIT_FAILURE);
				}
			yet_more_useless_junk = sizeof(some_more_junk);
			if ( getsockopt (cs, SOL_SOCKET, SO_ERROR,
						&some_more_junk,
						&yet_more_useless_junk) == -1 )
			{
				perror ("getsockopt");
				exit (EXIT_FAILURE);
			}
			if ( some_more_junk != 0 )
			{
				fprintf (stderr, "connect: %s\n",
						strerror (some_more_junk));
				close(cs);
				sleep(1);
				continue;
				exit (EXIT_FAILURE);
			}
			/*close(cs);*/
			return cs;
			/*sleep(1);*/
		} else
		{
			fprintf(stderr,
			"TCP socket connected towards next process in the ring (%d)\n", cs);
			return cs;
		}
	}
}

int createClientSocketBind(struct sockaddr_in caddr, struct sockaddr_in addr)
{
	int cs;
	int flag;
	int result;

	// Try until the connection has been succesfull
	while (1)
	{
		fprintf(stderr, "Trying to connect to %x:%d\n", addr.sin_addr, addr.sin_port);
		cs = socket(AF_INET, SOCK_STREAM, 0);
		if (cs < 0) {
		    perror("client socket:");
		    exit(EXIT_FAILURE);
		}

		flag = 1;
		result = setsockopt(cs, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int));
		result = setsockopt(cs, IPPROTO_TCP, SO_REUSEADDR, (char *) &flag, sizeof(int));
		result = setsockopt(cs, IPPROTO_TCP, SO_KEEPALIVE, (char *) &flag, sizeof(int));

		flag = 1;
        result = setsockopt(cs, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
        caddr.sin_addr.s_addr = htonl(INADDR_ANY);
        caddr.sin_family = AF_INET;
        if (bind(cs, (struct sockaddr*) &caddr, sizeof(caddr)) == -1)
        {
            perror("Error while binding to the socket! ");
		    exit(EXIT_FAILURE);
        }

		if (connect(cs, (struct sockaddr *) &(addr), sizeof(addr)) < 0)
		{
		    fprintf(stderr, "Unix really sux\n");
			struct pollfd unix_really_sucks;
			int some_more_junk;
			socklen_t yet_more_useless_junk;

		    if ( errno != EINTR && errno != EINPROGRESS && errno != EALREADY ) {
				perror("Cannot connect");
				close(cs);
				sleep(1);
				continue;
				exit (EXIT_FAILURE);
			}
			unix_really_sucks.fd = cs;
			unix_really_sucks.events = POLLOUT;
			while ( poll (&unix_really_sucks, 1, -1) == -1 )
				if ( errno != EINTR )
				{
					perror ("poll");
					exit (EXIT_FAILURE);
				}
			yet_more_useless_junk = sizeof(some_more_junk);
			if ( getsockopt (cs, SOL_SOCKET, SO_ERROR,
						&some_more_junk,
						&yet_more_useless_junk) == -1 )
			{
				perror ("getsockopt");
				exit (EXIT_FAILURE);
			}
			if ( some_more_junk != 0 )
			{
				fprintf (stderr, "connect: %s\n",
						strerror (some_more_junk));
				close(cs);
				sleep(1);
				continue;
				exit (EXIT_FAILURE);
			}
			/*close(cs);*/
			return cs;
			/*sleep(1);*/
		} else
		{
			fprintf(stderr,
			"TCP socket connected towards next process in the ring (%d)\n", cs);
			return cs;
		}
	}
}

int createServerSocket(int in_port)
{
	struct sockaddr_in addr;
	// Used so we can re-bind to our port while a previous connection is still
	// in TIME_WAIT state
	int reuse_addr = 1;
	int ss;
	int so_rcvlowat = -1;
	int so_sndlowat = -1;
	socklen_t res_size2;
	socklen_t res_size3;

	// Create server socket
	ss = socket(AF_INET, SOCK_STREAM, 0);
	if (ss < 0)
	{
		perror("Cannot open socket");
		return -1;
	}

	res_size2 = sizeof(so_rcvlowat);

	setsockopt(ss, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr));

	if (getsockopt(ss, SOL_SOCKET, SO_RCVLOWAT, (char *) &so_rcvlowat,
			&res_size2) == 0)
	{
		fprintf(stderr, "Socket SO_RCVLOWAT : %d \n", so_rcvlowat);
	} else
	{
		perror("Cannot get socket SO_RCVLOWAT");
		exit(-1);
	}

	res_size3 = sizeof(so_sndlowat);
	if (getsockopt(ss, SOL_SOCKET, SO_SNDLOWAT, (char *) &so_sndlowat,
			&res_size3) == 0)
	{
		fprintf(stderr, "Socket SO_SNDLOWAT : %d \n", so_sndlowat);
	} else
	{
		perror("Cannot get socket SO_SNDLOWAT");
		exit(-1);
	}

	/* bind server socket */
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(in_port);

	if (bind(ss, (struct sockaddr *) &(addr), sizeof(addr)) < 0)
	{
		perror("Cannot bind port");
		return -1;
	}

	fprintf(stderr, "Server socket created with port %d\n", in_port);
	return ss;
}

int recv_all_blocking(int s, void *buf, size_t len, int flags)
{
	int len_tmp = 0;
	int n;
	do
	{
		n = recv(s, &(((char *) buf)[len_tmp]), len - len_tmp, 0);
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

int recv_all_non_blocking(int s, void *buf, size_t len, int flags)
{
	int len_tmp = 0;
	int n;
	do
	{
		if (len_tmp > 0)
		{
			//fprintf(stderr, "len_tmp = %d\n", len_tmp);
		}
		n = recv(s, &(((char *) buf)[len_tmp]), len - len_tmp, 0);
		if (n == -1)
		{
			//      perror("Recv_all: ");
			//      return -1;
			continue;
		}
		len_tmp = len_tmp + n;
	} while (len_tmp < len);
	return len_tmp;
}

int send_all(int s, void *buf, int *len)
{
	int total = 0; // how many bytes we've sent
	int bytesleft = *len; // how many we have left to send
	int n = -1;

	while (total < *len)
	{
		n = send(s, buf + total, bytesleft, 0);
		if (n == -1)
		{
			continue;
		}
		total += n;
		bytesleft -= n;
	}

	*len = total; // return number actually sent here

	return n == -1 ? -1 : 0; // return -1 on failure, 0 on success
}

void setnonblocking(int sock)
{
	int opts;

	opts = fcntl(sock, F_GETFL);
	if (opts < 0)
	{
		perror("fcntl(F_GETFL)");
		exit(EXIT_FAILURE);
	}
	opts = (opts | O_NONBLOCK);
	if (fcntl(sock, F_SETFL, opts) < 0)
	{
		perror("fcntl(F_SETFL)");
		exit(EXIT_FAILURE);
	}
	return;
}

