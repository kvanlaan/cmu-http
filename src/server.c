/**
 * Copyright (C) 2022 Carnegie Mellon University
 *
 * This file is part of the HTTP course project developed for
 * the Computer Networks course (15-441/641) taught at Carnegie
 * Mellon University.
 *
 * No part of the HTTP project may be copied and/or distributed
 * without the express permission of the 15-441/641 course staff.
 */
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <arpa/inet.h>

#include "parse_http.h"
#include "ports.h"
#include <poll.h>


#include <limits.h>

#define BUF_SIZE 8192
// Closes a client's connection if they have not sent a valid request within
// CONNECTION_TIMEOUT seconds.
#define CONNECTION_TIMEOUT 50

#define MAX_CONCURRENT_CONNS 100

#define HOSTLEN 256
#define SERVLEN 8

#define DEFAULT_TIMEOUT 3000

struct client_info
{
    struct sockaddr_in addr; // Socket address
    socklen_t addrlen;       // Socket address length
    int connfd;              // Client connection file descriptor
    char host[HOSTLEN];      // Client host
    char serv[SERVLEN];      // Client service (port)
};



#define ERR(msg, __VA_ARGS__) if(__VA_ARGS__) {\
  fprintf(stderr, msg);\
  exit(1);\
}
#define CHK(msg, __VA_ARGS__) if(__VA_ARGS__) {\
  printf(msg);\
  return -1;\
}



int new_connection(int sockfd, struct pollfd *poll_list,
    struct client_info *client_info_list) {
  printf("received connection!\n");
  struct sockaddr_in client_addr;
  socklen_t client_addrlen = sizeof(client_addr);
  int client_sockfd = accept(sockfd, (struct sockaddr*)&client_addr,
      &client_addrlen);
  if(client_sockfd < 0) {
    // skip
    printf("coult not accept new connection\n");
    return -1;
  }
  size_t i = 0;
  for(; (i < MAX_CONCURRENT_CONNS) && (poll_list[i].fd >= 0);
      i++) {}
  if(i == MAX_CONCURRENT_CONNS) {
    // send 503
    printf("new connection, but too many existing -- sending 503\n");
    char *msg;
    size_t msg_len;
    serialize_http_response(&msg, &msg_len, "503 Service Unavailable\n",
      NULL, NULL, NULL, 0, NULL);
    int err = send(client_sockfd, msg, msg_len, 0);
    ERR("could not send HTTP 400\n", err < 0)
    return 0;
  }

  // new connection at location i in list
  struct pollfd *client_pollfd = &(poll_list[i]);
  client_pollfd->fd = client_sockfd;
  client_pollfd->events = POLLIN;
  client_pollfd->revents = 0;
  struct client_info *client_info = &(client_info_list[i]);
  client_info->addr = client_addr;
  client_info->addrlen = client_addrlen;
  client_info->connfd = client_sockfd;

  char *ip = inet_ntoa(client_addr.sin_addr);
  printf("new connection successfully set up from %s fd %d at %d!\n", ip, client_sockfd, i);

  return 1;
}


// assumes http request is valid
// returns proper HTTP response
char* process_http_request(Request *request, size_t *len) {
  return NULL;
}

