#ifndef FTP_SERVER_FTP_SERVER_H
#define FTP_SERVER_FTP_SERVER_H

#include <stdint.h>
#include <netdb.h>
#include <linux/limits.h>
#include "reactor.h"
#include "ftp_client.h"

typedef struct global_t global_t;

typedef struct ftp_server_t {
    epoll_item_t event_data;

    int fd;
    ftp_client_t clients;
    size_t clients_num;

    int family;
    char host[NI_MAXHOST];
    char port[NI_MAXSERV];
    char root[PATH_MAX];
    struct ftp_server_t *prev, *next;
} ftp_server_t;

void ftp_server_init();
int ftp_server_create(const char *host, const char *port,
                      int family, const char *root);
int ftp_server_close(ftp_server_t *server);
int ftp_server_close_all();

#endif
