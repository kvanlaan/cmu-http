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

#define BUF_SIZE 8192
// Closes a client's connection if they have not sent a valid request within
// CONNECTION_TIMEOUT seconds.
#define CONNECTION_TIMEOUT 50

#define MAX_CONCURRENT_CONNS 100

#define HOSTLEN 256
#define SERVLEN 8

#define DEFAULT_TIMEOUT 3000

typedef struct
{
    struct sockaddr_in addr; // Socket address
    socklen_t addrlen;       // Socket address length
    int connfd;              // Client connection file descriptor
    char host[HOSTLEN];      // Client host
    char serv[SERVLEN];      // Client service (port)
} client_info;

// setupsocket
// returns server_fd to poll for new connections
// returns -1 on error
static int setup_socket()
{
    int server_fd = 0;
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        fprintf(stderr, "socket failed\n");
        return -1;
    }

    int optval_pos = 1;

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &optval_pos, sizeof(optval_pos)))
    {
        fprintf(stderr, "setsockopt reuse failed\n");
        close(server_fd);
        return -1;
    }

    struct sockaddr_in address;

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("0.0.0.0"); // DON't USE 127..., restricts external clients
    address.sin_port = htons(HTTP_PORT);
    ;
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) != 0)
    {
        fprintf(stderr, "bind failed: %s\n", strerror(errno));
        if (close(server_fd) < 0)
        {
            fprintf(stderr, "fd socket not properly closed %s\n", strerror(errno));
        }
        return -1;
    }

    if (listen(server_fd, MAX_CONCURRENT_CONNS) != 0)
    {
        fprintf(stderr, "listen failed: %s\n", strerror(errno));
        // pull out this close method
        if (close(server_fd) < 0)
        {
            fprintf(stderr, "fd socket not properly closed %s\n", strerror(errno));
        }
        return -1;
    }

    return server_fd;
}


// init_poll_list
// mallocs poll_list set to length of max connections
// returns pointer to malloced poll_list
static struct pollfd *init_poll_list(int server_fd)
{
    struct pollfd *poll_list = malloc(MAX_CONCURRENT_CONNS * sizeof(struct pollfd));
    if (poll_list == NULL)
    {
        fprintf(stderr, "poll_list malloc failed: %s\n", strerror(errno));
        return NULL;
    }
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
    int server_fd = setup_socket();

    if (server_fd == -1)
    {
        fprintf(stderr, "ERR setup socket failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    struct pollfd *poll_list = init_poll_list(server_fd);
    if (poll_list == NULL)
    {
        fprintf(stderr, "ERR init_poll_list failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    while (1)
    {
        // to-do: determine appropriate folling timeout
        int timeout = DEFAULT_TIMEOUT;
        int poll_ready = poll(poll_list, MAX_CONCURRENT_CONNS, timeout);

        if (poll_ready == 0)
        {
            printf("No poll events, repolling\n");
            continue;
        }
        else if (poll_ready == -1)
        {
            fprintf(stderr, "poll threw error: %s\n", strerror(errno));
            return EXIT_FAILURE;
        }

        for (int i = 0; i < MAX_CONCURRENT_CONNS; i++)
        {
            struct pollfd poll_list_entry = poll_list[i];

            int poll_event_returned = poll_list_entry.revents & POLLIN;
            if (poll_event_returned != 0)
            {
                printf("real event...\n");
                if (poll_list_entry.fd == server_fd)
                {
                    add_new_connection(poll_list, server_fd);
                    printf("Added new conncetion\n");
                    continue;
                }
                else
                {
                    printf("client conn\n");
                    // consume data
                }
            }
        }
    }
}
