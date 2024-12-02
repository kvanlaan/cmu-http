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

int main(int argc, char *argv[]) {
    /* Validate and parse args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <server-ip>\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    /* Set up a connection to the HTTP server */
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
    inet_pton(AF_INET, argv[1], &(sin.sin_addr));
    
    fprintf(stderr, "Parsed IP address of the server: %X\n", htonl(sin.sin_addr.s_addr));

    if(connect(sockfd, (struct sockaddr *)&sin, sizeof(sin)) < 0){
        printf("error on connect\n");
        return TEST_ERROR_HTTP_CONNECT_FAILED;
    }

    /* CP1: Send out a HTTP request, waiting for the response */
    // const int header_count = 1;
    // Request_header headers[header_count];
    // Request_header header_0;
    // memcpy(header_0.header_name, "Host", 5);
    // memcpy(header_0.header_name, "It's me", 8);
    // headers[0] = header_0;
    Request request = {
      "1.1",
      "GET",
      "/index2.html",
      "127.0.0.1",
      // headers,
      // header_count,
      // header_count,
      NULL,
      0,
      0,
      0,
      NULL,
      0
    };
    char buf[8192];
    size_t size;
    int err = serialize_http_request(buf, &size, &request);
    if(err != TEST_ERROR_NONE) {
      printf("error serializing request\n");
    }
    buf[size] = '\0';
    printf("sending request:\n%s\n", buf);
    err = send(sockfd, buf, size, 0);
    if(err < 0) {
      printf("error sending msg\n");
    }
    // parse_http_response should be implemeted in CP2
}
