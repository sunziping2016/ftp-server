#ifndef FTP_SERVER_FTP_SERVER_H
#define FTP_SERVER_FTP_SERVER_H

#include <stdint.h>
#include <netdb.h>
#include <linux/limits.h>
#include "helper.h"
#include "ftp_client.h"

typedef struct ftp_server_t {
    epoll_item_t event_data;
    int fd;

    struct sockaddr addr;
    socklen_t addrlen;
    char host[ADDRSTRLEN];
    in_port_t port;
    struct ftp_server_t *prev, *next;
} ftp_server_t;

void ftp_server_init();
int ftp_server_create(const char *host, const char *port, int family);
int ftp_server_close(ftp_server_t *server);
int ftp_server_close_all();

int ftp_server_list();

#endif
