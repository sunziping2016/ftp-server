#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "global.h"


void ftp_cli_init()
{
    global.cli.fd = -1;
    global.cli.done = 0;
}

int ftp_cli_callback(uint32_t events, void *arg)
{
    (void) arg;
    if (events & EPOLLIN)
        rl_callback_read_char();
    if (events & EPOLLHUP)
        ftp_cli_stop();
    return 0;
}

void rl_callback(char *line)
{
    if (line) {
        if (*line) {
            printf("%s\n", line);
            add_history(line);
        }
    } else
        ftp_cli_stop();
}

int ftp_cli_start()
{
    int ret = -1;
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.ptr = &global.cli.epoll_item;
    if (epoll_ctl(global.epfd, EPOLL_CTL_ADD, STDIN_FILENO, &event) == -1)
        perror("E: epoll_ctl");
    else {
        ++global.epoll_size;
        global.cli.epoll_item.callback = ftp_cli_callback;
        global.cli.epoll_item.arg = NULL;
        global.cli.fd = STDIN_FILENO;
        rl_readline_name = "minimum-ftpd";
        rl_callback_handler_install("> ", rl_callback);
    }
    return ret;
}

int ftp_cli_stop()
{
    int ret = 0;
    if (global.cli.fd != -1) {
        if (!global.cli.done){
            printf("exit\n");
            if (ftp_server_close_all() == -1)
                ret = -1;
        }
        --global.epoll_size;
        if (epoll_ctl(global.epfd, EPOLL_CTL_DEL, STDIN_FILENO, NULL) == -1) {
            perror("E: epoll_ctl");
            ret = -1;
        }
        rl_callback_handler_remove();
        global.cli.fd = -1;
    }
    return ret;
}