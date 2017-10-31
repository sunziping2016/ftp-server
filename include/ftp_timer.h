#ifndef FTP_SERVER_TIMER_H
#define FTP_SERVER_TIMER_H

#include <sys/timerfd.h>

#include "reactor.h"

typedef struct ftp_timer_t ftp_timer_t;
typedef int (*timer_callback_t)(ftp_timer_t *timer, void *arg);

typedef struct ftp_timer_t {
    epoll_item_t event_data;
    int fd, ref, oneshot;

    uint64_t times;
    timer_callback_t callback;
    void *arg;

    ftp_timer_t *prev, *next;
} ftp_timer_t;


void ftp_timer_init();
int ftp_timer_add(const struct itimerspec *value, timer_callback_t callback, void *arg, int ref);
int ftp_timer_close(ftp_timer_t *timer);
int ftp_timer_close_all();

#endif