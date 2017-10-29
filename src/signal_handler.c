#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "global.h"

static int signal_handler_callback(uint32_t events, void *arg)
{
    (void) arg;
    if (events & EPOLLIN) {
        struct signalfd_siginfo siginfo;
        while (global.signal_handler.fd != -1) {
            if (read(global.signal_handler.fd, &siginfo, sizeof(siginfo)) == -1) {
                if (errno != EAGAIN && errno != EWOULDBLOCK && global.loglevel >= LOGLEVEL_ERROR)
                    perror("E: read(fd: signal)");
                break;
            }
            if (global.loglevel >= LOGLEVEL_DEBUG) {
                if (siginfo.ssi_signo != SIGINT)
                    printf("D: receive signal %s\n", strsignal(siginfo.ssi_signo));
                else
                    printf("D: receive signal %s (%d)\n", strsignal(siginfo.ssi_signo),
                           global.signal_handler.sigint_num);
            }
            if (siginfo.ssi_signo == SIGINT) {
                if (!global.signal_handler.sigint_num) {
                    if (global.loglevel >= LOGLEVEL_INFO)
                        printf("I: close all servers\n");
                    ftp_cli_stop();
                    ftp_server_close_all();
                } else {
                    if (global.loglevel >= LOGLEVEL_WARN)
                        printf("W: close all connections\n");
                    global_close();
                }
                ++global.signal_handler.sigint_num;
            } else if (siginfo.ssi_signo == SIGTERM) {
                if (global.loglevel >= LOGLEVEL_WARN)
                    printf("W: close all connections\n");
                global_close();
            }
        }
    }
    return 0;
}

void signal_handler_init()
{
    global.signal_handler.fd = -1;
    global.signal_handler.sigint_num = 0;
    global.signal_handler.epoll_item.callback = signal_handler_callback;
    global.signal_handler.epoll_item.arg = NULL;
}

int signal_handler_start()
{
    int ret = -1;
    sigset_t set;
    if (sigfillset(&set) == -1) {
        if (global.loglevel >= LOGLEVEL_ERROR)
            perror("E: sigfillset");
    } else if (sigprocmask(SIG_BLOCK, &set, NULL) == -1) {
        if (global.loglevel >= LOGLEVEL_ERROR)
            perror("E: sigprocmask");
    } else if ((global.signal_handler.fd = signalfd(-1, &set, SFD_NONBLOCK)) == -1) {
        if (global.loglevel >= LOGLEVEL_ERROR)
            perror("E: signalfd");
    } else {
        struct epoll_event event;
        event.events = EPOLLIN | EPOLLET;
        event.data.ptr = &global.signal_handler.epoll_item;
        if (epoll_ctl(global.epfd, EPOLL_CTL_ADD, global.signal_handler.fd, &event) == -1) {
            if (global.loglevel >= LOGLEVEL_ERROR)
                perror("E: epoll_ctl(add: signal)");
            close(global.signal_handler.fd);
            global.signal_handler.fd = -1;
        } else {
            ret = 0;
            global_add_fd(global.signal_handler.fd, FD_SIGNAL, &global.signal_handler);
        }
    }
    return ret;
}

int signal_handler_stop()
{
    int ret = 0;
    if (global.signal_handler.fd != -1) {
        if (close(global.signal_handler.fd) == -1) {
            if (global.loglevel >= LOGLEVEL_ERROR)
                perror("E: close(fd: signal)");
            ret = -1;
        }
        global_remove_fd(global.signal_handler.fd);
        global.signal_handler.fd = -1;
    }
    return ret;
}
