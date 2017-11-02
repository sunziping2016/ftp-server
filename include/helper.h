#ifndef FTP_SERVER_REACTOR_H
#define FTP_SERVER_REACTOR_H

#define _GNU_SOURCE
#include <stdint.h>
#include <sys/socket.h>

#define ADDRSTRLEN (INET6_ADDRSTRLEN > INET_ADDRSTRLEN ? INET6_ADDRSTRLEN : INET_ADDRSTRLEN)

typedef int (*epoll_callback_t)(uint32_t event, void *arg);

typedef struct {
    epoll_callback_t callback; /**< pointer to callback function. */
    void *arg;                 /**< extra argument for callback */
} epoll_item_t;

int get_addrin_info(const struct sockaddr *addr, char *host, socklen_t host_len, uint16_t *port);
int path_resolve(char *result, const char *from, const char *to, const char *root);

#endif
