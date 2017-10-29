#ifndef FTP_SERVER_FTP_CLIENT_H
#define FTP_SERVER_FTP_CLIENT_H

#include <netdb.h>
#include "reactor.h"

// TODO: DEBUG
#define CLIENT_RECV_BUFFER_SIZE 4096
#define CLIENT_SEND_BUFFER_SIZE 4096

#define FTP_COMMAND_MAX_LEN 4

typedef struct ftp_client_t {
    epoll_item_t event_data;

    int fd;
    uint32_t events;

    int busy;
    int exit_on_sent;

    char host[NI_MAXHOST], port[NI_MAXSERV];
    char recv_buffer[CLIENT_RECV_BUFFER_SIZE];
    char send_buffer[CLIENT_SEND_BUFFER_SIZE];

    size_t recv_buffer_size;
    size_t send_buffer_head, send_buffer_tail;

    struct ftp_client_t *prev, *next;

} ftp_client_t;

void ftp_client_init();
int ftp_client_add(int fd, struct sockaddr *addr, socklen_t len);
int ftp_client_close(ftp_client_t *client);
int ftp_client_close_all();
int ftp_client_list();

int ftp_client_write(ftp_client_t *client, const char *format, ...);
int ftp_client_command(ftp_client_t *client, char *command, size_t len);

#endif