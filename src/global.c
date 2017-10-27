#include <stdlib.h>
#include <getopt.h>
#include <stdio.h>
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
        "  -r, --root                root directory to server_t\n"
        "  -v, --verbose             enable verbose mode\n"
        "  -q, --quite               enable quite mode\n"
        "  -h, --help                print this help message\n"
        "  -V, --version             print program version\n"
        "\n"
        "minimum ftp server implemented by Sun Ziping.\n",
        *version_str = "0.1.0 alpha";

void global_init()
{
    global.loglevel = LOGLEVEL_WARN;
    global.epfd = -1;
    global.epoll_size = 0;
    ftp_server_init();
    ftp_cli_init();
}

int global_start(int argc, char *const argv[])
{
    char *host = NULL, *port = "21", *root = "/tmp";
    int family = AF_UNSPEC, cli = 0;
    struct option long_options[] = {
            {"cli", no_argument, &cli, 1},
            {"port", required_argument, NULL, 'p'},
            {"root", required_argument, NULL, 'r'},
            {"verbose", no_argument, NULL, 'v'},
            {"quite", no_argument, NULL, 'q'},
            {"help", no_argument, NULL, 'h'},
            {"version", no_argument, NULL, 'V'},
            {NULL, 0, NULL, 0}
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
                printf(help_str);
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
        fprintf(stderr, "%s: invalid number of position arguments %d\n", argv[0], pos_argc);
        exit(EXIT_FAILURE);
    }
    global.epfd = epoll_create1(0);
    if (global.epfd == -1) {
        perror("epoll_create");
        return -1;
    }
    if ((host || port) && root)
        ftp_server_create(host, port, family, root);
    if (cli)
        ftp_cli_start();
    return 0;
}

int global_run()
{
    struct epoll_event events[MAX_EPOLL_EVENTS];
    while (global.epoll_size) {
        int ret = epoll_wait(global.epfd, events, MAX_EPOLL_EVENTS, -1);
        if (ret == -1) {
            perror("epoll_wait");
            return -1;
        }
        for (int i = 0; i < ret; ++i) {
            epoll_item_t *item = events[i].data.ptr;

            if (item->callback)
                item->callback(events[i].events, item->arg);
        }
    }
    return 0;
}

int global_close()
{
    int ret = 0;
    if (close(global.epfd) == -1)
        ret = -1;
    global.epfd = -1;
    return ret;
}

