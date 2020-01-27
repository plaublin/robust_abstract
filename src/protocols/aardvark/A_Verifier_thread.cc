#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <netinet/tcp.h> // for TCP_NODELAY
#include "A_Wrapped_request.h"
#include "A_Request.h"
#include "A_Verifier_thread.h"
#include "A_parameters.h"
#include "tcp_net.h"
#include "A_Circular_buffer.h"

void *Verifier_thread_startup(void *_tgtObject)
{
  A_Verifier_thread *tgtObject = (A_Verifier_thread *) _tgtObject;
  //  fprintf(stderr, "Running a A_verifier thread object in a new thread\n");
  void *threadResult = tgtObject->run();
  //  fprintf(stderr, "Deleting object\n");
  delete tgtObject;
  return threadResult;
}

void *A_Verifier_thread::run(void)
{
  while (!A_replica)
  {
    fprintf(stderr, "replica not initialized yet...\n");
    sleep(1);
  }
  fprintf(stderr, "%s\n", message);

  //blacklist malloc and initialization
  blacklisted = (bool*) malloc(sizeof(bool) * A_node->num_principals);
  for (int i = 0; i < A_node->num_principals; i++)
    blacklisted[i] = false;

  // initialize, bind and set the client socket
  // (used to receive request from clients)

  // initialize the sockets for communications with clients
  A_replica->clients_socks = (int*) malloc(sizeof(int) * A_replica->num_clients());
  for (int i = 0; i < A_replica->num_clients(); i++)
  {
    A_replica->clients_socks[i] = -1;
  }

  bootstrap_clients_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (bootstrap_clients_socket == -1)
  {
    perror("Error while creating the socket! ");
    exit(errno);
  }

  // TCP NO DELAY
  int flag = 1;
  int result = setsockopt(bootstrap_clients_socket, IPPROTO_TCP, TCP_NODELAY,
      (char*) &flag, sizeof(int));

  // 2) bind on it
  bootstrap_clients_sin.sin_addr.s_addr = htonl(INADDR_ANY);
  bootstrap_clients_sin.sin_family = AF_INET;
  bootstrap_clients_sin.sin_port
      = htons(A_replica->replicas_ports[A_replica->id()]);

  if (bind(bootstrap_clients_socket, (struct sockaddr*) &bootstrap_clients_sin,
      sizeof(bootstrap_clients_sin)) == -1)
  {
    perror("Error while binding to the bootstrap clients socket! ");
    exit(errno);
  }

  // 3) make the socket listening for incoming connections
  if (listen(bootstrap_clients_socket, A_replica->num_clients() + 1) == -1)
  {
    perror("Error while calling listen! ");
    exit(errno);
  }

  fprintf(stderr, "Sockets for communications with clients initialized!\n");

  //start the main processing loop
  while (1)
  {
    select_timeout.tv_sec = 0;
    select_timeout.tv_usec = 10000000; //10ms, probably it could even be infinite...

    FD_ZERO(&fdset);

    int maxsock = bootstrap_clients_socket;
    FD_SET(bootstrap_clients_socket, &fdset);

    for (int i = 0; i < A_replica->num_clients(); i++)
    {
      if (A_replica->clients_socks[i] != -1)
      {
        FD_SET(A_replica->clients_socks[i], &fdset);
        maxsock = MAX(maxsock, A_replica->clients_socks[i]);
      }
    }

    select(maxsock + 1, &fdset, NULL, NULL, &select_timeout);

    /* get a new connection from a client */
    if (FD_ISSET(bootstrap_clients_socket, &fdset))
    {
      // accept the socket
      sockaddr_in csin;
      int sinsize = sizeof(csin);
      int client_sock = accept(bootstrap_clients_socket,
          (struct sockaddr*) &csin, (socklen_t*) &sinsize);

      if (client_sock == -1)
      {
        perror("An invalid socket has been accepted: ");
      }
      else
      {
        //TCP NO DELAY
        int flag = 1;
        int result = setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY,
            (char *) &flag, sizeof(int));

        int client_port = ntohs(csin.sin_port);
        char *hostname = inet_ntoa(csin.sin_addr);

        //printf("The client is %s:%i\n", hostname, client_port);

        int i = 0;
        while (i < A_replica->num_clients())
        {
          //fprintf(stderr, "A_Client %i is %s:%i\n", i, clients_ipaddr[i],
          //    clients_ports[i]);
          if (client_port == A_replica->clients_ports[i] && !strcmp(hostname,
              A_replica->clients_ipaddr[i]))
          {
            A_replica->clients_socks[i] = client_sock;
            break;
          }
          i++;
        }

        fprintf(
            stderr,
            "Received a new connection from a client: %s:%i. Should be client %i\n",
            hostname, client_port, i);

        if (i >= A_replica->num_clients())
        {
          fprintf(stderr, "Error: This client is unknown\n");
        }
        else
        {
          continue;
        }
      }
    }

    // is there a message from somebody?
    int rcv_client_sock = -1;
    for (int i = 0; i < A_replica->num_clients(); i++)
    {
      if (A_replica->clients_socks[i] != -1
          && FD_ISSET(A_replica->clients_socks[i], &fdset))
      {
        rcv_client_sock = A_replica->clients_socks[i];

        /****************************************************************/
        // yes there is: receive it and then process it
        if (rcv_client_sock != -1)
        {
            m = new A_Message(A_Max_message_size);

            // 1) first of all, we need to receive the A_Message_rep (in order to get the message size)
            int msg_rep_size = recvMsg(rcv_client_sock, (void*) m->contents(),
                    sizeof(A_Message_rep));

            // 2) now that we have the size of the message, receive the content
            int msg_size = recvMsg(rcv_client_sock,
                    (void*) ((char*) m->contents() + msg_rep_size),
                    m->size() - msg_rep_size);

            int ret = msg_rep_size + msg_size;

            if (ret >= (int) sizeof(A_Message_rep) && ret >= m->size())
            {
                if ((m->tag() < 1) || ((m->tag() > 15) && (m->tag() != 18)))
                {
                    /*
                       fprintf(stderr, "A_Replica %i handles a non-valid message: %i\n",
                       id(), m->tag());
                       */
                    delete m;
                }
                else
                {
                    if (m->tag() == A_Wrapped_request_tag)
                    {
                        // the tag is correct, verify the wrapped request
                        A_Request* req = A_replica->verify((A_Wrapped_request*) m);

                        if (req)
                        {
#ifdef MSG_DEBUG
                            //fprintf(stderr, "[Verifier] A_Replica %i handles a valid Wrapped request\n", A_replica->id());
#endif

                            // old code which uses the original circular buffer
#if 0
                            //sendto(server_socket, req.contents(),req.size(),0,(struct sockaddr*)&dest_address, sizeof(Addr));
                            if (!A_replica->cb_write_message((A_Message*) req))
                            {
                                //fprintf(stderr, "Unable to write request in the buffer \n");
                            }
#endif

                            bool w = A_replica->verifier_thr_to_replica_buffer->cb_write_msg(
                                    (A_Message*) req);

                            // buffer is full. Delete the request
                            if (!w) {
                                delete req;
                            }

                            // m is deleted in verify(m)
                            //delete m;
                        }
                    }
                    else if (m->tag() == A_New_key_tag)
                    {
                        //fprintf(stderr, "A_Replica %i, View %qd, received a A_New_key msg.\n", id(),
                        //   ((A_Node*) this)->view());
                        ((A_New_key*) m)->verify();

                        delete m;
                        /*
                        //sending the message to the main thread
                        if (!A_replica->cb_write_message(m))
                        {
                        //fprintf(stderr, "Unable to write request in the buffer \n");
                        delete m;
                        }
                        */
                    }
                    else
                    {
                        delete m;
                    }
                }
            }
            else
            {
                delete m;
            }
        }
        /****************************************************************/
      }
    }

  }

  return NULL;
}

