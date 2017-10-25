#ifndef FTP_SERVER_FTP_SERVER_H
#define FTP_SERVER_FTP_SERVER_H

#include <stdint.h>
#include <netdb.h>
#include "reactor.h"

typedef struct _ftp_client_session_t {
    epoll_item_t event_data;

    struct _ftp_client_session_t *prev, *next;
} client_session_t;

typedef struct _ftp_server_t {
    epoll_item_t event_data;
    int fd;
    client_session_t clients;

    int family;
    char host[NI_MAXHOST], port[NI_MAXSERV];
    size_t clients_num;
    struct _ftp_server_t *prev, *next;
} ftp_server_t;

inline void ftp_server_init(ftp_server_t *head) {
    head->fd = -1;
    head->clients_num = 0;
    head->clients.prev = head->clients.next = &head->clients;
    head->prev = head->next = head;
}
int ftp_server_create(ftp_server_t *head, int efd, const char *host, const char *port);
int ftp_server_close(ftp_server_t *server);
int ftp_server_close_all(ftp_server_t *head);

#endif
