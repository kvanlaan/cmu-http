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
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>

#include <parse_http.h>
#include <test_error.h>
#include <ports.h>
#include <poll.h>

#include <errno.h>

#define BUF_SIZE 999999
#define MAX_PARALLEL_CONNS 10

static struct {
  char server_ip[40];

  uint32_t n_resources;  // # resources in request
  char** resources;  // holds paths of all the resources being requested
                     // if NULL at some index i, then no conn info should have
                     // it instream, and it is available for a new resource;

  struct pollfd connections[MAX_PARALLEL_CONNS];
  struct conn_info {
    // char path[4096];
    uint32_t n_instream;
    uint32_t* resources;
    // in theory there might also be a timeout field here too
  } conn_info_list[MAX_PARALLEL_CONNS];
  // size_t conn_sizes[MAX_PARALLEL_CONNS];  // how many requests await in each?
                           // note: can assume will be answered in-order
} client_data;


static void free_client_data() {
  for(uint32_t i = 0; i < MAX_PARALLEL_CONNS; i++)
    free(client_data.conn_info_list[i].resources);
  for(uint32_t i = 0; i < client_data.n_resources; i++)
    free(client_data.resources[i]);
  free(client_data.resources);
}


static int new_connection() {
  printf("new_connection() called!\n");  // note: this is merely debug stmnt
  uint32_t conn_i = 0;
  for(; (conn_i < MAX_PARALLEL_CONNS) && (client_data.connections[conn_i].fd != -1);
      conn_i++) {}
  if(conn_i == MAX_PARALLEL_CONNS) {
    printf("cannot make new connection: amnt exceeded\n");  // note: this is merely debug stmnt
    return MAX_PARALLEL_CONNS;
  }

  int sockfd;
  struct sockaddr_in sin;
  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
      return TEST_ERROR_HTTP_CONNECT_FAILED;
  }

  int optval = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int));
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(int));

  sin.sin_family = AF_INET;
  sin.sin_port = htons(HTTP_PORT);
  inet_pton(AF_INET, client_data.server_ip, &(sin.sin_addr));
  
  fprintf(stderr, "Parsed IP address of the server: %X\n", htonl(sin.sin_addr.s_addr));

  if(connect(sockfd, (struct sockaddr *)&sin, sizeof(sin)) < 0){
      printf("error on connect\n");
      return TEST_ERROR_HTTP_CONNECT_FAILED;
  }

  printf("new connection created with sockfd %d!\n", sockfd);  // note: this is merely debug stmnt

  client_data.connections[conn_i].fd = sockfd;
  return conn_i;
}




/* assumes strlen(path) < 4096 & starts with '/' */
/* makes a new connection if possible & all current conns have something instream */
static int req_resource(const char *path, size_t len) {
  printf("called req_resource() with path %s, len %d\n", path, (int)len);
  /* check if resource already requested */
  for(uint32_t i = 0; i < client_data.n_resources; i++) {
    if(strcmp(client_data.resources[i], path) == 0)
      return 0;
  }
  /* add the resource to the resource list */
  client_data.resources = realloc(client_data.resources,
      (client_data.n_resources + 1)*sizeof(char*));
  char *path_str = malloc(sizeof(char*)*len);
  memcpy(path_str, path, len);
  uint32_t rsrc_i = client_data.n_resources;
  client_data.resources[rsrc_i] = path_str;
  client_data.n_resources++;

  /* find the connection with the least instream */
  uint32_t min_instream = (uint32_t)-1;
  uint32_t conn_i = MAX_PARALLEL_CONNS;
  for(uint32_t i = 0; i < MAX_PARALLEL_CONNS; i++) {
    if(client_data.connections[i].fd == -1) {
      continue;
    }
    uint32_t n_instream_i = client_data.conn_info_list[i].n_instream;
    if(n_instream_i < min_instream) {
      min_instream = n_instream_i;
      conn_i = i;
    }
  }
  if(min_instream > 0) {
    uint32_t new_conn_i = new_connection();
    if(new_conn_i < MAX_PARALLEL_CONNS) {
      conn_i = new_conn_i;
    }
  }

  Request request = {
    "1.1",
    "GET",
    "/",
    "127.0.0.1",
    NULL,
    0,
    0,
    0,
    NULL,
    1,
    0
  };
  memcpy(request.http_uri + 1, path, len);

  char buf[MAX_HEADER_SIZE];
  size_t size = 0;
  int err = serialize_http_request(buf, &size, &request);
  if(err != TEST_ERROR_NONE) {
    printf("error %d serializing request\n", err);
    return -1;
  }
  printf("CLIENT RSRC REQ len %d: \n%s\n", size, buf);
  err = send(client_data.connections[conn_i].fd, buf, size, 0);
  if(err < 0) {
    printf("error sending msg\n");
    return -1;
  }
  
  /* update the connection's pipeline log */
  struct conn_info *conn_info = &(client_data.conn_info_list[conn_i]);
  uint32_t n_instream = conn_info->n_instream;
  conn_info->resources = realloc(conn_info->resources, n_instream + 1);
  conn_info->resources[n_instream] = rsrc_i;
  conn_info->n_instream++;

  return 1;
}



