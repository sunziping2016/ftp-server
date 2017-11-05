#ifndef FTP_SERVER_FTP_CLI_H
#define FTP_SERVER_FTP_CLI_H

#include "helper.h"

typedef struct ftp_cli_t {
    epoll_item_t epoll_item;
    int fd;
    int done;
} ftp_cli_t;

typedef int (*ftp_cli_command_callback_t)(char *arg);

typedef struct ftp_cli_command_t {
    char *name;
    ftp_cli_command_callback_t func;
    char *doc;
} ftp_cli_command_t;

void ftp_cli_init();
int ftp_cli_start(int prompt);
int ftp_cli_stop();

int parse_arguments(char *arg, int argc, char *argv[]);

#endif