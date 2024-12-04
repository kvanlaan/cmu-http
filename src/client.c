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

#include <errno.h>

#define BUF_SIZE 999999

int make_request(char *buf, int sockfd, char *filename)
{
  // to-do set filename_param to '/' + filename

  Request request = {
      "1.1",
      "GET",
      "/dependency.csv",
      "127.0.0.1",
      NULL,
      0,    // header_count
      0,    // allocated_headers
      0,    // status_header_size
      NULL, ///  char *body;
      1,    // valid
      0     //
  };

  snprintf(request.http_uri, sizeof(request.http_uri), "/%s", filename);

  size_t size;
  int err = serialize_http_request(buf, &size, &request);
  if (err != TEST_ERROR_NONE)
  {
    printf("error serializing request\n");
    return -1;
  }
  buf[size] = '\0';

  err = send(sockfd, buf, size, 0);
  if (err < 0)
  {
    printf("error sending msg\n");
    return -1;
  }

  int len = read(sockfd, buf, BUF_SIZE);
  printf("len received%d\n", len);

  return len;
}
int main(int argc, char *argv[])
{
  /* Validate and parse args */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <server-ip>\n", argv[0]);
    return EXIT_FAILURE;
  }

  /* Set up a connection to the HTTP server */
  int sockfd;
  struct sockaddr_in sin;
  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    return TEST_ERROR_HTTP_CONNECT_FAILED;
  }

  int optval = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int));
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(int));

  sin.sin_family = AF_INET;
  sin.sin_port = htons(HTTP_PORT);
  inet_pton(AF_INET, argv[1], &(sin.sin_addr));

  fprintf(stderr, "Parsed IP address of the server: %X\n", htonl(sin.sin_addr.s_addr));

  if (connect(sockfd, (struct sockaddr *)&sin, sizeof(sin)) < 0)
  {
    printf("error on connect\n");
    return TEST_ERROR_HTTP_CONNECT_FAILED;
  }

  /* CP1: Send out a HTTP request, waiting for the response */
  char buf[BUF_SIZE];
  int len = make_request(buf, sockfd, "dependency.csv");
  if (len > 0)
  {
    buf[len] = '\0';
    char *start_body = strstr(buf, "index.html");
    char *dep_entry = strtok(start_body, "\r\n");
    printf("FIX THIS extract loop");
    while (dep_entry != NULL)
    {
      printf("dep_entry\n %s\n", dep_entry);
      char *filename = strtok(NULL, ",");
      printf("filename extract\n %s\n", filename);
      printf("making request....\n");
      char temp_buf[BUF_SIZE];
      make_request(temp_buf, sockfd, filename);
      dep_entry = strtok(NULL, "\r\n");
      printf("dep_entry\n %s\n", dep_entry);
    }
  }
  else
  {
    perror("ERR occured from server\n");
  }
}
