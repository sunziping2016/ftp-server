#ifndef FTP_SERVER_REACTOR_H
#define FTP_SERVER_REACTOR_H

#include <stdint.h>

typedef int (*epoll_callback_t)(uint32_t event, void *arg);

typedef struct {
    epoll_callback_t callback; /**< pointer to callback function. */
    void *arg;                 /**< extra argument for callback */
} epoll_item_t;

#endif