int main(int argc, char *argv[]) {
  /* Validate and parse args */
  if (argc != 2) {
      fprintf(stderr, "usage: %s <server-ip>\n", argv[0]);
      return EXIT_FAILURE;
  }
  
  char *ip = argv[1];

  /* initialize client_data stuff */
  strcpy(client_data.server_ip, ip);
  client_data.n_resources = 0;
  for(uint32_t i = 0; i < MAX_PARALLEL_CONNS; i++) {
    client_data.conn_info_list[i].n_instream = 0;
    client_data.conn_info_list[i].resources = NULL;
  }
  for(uint32_t i = 0; i < MAX_PARALLEL_CONNS; i++) {
    client_data.connections[i].fd = -1;
    client_data.connections[i].events = POLLIN | POLLHUP;
    client_data.connections[i].revents = 0;
  }

  /* request the dependency (should go in connection 0) */
  const char *dep_str = "dependency.csv";
  int requested = req_resource(dep_str, strlen(dep_str) + 1);
  printf("requested? %d\n", requested);
  int sockfd = client_data.connections[0].fd;
  printf("sockfd is %d\n", sockfd);

  char buf[BUF_SIZE];
  int len = read(sockfd, buf, BUF_SIZE);
  printf("read dependency: %s\n", buf);
  if(len == 0) { 
    printf("read of zero\n");
    exit(1);
  }
  // sleep(10);
  // char *start_body = strstr(buf, "index.html");
  // printf("body is: %s\n", start_body + 4);
  // printf("chars are: %d,%d,%d,%d,%d\n", (int)*(start_body - 5), (int)*(start_body - 4), (int)*(start_body - 3), (int)*(start_body - 2), (int)*(start_body - 1));
  printf("parsing dependency.csv...\n");
  buf[len] = '\0';
  char *start_body = strstr(buf, "\r\n\r\n");
  char *body_end = start_body + strlen(start_body);
  char *line = start_body + 3;
  while (line++ < body_end - 1)
  {
    char *next_line = strstr(line, "\n");
    if(next_line == NULL)
      next_line = body_end;
    if(next_line != NULL)
      *next_line = '\0';
    printf("LINE:\n", (int)(next_line - line), line);
    char *file = line;
    char *com = strstr(line, ",");
    if(com == NULL)
      com = next_line;
    while(1) {
      *com = '\0';
      printf("\tFILE: %s\n", file);
      req_resource(file, (int)(com - file) + 3);
      file = com + 1;
      if(file >= next_line)
        break;
      com = strstr(file, ",");
      if(com == NULL)
        com = next_line;
    }
    line = next_line;
  }
  sleep(5);

  free_client_data();
  return 0;
}

