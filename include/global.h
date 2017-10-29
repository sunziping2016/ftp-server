#ifndef FTP_SERVER_GLOBAL_H
#define FTP_SERVER_GLOBAL_H

#include "ftp_cli.h"
#include "signal_handler.h"
#include "ftp_server.h"
#include "ftp_client.h"
#include "ftp_users.h"

#define MAX_EPOLL_EVENTS 64
#define FD_VECTOR_INIT_SIZE 1024

typedef struct ftp_server_t ftp_server_t;

enum loglevel_t {
    LOGLEVEL_NONE,
    LOGLEVEL_ERROR,
    LOGLEVEL_WARN,
    LOGLEVEL_INFO,
    LOGLEVEL_DEBUG
};

typedef struct pending_free_t {
    void *ptr;
    struct pending_free_t *next;
} pending_free_t;

enum fd_type_t {
    FD_NOT_USED,
    FD_SIGNAL,
    FD_EPOLL,
    FD_FTP_SERVER,
    FD_FTP_CLIENT
};

typedef struct fd_vector_t {
    enum fd_type_t fd_type;
    void *pointer;
} fd_vector_t;

typedef struct global_t {
    enum loglevel_t loglevel;

    int epfd;
    size_t epoll_size;
    fd_vector_t *fd_vector;
    size_t fd_vector_capacity;

    ftp_cli_t cli;
    signal_handler_t signal_handler;
    ftp_server_t servers;
    ftp_client_t clients;
    ftp_users_t users;

    pending_free_t *pending_free;
} global_t;

extern const char *help_str, *version_str;
extern global_t global;

void global_init();
int global_start(int argc, char *const argv[]);
int global_run();
int global_close();

int free_later(void *prt);

int global_add_fd(int fd, enum fd_type_t fd_type, void *pointer);
int global_remove_fd(int fd);

int global_list_fd();

#endif
