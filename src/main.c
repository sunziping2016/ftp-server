#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <netdb.h>
#include <reactor.h>
#include "ftp_server.h"


#define MAX_EPOLL_EVENTS 64

const char *help_str = ""
        "Usage: ftp-server [OPTION...] [[HOST] PORT]\n"
        "Options:\n"
        "  -r, --root                root directory to server_t\n"
        "  -v, --verbose             enable verbose mode\n"
        "  -h, --help                print this help message\n"
        "  -V, --version             print program version\n"
        "\n"
        "minimum ftp server implemented by Sun Ziping.\n",
        *version_str = "0.1.0 alpha";


int main(int argc, char * const argv[])
{
    int verbose = 0, ch, ret;
    const char *host = NULL, *port = "21";
    struct option long_options[] = {
            {"verbose", no_argument, NULL, 'v'},
            {"version", no_argument, NULL, 'V'},
            {"help", no_argument, NULL, 'h'},
            {NULL, 0, NULL, 0}
    };
    while ((ch = getopt_long(argc, argv, "vVh", long_options, NULL)) != -1) {
        switch (ch) {
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
        exit(1);
    }
    int efd = epoll_create1(0);
    if (efd == -1) {
        perror("failed to create epoll");
        exit(EXIT_FAILURE);
    }
    ftp_server_t ftp_server, *temp;
    ftp_server_init(&ftp_server);
    ret = ftp_server_create(&ftp_server, efd, host, port);
    if (ret < 0) {
        const char *error = ret == EAI_SYSTEM ? strerror(errno) : gai_strerror(ret);
        fprintf(stderr, "failed to create server: %s\n", error);
        exit(EXIT_FAILURE);
    }
    temp = ftp_server.next;
    while (temp != &ftp_server) {
        printf("server started at %s:%s\n", temp->host, temp->port);
        temp = temp->next;
    }

    struct epoll_event events[MAX_EPOLL_EVENTS];
    while (ftp_server.next != &ftp_server || ftp_server.clients_num) {
        ret = epoll_wait(efd, events, MAX_EPOLL_EVENTS, -1);
        if (ret == -1) {
            perror("failed to wait epoll");
            exit(EXIT_FAILURE);
        }
        for (int i = 0; i < ret; ++i) {
            epoll_item_t *item = events[i].data.ptr;
            if (!item->callback)
                continue;
            if (item->callback(efd, events[i].events, item->arg) == -1)
                perror("failed to process event");
        }
    }

    close(efd);
    return 0;
}

