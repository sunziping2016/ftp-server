#ifndef FTP_SERVER_GLOBAL_H
#define FTP_SERVER_GLOBAL_H

#include "ftp_server.h"
#include "ftp_cli.h"

#define MAX_EPOLL_EVENTS 64

typedef struct ftp_server_t ftp_server_t;

enum loglevel_t {
    LOGLEVEL_ERROR,
    LOGLEVEL_WARN,
    LOGLEVEL_INFO
};

typedef struct global_t {
    enum loglevel_t loglevel;

    int epfd;
    size_t epoll_size;

    ftp_server_t servers;
    ftp_cli_t cli;
} global_t;

extern global_t global;

void global_init();
int global_start(int argc, char *const argv[]);
int global_run();
int global_close();

#endif
