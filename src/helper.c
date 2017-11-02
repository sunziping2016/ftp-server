#include <netinet/in.h>
#include <arpa/inet.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <limits.h>

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

int _path_resolve_helper(char *result, int pos, const char *from)
{
    while (*from) {
        if (*from == '/') {
            if (result[pos - 1] != '/')
                result[pos++] = '/';
            if (pos == PATH_MAX)
                return -1;
            ++from;
        } else {
            const char *temp = from;
            while (*temp == '.')
                ++temp;
            if (!*temp || *temp == '/') {
                ++from;
                while (*from && *from == '.') {
                    if (pos != 1 && result[pos - 1] == '/')
                        --pos;
                    while (pos != 1 && result[pos - 1] != '/')
                        --pos;
                    ++from;
                }
            } else {
                while (*from && *from != '/') {
                    result[pos++] = *from++;
                    if (pos == PATH_MAX)
                        return -1;
                }
            }
        }
    }
    return pos;
}

int path_resolve(char *result, const char *from, const char *to, const char *root)
{
    int result_pos = 0;
    assert(*from == '/');
    result[result_pos++] = '/';
    if (*to != '/')
        result_pos = _path_resolve_helper(result, result_pos, from);
    else if (root)
        result_pos = _path_resolve_helper(result, result_pos, root);
    if (result_pos == -1)
        return -1;
    if (result[result_pos - 1] != '/') {
        if (result_pos + 1 == PATH_MAX)
            return -1;
        result[result_pos++] = '/';
    }
    result_pos = _path_resolve_helper(result, result_pos, to);
    if (result_pos == -1)
        return -1;
    if (result_pos != 1 && result[result_pos - 1] == '/')
        --result_pos;
    else if (result_pos + 1 == PATH_MAX)
        return -1;
    result[result_pos] = '\0';
    return result_pos;
}
