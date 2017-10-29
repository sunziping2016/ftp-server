#ifndef FTP_SERVER_SIGNAL_HANDLER_H
#define FTP_SERVER_SIGNAL_HANDLER_H

#include "reactor.h"

typedef struct signal_handler_t {
    epoll_item_t epoll_item;
    int fd;
    int sigint_num;
} signal_handler_t;

void signal_handler_init();
int signal_handler_start();
int signal_handler_stop();

#endif
