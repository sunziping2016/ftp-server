#include <malloc.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <helper.h>
#include "global.h"

static int ftp_timer_callback(uint32_t events, void *arg)
{
    ftp_timer_t *timer = arg;
    if (events & EPOLLIN) {
        ssize_t ret;
        while (timer->fd != -1) {
            ret = read(timer->fd, &timer->times, sizeof(timer->times));
            if (ret == -1) {
                if (errno != EAGAIN && global.loglevel >= LOGLEVEL_ERROR)
                    fprintf(stderr, "E: read(timer#%d): %s\n", timer->fd, strerror(errno));
                break;
            }
            if (timer->callback)
                (*timer->callback)(timer, timer->arg);
            if (timer->oneshot)
                ftp_timer_close(timer);
        }
    }
    if (events & EPOLLHUP || events & EPOLLERR)
        ftp_timer_close(timer);
    return 0;
}

void ftp_timer_init()
{
    global.timers.fd = -1;
    global.timers.prev = global.timers.next = &global.timers;
}

int ftp_timer_add(const struct itimerspec *value, timer_callback_t callback, void *arg, int ref)
{
    ftp_timer_t *timer = malloc(sizeof(ftp_timer_t));
    if (timer == NULL) {
        if (global.loglevel >= LOGLEVEL_ERROR)
            perror("E: malloc(timer)");
        return -1;
    }
    if ((timer->fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)) == -1) {
        if (global.loglevel >= LOGLEVEL_ERROR)
            perror("E: timerfd_create");
        free(timer);
        return -1;
    }
    if (timerfd_settime(timer->fd, 0, value, NULL) == -1) {
        if (global.loglevel >= LOGLEVEL_ERROR)
            fprintf(stderr, "E: timerfd_settime(timer#%d): %s\n", timer->fd, strerror(errno));
        close(timer->fd);
        free(timer);
        return -1;
    }
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET;
    event.data.ptr = &timer->event_data;
    if (epoll_ctl(global.epfd, EPOLL_CTL_ADD, timer->fd, &event) == -1) {
        if (global.loglevel >= LOGLEVEL_ERROR)
            fprintf(stderr, "E: epoll_ctl(add: timer#%d): %s\n", timer->fd, strerror(errno));
        close(timer->fd);
        free(timer);
        return -1;
    }
    timer->event_data.callback = ftp_timer_callback;
    timer->event_data.arg = timer;

    timer->oneshot = value->it_interval.tv_sec == 0 && value->it_interval.tv_nsec == 0;
    timer->callback = callback;
    timer->arg = arg;
    timer->ref = ref;

    timer->prev = global.timers.prev;
    global.timers.prev->next = timer;
    global.timers.prev = timer;
    timer->next = &global.timers;

    global_add_fd(timer->fd, FD_TIMER, timer);
    if (timer->ref)
        ++global.epoll_size;
    return 0;
}

int ftp_timer_close(ftp_timer_t *timer)
{
    int ret = -1;
    if (timer->fd != -1) {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        if (close(timer->fd) == -1) {
            if (global.loglevel >= LOGLEVEL_ERROR)
                fprintf(stderr, "E: close(timer#%d): %s\n", timer->fd, strerror(errno));
            ret = -1;
        }
        global_remove_fd(timer->fd);
        timer->fd = -1;
        if (timer->ref)
            --global.epoll_size;
        free_later(timer);
    }
    return ret;
}

int ftp_timer_close_all()
{
    int ret = 0;
    while (&global.timers != global.timers.next) {
        if (ftp_timer_close(global.timers.next) == -1)
            ret = -1;
    }
    return ret;
}
