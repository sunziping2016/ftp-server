#include <malloc.h>
#include <reactor.h>
#include <sys/epoll.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include "global.h"

int ftp_client_command(ftp_client_t *client, char *command, size_t len)
{
    printf("%s %lu\n", command, strlen(command));
    ftp_client_write(client, "%s\r\n", command);
    return 0;
}

int ftp_client_write(ftp_client_t *client, const char *format, ...)
{
    if (client->send_buffer_tail == CLIENT_SEND_BUFFER_SIZE)
        return -1;
    va_list args;
    va_start(args, format);
    int ret = vsnprintf(client->send_buffer + client->send_buffer_tail,
                        CLIENT_SEND_BUFFER_SIZE - client->send_buffer_tail,
                        format, args);
    if (ret < 0) {
        if (global.loglevel >= LOGLEVEL_ERROR)
            fprintf(stderr, "E: vsnprintf(client#%d)\n", client->fd);
    } else if ((client->send_buffer_tail += ret) >= CLIENT_SEND_BUFFER_SIZE) {
        if (global.loglevel >= LOGLEVEL_ERROR)
            fprintf(stderr, "E: client#%d send buffer overflow\n", client->fd);
        client->send_buffer_tail = CLIENT_SEND_BUFFER_SIZE;
        ret = -1;
    }
    if (ret < 0)
        ftp_client_close(client);
    va_end(args);
    return ret;
}

static int ftp_client_update(ftp_client_t *client)
{
    ssize_t ret;
    while (client->fd != -1) {
        if (client->events & EPOLLOUT && client->send_buffer_head != client->send_buffer_tail) {
            ret = write(client->fd, client->send_buffer + client->send_buffer_head,
                        client->send_buffer_tail - client->send_buffer_head);
            if (ret == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    client->events &= ~EPOLLOUT;
                else if (global.loglevel >= LOGLEVEL_ERROR) {
                    fprintf(stderr, "E: write(client#%d): %s\n", client->fd, strerror(errno));
                    break;
                }
            } else if ((client->send_buffer_head += ret) == client->send_buffer_tail) {
                client->send_buffer_head = 0;
                client->send_buffer_tail = 0;
                if (client->exit_on_sent)
                    ftp_client_close(client);
            }
        } else if (client->events & EPOLLIN && client->send_buffer_tail == 0 && !client->busy) {
            ret = read(client->fd, client->recv_buffer + client->recv_buffer_size,
                       CLIENT_RECV_BUFFER_SIZE - client->recv_buffer_size);
            if (ret == 0) {
                ftp_client_close(client);
            } else if (ret == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    client->events &= ~EPOLLIN;
                else if (global.loglevel >= LOGLEVEL_ERROR) {
                    fprintf(stderr, "E: read(client#%d): %s\n", client->fd, strerror(errno));
                    break;
                }
            } else {
                size_t i = client->recv_buffer_size;
                if ((client->recv_buffer_size += ret) == CLIENT_RECV_BUFFER_SIZE) {
                    ftp_client_write(client, "500 Input line too long.\r\n");
                    client->exit_on_sent = 1;
                } else {
                    for (; i < client->recv_buffer_size && client->recv_buffer[i] != '\n'; ++i);
                    if (client->recv_buffer[i] == '\n') {
                        size_t len = i != 0 && client->recv_buffer[i - 1] == '\r' ? i - 1 : i;
                        client->recv_buffer[len] = '\0';
                        ftp_client_command(client, client->recv_buffer, len);
                        ++i;
                        if (client->recv_buffer_size != i)
                            memmove(client->recv_buffer, client->recv_buffer + i, client->recv_buffer_size - i);
                        client->recv_buffer_size -= i;
                    }
                }
            }
        } else
            break;
    }
    return 0;
}

static int ftp_client_callback(uint32_t events, void *arg)
{
    ftp_client_t *session = arg;
    if (events & EPOLLOUT)
        session->events |= EPOLLOUT;
    if (events & EPOLLIN)
        session->events |= EPOLLIN;
    if (events & (EPOLLIN | EPOLLOUT))
        ftp_client_update(session);
    if (events & EPOLLERR || events & EPOLLHUP)
        ftp_client_close(session);
    return 0;
}

void ftp_client_init()
{
    global.clients.fd = -1;
    global.clients.prev = global.clients.next = &global.clients;
}

int ftp_client_add(int fd, struct sockaddr *addr, socklen_t len)
{
    ftp_client_t *client = malloc(sizeof(ftp_client_t));
    if (client == NULL) {
        perror("E: malloc(client)");
        close(fd);
        return -1;
    }
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLOUT | EPOLLET;
    event.data.ptr = &client->event_data;
    if (epoll_ctl(global.epfd, EPOLL_CTL_ADD, fd, &event) == -1) {
        if (global.loglevel >= LOGLEVEL_ERROR)
            fprintf(stderr, "E: epoll_ctl(add: client#%d): %s\n", fd, strerror(errno));
        close(fd);
        free(client);
        return -1;
    }
    if (getnameinfo(addr, len, client->host, NI_MAXHOST,
                    client->port, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV) == -1) {
        if (global.loglevel >= LOGLEVEL_WARN)
            fprintf(stderr, "W: getnameinfo(client#%d): %s\n", fd, strerror(errno));
        client->host[0] = '\0';
        client->port[0] = '\0';
    }
    if (global.loglevel >= LOGLEVEL_INFO)
        printf("I: accepted client on descriptor %d (host=%s, port=%s)\n",
               fd, client->host, client->port);
    client->event_data.callback = ftp_client_callback;
    client->event_data.arg = client;

    client->fd = fd;
    client->busy = 0;
    client->exit_on_sent = 0;
    client->recv_buffer_size = 0;
    client->send_buffer_head = 0;
    client->send_buffer_tail = 0;

    client->prev = global.clients.prev;
    global.clients.prev->next = client;
    client->next = &global.clients;
    global.clients.prev = client;

    global_add_fd(client->fd, FD_FTP_CLIENT, client);
    ++global.epoll_size;

    ftp_client_write(client, "220 (MinimumFTP v0.1.0 alpha)\r\n");
    return 0;
}

int ftp_client_close(ftp_client_t *client)
{
    int ret = 0;
    if (client->fd != -1) {
        client->prev->next = client->next;
        client->next->prev = client->prev;
        if (close(client->fd) == -1) {
            if (global.loglevel >= LOGLEVEL_ERROR)
                fprintf(stderr, "E: close(client#%d): %s\n", client->fd, strerror(errno));
            ret = -1;
        }
        if (global.loglevel >= LOGLEVEL_INFO)
            printf("I: closed client on descriptor %d (host=%s, port=%s)\n",
                   client->fd, client->host, client->port);
        global_remove_fd(client->fd);
        client->fd = -1;
        free_later(client);
        --global.epoll_size;
    }
    return ret;
}

int ftp_client_close_all()
{
    int ret = 0;
    while (&global.clients!= global.clients.next) {
        if (ftp_client_close(global.clients.next) == -1)
            ret = -1;
    }
    return ret;
}

int ftp_client_list()
{
    ftp_client_t *temp = global.clients.next;
    if (temp == &global.clients) {
        printf("No accepted client\n");
    } else {
        do {
            printf("fd: %d\thost: %s\tport: %s\n", temp->fd, temp->host, temp->port);
            temp = temp->next;
        } while (temp != &global.clients);
    }
    return 0;
}

