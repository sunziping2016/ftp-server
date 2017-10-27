#include <malloc.h>
#include <reactor.h>
#include <sys/epoll.h>
#include "ftp_client.h"

int ftp_client_callback(uint32_t events, void *arg)
{
    ftp_client_t *session = arg;
    return 0;
}

int ftp_client_add(ftp_client_t *head, int epfd, int fd, struct sockaddr *addr, socklen_t len)
{
    ftp_client_t *client = malloc(sizeof(ftp_client_t));
    if (client == NULL)
        return -1;
    client->event_data.arg = client;
    client->event_data.callback = ftp_client_callback;
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLOUT | EPOLLET;
    event.data.ptr = &client->event_data;
    return 0;
}
