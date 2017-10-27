#ifndef FTP_SERVER_FTP_CLI_H
#define FTP_SERVER_FTP_CLI_H

#include "reactor.h"

typedef struct global_t global_t;

typedef struct ftp_cli_t {
    epoll_item_t epoll_item;
    int fd;
    int done;
} ftp_cli_t;

void ftp_cli_init();
int ftp_cli_start();
int ftp_cli_stop();

#endif