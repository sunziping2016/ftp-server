#include <stdlib.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <reactor.h>
#include <unistd.h>
#include "global.h"

global_t global;

const char *help_str = ""
        "Usage: ftp-server [OPTION...] [[HOST] PORT]\n"
        "Options:\n"
        "  -4                        ipv4 only\n"
        "  -6                        ipv6 only\n"
        "  --cli                     launch cli\n"
        "  -p, --port                port to bind\n"
        "  --no-anonymous            disable anonymous user\n"
        "  -r, --root                root directory for anonymous user\n"
        "  -v, --verbose             enable verbose mode\n"
        "  -q, --quite               enable quite mode\n"
        "  -h, --help                print this help message\n"
        "  -V, --version             print program version\n"
        "\n"
        "minimum ftp server implemented by Sun Ziping.\n",
        *version_str = "v0.1.0 alpha";

void global_init()
{
    global.loglevel = LOGLEVEL_WARN;
    global.epfd = -1;
    global.epoll_size = 0;

    global.pending_free = NULL;
    global.fd_vector = NULL;
    global.fd_vector_capacity = 0;

    signal_handler_init();
    ftp_server_init();
    ftp_client_init();
    ftp_cli_init();
    ftp_users_init();
}

int global_start(int argc, char *const argv[])
{
    char *host = NULL, *port = "21", *root = "/tmp";
    int family = AF_UNSPEC, cli = 0, anonymous = 1;
    struct option long_options[] = {
            {"cli",          no_argument, &cli,                     1},
            {"port",         required_argument, NULL,               'p'},
            {"no-anonymous", no_argument, &anonymous,               0},
            {"root",         required_argument, NULL,               'r'},
            {"verbose",      no_argument,       NULL,               'v'},
            {"debug",        no_argument, (int *) &global.loglevel, LOGLEVEL_DEBUG},
            {"quite",        no_argument,       NULL,               'q'},
            {"help",         no_argument,       NULL,               'h'},
            {"version",      no_argument,       NULL,               'V'},
            {NULL, 0,                           NULL,               0}
    };
    int ch;
    while ((ch = getopt_long(argc, argv, "46p:r:vqhV", long_options, NULL)) != -1) {
        switch (ch) {
            case 0:
                break;
            case '4':
                family = AF_INET;
                break;
            case '6':
                family = AF_INET6;
                break;
            case 'p':
                port = optarg;
                break;
            case 'r':
                root = optarg;
                break;
            case 'v':
                global.loglevel = LOGLEVEL_INFO;
                break;
            case 'q':
                global.loglevel = LOGLEVEL_ERROR;
                break;
            case 'h':
                puts(help_str);
                exit(EXIT_SUCCESS);
            case 'V':
                printf("%s\n", version_str);
                exit(EXIT_SUCCESS);
            default:
                exit(EXIT_FAILURE);
        }
    }
    int pos_argc = argc - optind;
    if (pos_argc == 2) {
        host = argv[optind];
        port = argv[optind + 1];
    } else if (pos_argc == 1)
        port = argv[optind];
    else if (pos_argc != 0) {
        if (global.loglevel >= LOGLEVEL_ERROR)
            fprintf(stderr, "E: invalid number of position arguments %d\n", pos_argc);
        exit(EXIT_FAILURE);
    }
    if (ftp_users_start() == -1)
        return -1; // Fatal
    global.epfd = epoll_create1(0);
    if (global.epfd == -1) {
        if (global.loglevel >= LOGLEVEL_ERROR)
            perror("E: epoll_create");
        return -1;
    }
    global_add_fd(global.epfd, FD_EPOLL, &global);
    if (cli)
        ftp_cli_start();
    if (anonymous)
        ftp_users_add("anonymous", NULL, root);
    if ((host || port) && root)
        ftp_server_create(host, port, family);
    signal_handler_start();
    return 0;
}

int global_run()
{
    struct epoll_event events[MAX_EPOLL_EVENTS];
    while (global.epoll_size) {
        int ret = epoll_wait(global.epfd, events, MAX_EPOLL_EVENTS, -1);
        if (ret == -1) {
            if (global.loglevel >= LOGLEVEL_ERROR)
                perror("E: epoll_wait");
            return -1;
        }
        for (int i = 0; i < ret; ++i) {
            epoll_item_t *item = events[i].data.ptr;

            if (item->callback)
                item->callback(events[i].events, item->arg);
        }
        pending_free_t *temp;
        while (global.pending_free) {
            free(global.pending_free->ptr);
            temp = global.pending_free->next;
            free(global.pending_free);
            global.pending_free = temp;
        }
    }
    return 0;
}

int global_close()
{
    int ret = 0;
    if (global.epfd != -1) {
        signal_handler_stop();
        if (ftp_cli_stop() == -1)
            ret = -1;
        if (ftp_server_close_all() == -1)
            ret = -1;
        if (ftp_client_close_all() == -1)
            ret = -1;
        if (close(global.epfd) == -1) {
            if (global.loglevel >= LOGLEVEL_ERROR)
                perror("E: close(fd: epoll)");
            ret = -1;
        }
        if (ftp_users_stop() == -1)
            ret = -1;
        if (global.epoll_size != 0 && global.loglevel >= LOGLEVEL_WARN)
            fprintf(stderr, "E: remains %lu fd(s) in epoll set (possibly a bug)\n", global.epoll_size);
        free(global.fd_vector);
        global_remove_fd(global.epfd);
        global.epfd = -1;
    }
    return ret;
}

int free_later(void *prt)
{
    pending_free_t *new_item = malloc(sizeof(pending_free_t));
    if (!new_item) {
        if (global.loglevel >= LOGLEVEL_ERROR)
            perror("E: malloc(pending_free)");
        return -1;
    }
    new_item->ptr = prt;
    new_item->next = global.pending_free;
    global.pending_free = new_item;
    return 0;
}

int global_add_fd(int fd, enum fd_type_t fd_type, void *pointer)
{
    if ((size_t) fd >= global.fd_vector_capacity) {
        size_t new_capacity = global.fd_vector_capacity == 0 ? FD_VECTOR_INIT_SIZE : global.fd_vector_capacity;
        if ((size_t) fd >= new_capacity)
            new_capacity = (size_t) (fd + 1);
        global.fd_vector = realloc(global.fd_vector, sizeof(fd_vector_t) * new_capacity);
        if (!global.fd_vector) {
            if (global.loglevel >= LOGLEVEL_ERROR)
                perror("E: reallocarray(fd_vector)");
            return -1;
        }
        memset(global.fd_vector + global.fd_vector_capacity, 0,
               sizeof(fd_vector_t) * (new_capacity - global.fd_vector_capacity));
        global.fd_vector_capacity = new_capacity;
    }
    global.fd_vector[fd].fd_type = fd_type;
    global.fd_vector[fd].pointer = pointer;
    return 0;
}

int global_remove_fd(int fd)
{
    if ((size_t) fd >= global.fd_vector_capacity)
        return -1;
    global.fd_vector[fd].fd_type = FD_NOT_USED;
    return 0;
}

static const char *fd_type_str[] = {
        "not used",
        "signal",
        "epoll",
        "ftp server",
        "ftp client"
};

int global_list_fd()
{
    int printed = 0;
    for (size_t i = 0; i < global.fd_vector_capacity; ++i) {
        if (global.fd_vector[i].fd_type != FD_NOT_USED) {
            printf("fd: %lu\ttype: %s\n", i, fd_type_str[global.fd_vector[i].fd_type]);
            printed = 1;
        }
    }
    if (!printed)
        printf("No file descriptor\n");
    return 0;
}
