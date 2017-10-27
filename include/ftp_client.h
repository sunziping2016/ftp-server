#ifndef FTP_SERVER_FTP_CLIENT_H
#define FTP_SERVER_FTP_CLIENT_H

#include <netdb.h>
#include "reactor.h"

typedef struct ftp_client_t {
    epoll_item_t event_data;

    int fd;
    uint32_t event;

    char host[NI_MAXHOST], port[NI_MAXSERV];

    struct ftp_client_t *prev, *next;

} ftp_client_t;

int ftp_client_add(ftp_client_t *head, int epfd, int fd, struct sockaddr *addr, socklen_t len);

#endif