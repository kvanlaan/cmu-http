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

// setupsocket
// returns server_fd to poll for new connections
// returns -1 on error
static int setup_socket()
{
    int server_fd = 0;
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        fprintf(stderr, "socket failed\n");
        exit(1);
    }

    int optval_pos = 1;

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &optval_pos, sizeof(optval_pos)))
    {
        fprintf(stderr, "setsockopt reuse failed\n");
        close(server_fd);
        exit(1);
    }

    struct sockaddr_in address;

    address.sin_family = AF_INET;
    // address.sin_addr.s_addr = inet_addr("0.0.0.0"); // DON't USE 127..., restricts external clients
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(HTTP_PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) != 0)
    {
        fprintf(stderr, "bind failed: %s\n", strerror(errno));
        if (close(server_fd) < 0)
          fprintf(stderr, "fd socket not properly closed %s\n", strerror(errno));
        exit(1);
        // return -1;
    }

    // if (listen(server_fd, MAX_CONCURRENT_CONNS) != 0)
    if (listen(server_fd, INT_MAX) != 0)
    {
        fprintf(stderr, "listen failed: %s\n", strerror(errno));
        // pull out this close method
        if (close(server_fd) < 0)
          fprintf(stderr, "fd socket not properly closed %s\n", strerror(errno));
        exit(1);
    }

    return server_fd;
}


// init_poll_list
// mallocs poll_list set to length of max connections
// returns pointer to malloced poll_list
static struct pollfd *init_poll_list(int server_fd)
{
    struct pollfd *poll_list = malloc(MAX_CONCURRENT_CONNS * sizeof(struct pollfd));
    for (int i = 0; i < MAX_CONCURRENT_CONNS; i++)
    {
        poll_list[i].fd = -1;
        if (i == 0)
        {
            poll_list[0].fd = server_fd;
            printf("server_fd: %d\n", server_fd);
        }
        poll_list[i].events = POLLIN;
    }
    return poll_list;
}

static int add_new_connection(struct pollfd *poll_list, int server_fd)
{
    struct sockaddr_in client_address;
    socklen_t client_addr_len = sizeof(client_address);
    int client_fd = accept(server_fd, (struct sockaddr *)&client_address, &client_addr_len);
    if (client_fd < 0)
    {
        fprintf(stderr, "accept failed: %s\n", strerror(errno));
        return -1;
    }

    for (int i = 0; i < MAX_CONCURRENT_CONNS; i++)
    {
        if (poll_list[i].fd == -1)
        {
            poll_list[i].fd = client_fd;
            break;
        }
    }
    return 0;
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
    return -1;
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
  printf("new connection successfully set up from %s fd %d!\n", ip, client_sockfd);

  return 0;
}


#define ERR(msg, __VA_ARGS__) if(__VA_ARGS__) {\
  fprintf(stderr, msg);\
  exit(1);\
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
    poll_list[i].events = POLLIN;
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
    } else if(my_pollfd->revents != 0) {
      printf("weird server socket file state\n");
      // ERR("weird server socket file state\n", (my_pollfd->revents != 0))
    }
    
    
    for (int i = 0; i < MAX_CONCURRENT_CONNS; i++)
    {
      struct pollfd pollfd = poll_list[i];

      if((pollfd.revents & POLLIN) == 0)
        continue;

      // pollfd.revents = 0;
      struct client_info client_info = client_info_list[i];
      char buf[BUF_SIZE];
      printf("connfd is %d\n", client_info.connfd);
      int len = recv(client_info.connfd, buf, BUF_SIZE,
                     MSG_DONTWAIT | MSG_PEEK);
      if(len < 0) {
        printf("couldn't receive data from %d: %s\n", client_info.connfd,
            strerror(errno));
        continue;
      }
      // ERR("couldn't receive data\n", (len < 0));
      Request request;
      err = parse_http_request(buf, len, &request);
      if(err == TEST_ERROR_PARSE_PARTIAL) {
        printf("parsing partial'ed\n");
        continue;  // socket recv buffer is not ready
      }
      if(err == TEST_ERROR_PARSE_FAILED) {
        printf("parsing failed\n");
        recv(request.status_header_size, buf, len,
                       MSG_DONTWAIT);
        continue;
      }
      printf("received HTTP request from %s!\n", request.host);
      // if(err != TEST_ERROR_NONE) {
      //   printf("weird parse error code\n");
      //   continue;
      // }
      recv(request.status_header_size, buf, len,
                     MSG_DONTWAIT);
    }

  }
}
