#include <netinet/in.h>
#include <arpa/inet.h>
#include <stddef.h>

#include "helper.h"

int get_addrin_info(const struct sockaddr *addr, char *host, socklen_t host_len, uint16_t *port)
{
    uint16_t temp_port;
    const void *any_addr;
    if (addr->sa_family == AF_INET) {
        const struct sockaddr_in *temp_addr = (const struct sockaddr_in *) addr;
        any_addr = &temp_addr->sin_addr;
        temp_port = temp_addr->sin_port;
    } else {
        const struct sockaddr_in6 *temp_addr = (const struct sockaddr_in6 *) addr;
        any_addr = &temp_addr->sin6_addr;
        temp_port = temp_addr->sin6_port;
    }
    if (inet_ntop(addr->sa_family, any_addr, host, host_len) == NULL)
        return -1;
    *port = htons(temp_port);
    return 0;
}