int main(int argc, char *argv[])
{
  /* Validate and parse args */
  if (argc != 2)
  {
      fprintf(stderr, "usage: %s <www-folder>\n", argv[0]);
      return EXIT_FAILURE;
  }

  char *www_folder = argv[1];

  DIR *www_dir = opendir(www_folder);
  if (www_dir == NULL)
  {
      fprintf(stderr, "Unable to open www folder %s.\n", www_folder);
      return EXIT_FAILURE;
  }

  closedir(www_dir);
  printf("setting up socket.. \n");
  /* CP1: Set up sockets and read the buf */
  int err;

  /* Set up socket, sockaddr_in, poll list */
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  ERR("couldn't make server socket\n", (sockfd < 0));
  int optval = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

  struct sockaddr_in sin;
  bzero((char*)&sin, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = INADDR_ANY;
  sin.sin_port = htons(HTTP_PORT);

  err = bind(sockfd, (struct sockaddr*)&sin, sizeof(sin));
  ERR("couldn't bind\n", (err < 0));
  listen(sockfd, 100000);

  // validity in the lists is based on whether the corresponding entry in
  //  poll_list has pollfd != -1
  struct pollfd poll_list[MAX_CONCURRENT_CONNS + 1];  // extra is for server
  for(size_t i = 0; i < MAX_CONCURRENT_CONNS; i++) {
    poll_list[i].fd = -1;
    poll_list[i].events = POLLIN | POLLHUP | POLLERR;
    poll_list[i].revents = 0;
  }
  struct client_info client_info_list[MAX_CONCURRENT_CONNS];

  struct pollfd *my_pollfd = &(poll_list[MAX_CONCURRENT_CONNS]);
  my_pollfd->fd = sockfd;
  my_pollfd->events = POLLIN;
  my_pollfd->revents = 0;

  while (1) {
    /* check for new connections */
    int n_ready = poll(poll_list, MAX_CONCURRENT_CONNS + 1, DEFAULT_TIMEOUT);
    if(n_ready == 0) {
      // printf("nothing so far!\n");
      printf("nothing so far %d\n", poll_list[0].fd);
    }

    if(my_pollfd->revents & POLLIN) {
      n_ready--;
      // I'm hoping poll() will reset this if there are multiple connections
      // pending
      my_pollfd->revents = 0;

      new_connection(sockfd, poll_list, client_info_list);
      if(n_ready == 0)
        continue;
    } else if(my_pollfd->revents != 0) {
      printf("weird server socket file state\n");
    }
    printf("%d events!\n", n_ready);
    
    
    for (int i = 0; i < MAX_CONCURRENT_CONNS; i++)
    {
      struct pollfd* pollfd = &(poll_list[i]);
      if(pollfd->fd < 0)
        continue;
      int revents = pollfd->revents;
      printf("connfd is %d, revents is %d\n", pollfd->fd, revents);

      pollfd->revents = 0;
      if((revents & POLLIN) == 0)
        continue;

      struct client_info client_info = client_info_list[i];
      char buf[BUF_SIZE];
      int len = recv(client_info.connfd, buf, BUF_SIZE,
                     MSG_DONTWAIT | MSG_PEEK);
      if(len < 0) { printf("couldn't receive data from %d: %s\n", client_info.connfd, strerror(errno)); continue; }

      int shutdown = (len == 0) || ((revents & POLLHUP) > 0);
      if(shutdown) {  // according to spec, when recv() returns 0, client has either shutdown or sent a zero-length datagram. We don't permit the latter, so this means we shutdown
        printf("closing connection with fd %d\n", client_info.connfd);
        pollfd->fd = -1;
        continue;
      }

      Request request;
      err = parse_http_request(buf, len, &request);
      if(err == TEST_ERROR_PARSE_PARTIAL) { printf("parsing partial'ed %d bytes\n", len); continue;  }
      // TODO send 503
      if(err == TEST_ERROR_PARSE_FAILED) {
        printf("parsing failed, sending HTTP 400\n");
        // shift the socket recv buffer
        err = recv(client_info.connfd, buf, request.status_header_size, MSG_DONTWAIT);
        ERR("could not shift buffer\n", (err < 0))
        // send HTTP 400
        char *msg;
        size_t msg_len;
        serialize_http_response(&msg, &msg_len, "400 Bad Request\n",
          NULL, NULL, NULL, 0, NULL);
        err = send(client_info.connfd, msg, msg_len, 0);
        ERR("could not send HTTP 400\n", (err < 0))
        continue;
      }
      // shift the socket recv buffer
      err = recv(client_info.connfd, buf, request.status_header_size,
                     MSG_DONTWAIT);
      if(err < 0) {
        printf("coulnd't shift buffer 2: %s\n", strerror(errno));
      }
            {
              char buffer[BUF_SIZE];
              size_t size;
              serialize_http_request(buffer, &size, &request);
              buffer[size] = '\0';
              printf("received HTTP request:\n%s, read in %d bytes\n", buffer, len);
              printf("status_header_size is %d\n", request.status_header_size);
              printf("buffer size is %d\n", size);
            }

      size_t resp_len;
      char *resp = process_http_request(&request, &resp_len);
      err = send(client_info.connfd, resp, resp_len, 0);
      if(err < 0) {
        printf("could not send HTTP response: %s\n", strerror(errno));
      }
    }

  }
}
