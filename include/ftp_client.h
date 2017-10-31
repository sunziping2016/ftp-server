#ifndef FTP_SERVER_FTP_CLIENT_H
#define FTP_SERVER_FTP_CLIENT_H

#include <netdb.h>
#include "reactor.h"

#define CLIENT_RECV_BUFFER_SIZE 4096
#define CLIENT_SEND_BUFFER_SIZE 4096

typedef struct ftp_client_t {
    epoll_item_t control_event_data;
    int fd;
    uint32_t control_events;

    epoll_item_t data_event_data;
    int data_fd;
    uint32_t data_events;

    int busy, logged_in;
    int exit_on_sent;

    char *user, *root;
    struct sockaddr local_addr, peer_addr;
    socklen_t local_addrlen, peer_addrlen;
    char peer_host[NI_MAXHOST], peer_port[NI_MAXSERV];
    char recv_buffer[CLIENT_RECV_BUFFER_SIZE];
    char send_buffer[CLIENT_SEND_BUFFER_SIZE];
    char wd[PATH_MAX];

    size_t recv_buffer_size;
    size_t send_buffer_head, send_buffer_tail;

    struct ftp_client_t *prev, *next;

} ftp_client_t;

void ftp_client_init();
int ftp_client_add(int fd, struct sockaddr *addr, socklen_t len);
int ftp_client_close(ftp_client_t *client);
int ftp_client_close_all();
int ftp_client_list();
int ftp_client_update(ftp_client_t *client);

int ftp_client_write(ftp_client_t *client, const char *format, ...);
int ftp_client_command(ftp_client_t *client, char *input);

#endif