#include "ftp_server.h"
#include "global.h"

#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <malloc.h>
#include <sys/epoll.h>
#include <string.h>
#include <sys/stat.h>

static int ftp_server_callback(uint32_t events, void *arg)
{
    ftp_server_t *session = arg;
    if (events & EPOLLERR || events & EPOLLHUP)
        ftp_server_close(session);
    else if (events & EPOLLIN) {
        for (;;) {
            // TODO: add accept client
        }
    }
    return 0;
}

void ftp_server_init()
{
    ftp_server_t *head = &global.servers;
    head->fd = -1;
    head->clients_num = 0;
    head->clients.prev = head->clients.next = &head->clients;
    head->prev = head->next = head;
}

int ftp_server_create(const char *host, const char *port,
                      int family, const char *root)
{
    // TODO: check dir
    struct addrinfo hints = {0};
    struct stat statbuf;
    if (stat(root, &statbuf) == -1) {
        perror("E: stat");
        return EAI_SYSTEM;
    }
    if (!S_ISDIR(statbuf.st_mode)) {
        fprintf(stderr, "E: requires directory\n");
        return EAI_SYSTEM;
    }
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    struct addrinfo *result, *temp;
    int ret = getaddrinfo(host, port, &hints, &result), yes = 1;
    if (ret != 0) {
        fprintf(stderr, "E: getaddrinfo: %s\n", gai_strerror(ret));
        return ret;
    }
    assert(result);
    ret = EAI_SYSTEM;
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET;
    temp = result;
    while (temp) {
        ftp_server_t *server = malloc(sizeof(ftp_server_t));
        if (server == NULL) {
            perror("E: malloc");
        } else if ((server->fd = socket(temp->ai_family, temp->ai_socktype | SOCK_NONBLOCK,
                                      temp->ai_protocol)) == -1) {
            perror("E: socket");
            free(server);
        } else {
            event.data.ptr = &server->event_data;
            if (temp->ai_family == AF_INET6 &&
                    setsockopt(server->fd, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof(yes)) == -1) {
                perror("E: setsockopt");
                if (global.loglevel >= LOGLEVEL_WARN)
                    fprintf(stderr, "W: maybe using ipv4 mapped ipv6 address");
            }
            if (bind(server->fd, temp->ai_addr, temp->ai_addrlen) == -1) {
                perror("E: bind");
                close(server->fd);
                free(server);
            } else if (listen(server->fd, SOMAXCONN) == -1) {
                perror("E: listen");
                close(server->fd);
                free(server);
            } else if (epoll_ctl(global.epfd, EPOLL_CTL_ADD, server->fd, &event) == -1) {
                perror("E: epoll_ctl");
                close(server->fd);
                free(server);
            } else {
                if (getnameinfo(temp->ai_addr, temp->ai_addrlen, server->host, NI_MAXHOST,
                                server->port, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV) == -1) {
                    if (global.loglevel >= LOGLEVEL_WARN)
                        fprintf(stderr, "W: getnameinfo %s", strerror(errno));
                    server->host[0] = '\0';
                    server->port[0] = '\0';
                }
                if (global.loglevel >= LOGLEVEL_INFO)
                    printf("I: started server on descriptor %d (host=%s, port=%s)\n",
                        server->fd, server->host, server->port);
                ++global.epoll_size;
                server->event_data.callback = ftp_server_callback;
                server->event_data.arg = server;
                server->family = temp->ai_family;
                strncpy(server->root, root, PATH_MAX);

                server->prev = global.servers.prev;
                global.servers.prev->next = server;
                global.servers.prev = server;
                server->next = &global.servers;

                server->clients_num = 0;
                server->clients.next = server->clients.prev = &server->clients;
                ret = 0;
            }
        }
        temp = temp->ai_next;
    }
    freeaddrinfo(result);
    return ret;
}

int ftp_server_close(ftp_server_t *server)
{
    int ret = 0;
    ftp_server_t *head = &global.servers;
    server->prev->next = server->next;
    server->next->prev = server->prev;
    if (server->clients_num) {
        ftp_client_t *head_client = &head->clients, *server_client = &server->clients;
        server_client->next->prev = head_client->prev;
        head_client->prev->next = server_client->next;
        head_client->prev = server_client->prev;
        server_client->prev->next = head_client;
        head->clients_num += server->clients_num;
    }
    int fd = server->fd;
    if (close(fd) == -1) {
        perror("E: close");
        ret = -1;
    }
    if (global.loglevel >= LOGLEVEL_INFO)
        printf("I: closed server on descriptor %d (host=%s, port=%s)\n",
               server->fd, server->host, server->port);
    --global.epoll_size;
    free(server);
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


