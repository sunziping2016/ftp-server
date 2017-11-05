#include "global.h"

#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <malloc.h>
#include <sys/epoll.h>
#include <string.h>

static int ftp_server_callback(uint32_t events, void *arg)
{
    ftp_server_t *session = arg;
    if (events & EPOLLIN) {
        struct sockaddr_storage addr;
        socklen_t len;
        int fd;
        while (session->fd != -1) {
            len = sizeof(addr);
            fd = accept4(session->fd, (struct sockaddr *) &addr, &len, SOCK_NONBLOCK);
            if (fd == -1) {
                if (errno != EAGAIN && errno != EWOULDBLOCK && global.loglevel >= LOGLEVEL_ERROR)
                    fprintf(stderr, "E: accept(server#%d): %s\n", session->fd, strerror(errno));
                break;
            }
            ftp_client_add(fd, &addr, len);
        }
    }
    if (events & EPOLLERR || events & EPOLLHUP)
        ftp_server_close(session);
    return 0;
}

void ftp_server_init()
{
    ftp_server_t *head = &global.servers;
    head->fd = -1;
    head->prev = head->next = head;
}

int ftp_server_create(const char *host, const char *port, int family)
{
    struct addrinfo hints = {0};
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    struct addrinfo *result, *temp;
    int ret = getaddrinfo(host, port, &hints, &result), yes = 1;
    if (ret != 0) {
        if (global.loglevel >= LOGLEVEL_ERROR)
            fprintf(stderr, "E: getaddrinfo: %s\n", gai_strerror(ret));
        return ret;
    }
    assert(result);
    ret = EAI_SYSTEM;
    temp = result;
    for (; temp; temp = temp->ai_next) {
        ftp_server_t *server = malloc(sizeof(ftp_server_t));
        if (server == NULL) {
            if (global.loglevel >= LOGLEVEL_ERROR)
                perror("E: malloc(server)");
            continue;
        }
        if (get_addrin_info(temp->ai_addr, server->host, sizeof(server->host), &server->port) == -1) {
            if (global.loglevel >= LOGLEVEL_ERROR)
                perror("E: inet_ntop");
            free(server);
            continue;
        }
        if ((server->fd = socket(temp->ai_family, temp->ai_socktype | SOCK_NONBLOCK,
                                 temp->ai_protocol)) == -1) {
            if (global.loglevel >= LOGLEVEL_ERROR)
                perror("E: socket(server)");
            free(server);
            continue;
        }
        if (temp->ai_family == AF_INET6 &&
            setsockopt(server->fd, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof(yes)) == -1 &&
                global.loglevel >= LOGLEVEL_ERROR)
            fprintf(stderr, "W: setsockopt(server#%d.ipv6only): %s\n", server->fd, strerror(errno));
        if (setsockopt(server->fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1 &&
                global.loglevel >= LOGLEVEL_ERROR)
            fprintf(stderr, "W: setsockopt(server#%d.reuse): %s\n", server->fd, strerror(errno));
        if (bind(server->fd, temp->ai_addr, temp->ai_addrlen) == -1) {
            if (global.loglevel >= LOGLEVEL_ERROR)
                fprintf(stderr, "E: bind(server#%d): %s\n", server->fd, strerror(errno));
            close(server->fd);
            free(server);
        } else if (listen(server->fd, SOMAXCONN) == -1) {
            if (global.loglevel >= LOGLEVEL_ERROR)
                fprintf(stderr, "E: listen(server#%d): %s\n", server->fd, strerror(errno));
            close(server->fd);
            free(server);
        } else {
            struct epoll_event event;
            event.events = EPOLLIN | EPOLLET;
            event.data.ptr = &server->event_data;
            if (epoll_ctl(global.epfd, EPOLL_CTL_ADD, server->fd, &event) == -1) {
                if (global.loglevel >= LOGLEVEL_ERROR)
                    fprintf(stderr, "E: epoll_ctl(add: server#%d): %s\n", server->fd, strerror(errno));
                close(server->fd);
                free(server);
            } else {
                if (global.loglevel >= LOGLEVEL_INFO)
                    printf("I: started server on descriptor %d (host=%s, port=%u)\n",
                           server->fd, server->host, server->port);
                server->event_data.callback = ftp_server_callback;
                server->event_data.arg = server;
                memcpy(&server->addr, temp->ai_addr, temp->ai_addrlen);
                server->addrlen = temp->ai_addrlen;

                server->prev = global.servers.prev;
                global.servers.prev->next = server;
                global.servers.prev = server;
                server->next = &global.servers;

                global_add_fd(server->fd, FD_FTP_SERVER, server);
                ++global.epoll_size;
                ret = 0;
            }

        }
    }
    freeaddrinfo(result);
    return ret;
}

int ftp_server_close(ftp_server_t *server)
{
    int ret = 0;
    if (server->fd != -1) {
        server->prev->next = server->next;
        server->next->prev = server->prev;
        if (close(server->fd) == -1) {
            if (global.loglevel >= LOGLEVEL_ERROR)
                fprintf(stderr, "E: close(server#%d): %s\n", server->fd, strerror(errno));
            ret = -1;
        }
        if (global.loglevel >= LOGLEVEL_INFO)
            printf("I: closed server on descriptor %d (host=%s, port=%u)\n",
                   server->fd, server->host, server->port);
        global_remove_fd(server->fd);
        server->fd = -1;
        free_later(server);
        --global.epoll_size;
    }
    return ret;
}

int ftp_server_close_all()
{
    int ret = 0;
    while (&global.servers != global.servers.next) {
        if (ftp_server_close(global.servers.next) == -1)
            ret = -1;
    }
    return ret;
}

int ftp_server_list()
{
    ftp_server_t *temp = global.servers.next;
    if (temp == &global.servers) {
        printf("No running ftp server\n");
    } else {
        do {
            printf("fd: %d\thost: %s\tport: %u\n", temp->fd, temp->host, temp->port);
            temp = temp->next;
        } while (temp != &global.servers);
    }
    return 0;
}
