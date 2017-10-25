#include "ftp_server.h"

#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <malloc.h>
#include <sys/epoll.h>

static int ftp_server_callback(int efd, uint32_t events, void *arg)
{
    ftp_server_t *session = arg;
    printf("hello\n");
    ftp_server_close(session);
    return 0;
}

extern inline void ftp_server_init(ftp_server_t *head);

int ftp_server_create(ftp_server_t *head, int efd, const char *host, const char *port)
{
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    struct addrinfo *result, *temp;
    int ret = getaddrinfo(host, port, &hints, &result), error = 0, yes = 1;
    if (ret != 0)
        return ret;
    assert(result);
    ret = EAI_SYSTEM;
    temp = result;
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET;
    while (temp) {
        ftp_server_t *server = malloc(sizeof(ftp_server_t));
        if (server == NULL)
            error = errno;
        else if ((server->fd = socket(temp->ai_family, temp->ai_socktype | SOCK_NONBLOCK,
                                      temp->ai_protocol)) == -1) {
            error = errno;
            free(server);
        } else {
            event.data.ptr = &server->event_data;
            if ((temp->ai_family == AF_INET6 &&
                    setsockopt(server->fd, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof(yes)) == -1) ||
                    getnameinfo(temp->ai_addr, temp->ai_addrlen, server->host, NI_MAXHOST,
                                server->port, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV) == -1 ||
                    bind(server->fd, temp->ai_addr, temp->ai_addrlen) == -1 ||
                    listen(server->fd, SOMAXCONN) == -1 ||
                    epoll_ctl(efd, EPOLL_CTL_ADD, server->fd, &event) == -1) {
                error = errno;
                close(server->fd);
                free(server);
            } else {
                server->family = temp->ai_family;
                server->event_data.callback = ftp_server_callback;
                server->event_data.arg = server;
                server->prev = head->prev;
                head->prev->next = server;
                head->prev = server;
                server->next = head;

                server->clients.next = server->clients.prev = &server->clients;
                server->clients_num = 0;
                ret = 0;
            }
        }
        temp = temp->ai_next;
    }
    freeaddrinfo(result);
    if (error)
        errno = error;
    return ret;
}

int ftp_server_close_all(ftp_server_t *head)
{
    ftp_server_t *temp = head->next;
    int ret = 0, error = 0;
    while (temp != head) {
        // TODO: chain clients too head
        if (close (temp->fd) == -1) {
            error = errno;
            ret = -1;
        }
        temp = temp->next;
        free(temp->prev);
    }
    head->prev = head->next = head;
    if (error)
        errno = error;
    return ret;
}

int ftp_server_close(ftp_server_t *server)
{
    server->prev->next = server->next;
    server->next->prev = server->prev;
    return close(server->fd);
}

