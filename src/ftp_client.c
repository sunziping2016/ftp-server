#include "global.h"

#include <malloc.h>
#include <sys/epoll.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <assert.h>
#include <fcntl.h>

typedef int (*ftp_client_command_callback_t)(ftp_client_t *client, char *arg);

typedef struct ftp_client_command_t {
    char *command;
    size_t command_len;
    ftp_client_command_callback_t callback;
} ftp_client_command_t;

ftp_client_truck_t *ftp_client_chunk_pool;
size_t ftp_client_chunk_pool_num;

static int add_data_trunk(ftp_client_t *client)
{
    ftp_client_truck_t *temp;
    if (ftp_client_chunk_pool) {
        temp = ftp_client_chunk_pool;
        ftp_client_chunk_pool = ftp_client_chunk_pool->next;
        --ftp_client_chunk_pool_num;
    } else {
        temp = malloc(sizeof(ftp_client_truck_t));
        if (!temp) {
            if (global.loglevel >= LOGLEVEL_ERROR)
                perror("E: malloc(chunk)");
            return -1;
        }
    }
    temp->next = NULL;
    if (client->tail) {
        client->tail->next = temp;
        client->tail = temp;
    } else
        client->head = client->tail = temp;
    ++client->chunk_num;
    return 0;
}

static void remove_data_trunk(ftp_client_t *client)
{
    assert(client->head != NULL);
    ftp_client_truck_t *temp;
    if (client->head == client->tail) {
        temp = client->head;
        client->head = client->tail = NULL;
    } else {
        temp = client->head;
        client->head = client->head->next;
    }
    --client->chunk_num;
    if (ftp_client_chunk_pool_num < CLIENT_MAX_CHUNK_POOL_NUM) {
        temp->next = ftp_client_chunk_pool;
        ftp_client_chunk_pool = temp;
        ++ftp_client_chunk_pool_num;
    } else
        free(temp);
}

static void clear_data_connection(ftp_client_t *client)
{
    if (client->data_read_fd != -1) {
        close(client->data_read_fd);
        global_remove_fd(client->data_read_fd);
        client->data_read_fd = -1;
    }
    if (client->data_write_fd != -1) {
        close(client->data_write_fd);
        global_remove_fd(client->data_write_fd);
        client->data_write_fd = -1;
    }
    if (client->remote_fd != -1) {
        close(client->remote_fd);
        global_remove_fd(client->remote_fd);
        client->remote_fd = -1;
    }
    if (client->pasv_fd != -1) {
        close(client->pasv_fd);
        global_remove_fd(client->pasv_fd);
        client->pasv_fd = -1;
    }
    while (client->head)
        remove_data_trunk(client);
    client->port_addrlen = 0;
}

static int ftp_client_handle_user(ftp_client_t *client, char *arg)
{
    int ret = -1;
    if (client->logged_in)
        ftp_client_write(client, "530 Can't change to another user.\r\n");
    else if (!*arg)
        ftp_client_write(client, "500 Requires username.\r\n");
    else {
        if (client->user)
            free(client->user);
        client->user = malloc(strlen(arg) + 1);
        if (!client->user)
            ftp_client_write(client, "451 Server temporary unavailable.\r\n");
        else {
            strcpy(client->user, arg);
            ftp_client_write(client, "331 Please specify the password.\r\n");
        }
    }
    return ret;
}

static int ftp_client_wrong_pass(ftp_timer_t *timer, void *arg)
{
    (void) timer;
    ftp_client_t *client = arg;
    ftp_client_write(client, "530 Login incorrect.\r\n");
    client->busy = 0;
    ftp_client_update(client);
    return 0;
}

static int ftp_client_handle_pass(ftp_client_t *client, char *arg)
{
    int ret = -1;
    if (client->logged_in)
        ftp_client_write(client, "230 Already logged in.\r\n");
    else if (!client->user)
        ftp_client_write(client, "503 Login with USER first.\r\n");
    else {
        ftp_user_data_t *data;
        if ((ret = ftp_users_check(client->user, arg, &data)) == -1)
            ftp_client_write(client, "451 Server temporary unavailable.\r\n");
        else if (ret == 1) {
            free(client->user);
            client->user = NULL;
            struct itimerspec timer = {0};
            timer.it_value.tv_sec = 3;
            if (ftp_timer_add(&timer, ftp_client_wrong_pass, client, 1) != -1)
                client->busy = 1;
            else
                ftp_client_write(client, "451 Server temporary unavailable.\r\n");
            ret = -1;
        } else {
            client->root = malloc(strlen(data->root) + 1);
            if (!client->root) {
                ftp_client_write(client, "451 Server temporary unavailable.\r\n");
                ret = -1;
            } else {
                strcpy(client->root, data->root);
                strcpy(client->wd, client->root);
                ftp_client_write(client, "230 Login successful.\r\n");
                client->logged_in = 1;
            }
        }
    }
    return ret;
}

static int ftp_client_handle_syst(ftp_client_t *client, char *arg)
{
    (void) arg;
    ftp_client_write(client, "215 UNIX Type: L8\r\n");
    return 0;
}

static int ftp_client_handle_type(ftp_client_t *client, char *arg)
{
    if (strcasecmp(arg, "a") == 0)
        ftp_client_write(client, "200 Switching to ASCII mode.\r\n");
    else if (strcasecmp(arg, "i") == 0)
        /*ftp_client_write(client, "200 Switching to Binary mode.\r\n");*/
        ftp_client_write(client, "200 Type set to I.\r\n");
    else {
        ftp_client_write(client, "500 Unrecognised TYPE command.\r\n");
        return -1;
    }
    return 0;
}

static int ftp_client_handle_quit(ftp_client_t *client, char *arg)
{
    (void) arg;
    ftp_client_write(client, "221 Goodbye.\r\n");
    client->exit_on_sent = 1;
    return 0;
}

static epoll_callback_t ftp_data_update(ftp_client_t *client)
{
    int status = 0;
    while (client->data_write_fd != -1) {
        int has_read = client->head && (client->head != client->tail || client->head_pos < client->tail_pos);
        if (client->data_read_fd == -1 && !has_read) {
            close(client->data_write_fd);
            global_remove_fd(client->data_write_fd);
            client->data_write_fd = -1;
        } else if (client->data_events & EPOLLOUT && has_read) {
            ftp_client_truck_t *head = client->head;
            ssize_t ret = write(client->data_write_fd, head->chunk + client->head_pos, client->head == client->tail ?
                                client->tail_pos - client->head_pos : CLIENT_CHUNK_SIZE - client->head_pos);
            if (ret == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    client->data_events &= ~EPOLLOUT;
                else {
                    if (global.loglevel >= LOGLEVEL_ERROR)
                        fprintf(stderr, "E: write(fd: client#%d.write#%d): %s\n",
                                client->fd, client->data_write_fd, strerror(errno));
                    ftp_client_write(client, "426 Failure writing stream.\r\n");
                    status = 1;
                }
                break;
            } else
                client->head_pos += ret;
            if (client->head_pos == CLIENT_CHUNK_SIZE) {
                remove_data_trunk(client);
                client->head_pos = 0;
            }
        } else if (client->data_read_fd != -1 && client->data_events & EPOLLIN &&
                (client->chunk_num < CLIENT_MAX_CHUNK_NUM || client->tail_pos < CLIENT_CHUNK_SIZE)) {
            if (!client->tail) {
                if (add_data_trunk(client) == -1) {
                    ftp_client_write(client, "426 Server temporary unavailable.\r\n");
                    status = 1;
                    break;
                }
                client->tail_pos = 0;
            }
            ftp_client_truck_t *tail = client->tail;
            ssize_t ret = read(client->data_read_fd, tail->chunk + client->tail_pos, CLIENT_CHUNK_SIZE - client->tail_pos);
            if (ret == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    client->data_events &= ~EPOLLIN;
                else {
                    if (global.loglevel >= LOGLEVEL_ERROR)
                        fprintf(stderr, "E: read(fd: client#%d.read#%d): %s\n",
                                client->fd, client->data_read_fd, strerror(errno));
                    ftp_client_write(client, "426 Failure reading stream.\r\n");
                    status = 1;
                }
                break;
            } else if (ret == 0) {
                close(client->data_read_fd);
                global_remove_fd(client->data_read_fd);
                client->data_read_fd = -1;
            } else
                client->tail_pos += ret;
            if (client->tail_pos == CLIENT_CHUNK_SIZE && client->chunk_num < CLIENT_MAX_CHUNK_NUM) {
                if (add_data_trunk(client) == -1) {
                    ftp_client_write(client, "426 Server temporary unavailable.\r\n");
                    status = 1;
                    break;
                }
                client->tail_pos = 0;
            }
        } else
            break;

    }
    if (status) {
        clear_data_connection(client);
        client->busy = 0;
        ftp_client_update(client);
    } else if (client->data_read_fd == -1 && client->data_write_fd == -1) {
        clear_data_connection(client);
        client->busy = 0;
        ftp_client_write(client, "226 Transfer complete.\r\n");
        ftp_client_update(client);
    }
    return 0;
}


static int ftp_client_data_read_callback(uint32_t events, void *arg)
{
    ftp_client_t *client = arg;
    uint32_t filter = events & EPOLLIN;
    if (filter) {
        client->data_events |= filter;
        ftp_data_update(client);
    }
    if (client->busy && client->data_read_fd != -1 && events & EPOLLERR) {
        clear_data_connection(client);
        client->busy = 0;
        ftp_client_write(client, "426 Failure reading stream.\r\n");
        ftp_client_update(client);
    }
    return 0;
}

static int ftp_client_data_write_callback(uint32_t events, void *arg)
{
    ftp_client_t *client = arg;
    uint32_t filter = events & EPOLLOUT;
    if (filter) {
        client->data_events |= filter;
        ftp_data_update(client);
    }
    if (client->busy && client->data_write_fd != -1 && events & (EPOLLHUP | EPOLLERR)) {
        clear_data_connection(client);
        client->busy = 0;
        ftp_client_write(client, "426 Failure writing stream.\r\n");
        ftp_client_update(client);
    }
    return 0;
}

static int ftp_client_data_check_startable(ftp_client_t *client)
{
    return client->remote_fd != -1 || client->pasv_fd != -1 || client->port_addrlen != 0;
}

static int ftp_client_data_try_start(ftp_client_t *client)
{
    int ret = -1;
    if (client->local_fd != -1 && client->port_addrlen) {
        client->remote_fd = socket(client->port_addr.sa_family, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (client->remote_fd == -1) {
            clear_data_connection(client);
            client->busy = 0;
            ftp_client_write(client, "425 Server temporary unavailable.\r\n");
            ftp_client_update(client);
            return -1;
        }
        if (connect(client->remote_fd, &client->port_addr, client->port_addrlen) == -1 &&
                errno != EINPROGRESS) {
            if (global.loglevel >= LOGLEVEL_ERROR)
                fprintf(stderr, "E: connect(client#%d.port#%d): %s\n", client->fd,
                        client->local_fd, strerror(errno));
            clear_data_connection(client);
            client->busy = 0;
            ftp_client_write(client, "425 Can't open data connection.\r\n");
            ftp_client_update(client);
            return -1;
        }
        global_add_fd(client->remote_fd, FD_FTP_PORT_CLIENT, client);
    }
    if (client->local_fd != -1 && client->remote_fd != -1) {
        if (client->data_command == DC_RETR || client->data_command == DC_LIST) {
            client->data_read_fd = client->local_fd;
            client->data_write_fd = client->remote_fd;
        } else {
            client->data_write_fd = client->local_fd;
            client->data_read_fd = client->remote_fd;
        }
        client->local_fd = -1;
        client->remote_fd = -1;
        struct epoll_event event;
        event.events = EPOLLIN | EPOLLET;
        event.data.ptr = &client->data_read_event_data;
        if (epoll_ctl(global.epfd, EPOLL_CTL_ADD, client->data_read_fd, &event) == -1 &&
                global.loglevel >= LOGLEVEL_ERROR)
            fprintf(stderr, "E: epoll_ctl(add: client#%d.read#%d): %s\n",
                    client->fd, client->data_read_fd, strerror(errno));
        else {
            event.events = EPOLLOUT | EPOLLET;
            event.data.ptr = &client->data_write_event_data;
            if (epoll_ctl(global.epfd, EPOLL_CTL_ADD, client->data_write_fd, &event) == -1 && global.loglevel >= LOGLEVEL_ERROR)
                fprintf(stderr, "E: epoll_ctl(add: client#%d.read#%d): %s\n",
                        client->fd, client->data_read_fd, strerror(errno));
            else {
                client->data_events = 0;
                client->head_pos = client->tail_pos = 0;
                ret = 0;
            }
        }
    } else
        return 0;
    if (ret == -1) {
        clear_data_connection(client);
        client->busy = 0;
        ftp_client_write(client, "425 Server temporary unavailable.\r\n");
    } else
        ftp_client_write(client, "150 Here comes the directory listing.\r\n");
    ftp_client_update(client);
    return ret;
}

static int ftp_client_handle_list(ftp_client_t *client, char *arg)
{
    (void) arg;
    int pipe_fd[2], ret = -1, pid;
    if (ftp_client_data_check_startable(client) == 0) {
        ftp_client_write(client, "425 Use PORT or PASV first.\r\n");
        return -1;
    }
    if (pipe(pipe_fd) == -1) {
        if (global.loglevel >= LOGLEVEL_ERROR)
            perror("E: pipe");
    } else if ((pid = fork()) == -1) {
        if (global.loglevel >= LOGLEVEL_ERROR)
            perror("E: fork");
        close(pipe_fd[0]);
        close(pipe_fd[1]);
    } else if (pid == 0) {
        dup2(pipe_fd[1], STDOUT_FILENO);
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        execlp("ls", "ls", "-n", client->wd, NULL);
    } else {
        close(pipe_fd[1]);
        client->local_fd = pipe_fd[0];
        int flags = fcntl(client->local_fd, F_GETFL);
        fcntl(client->local_fd, F_SETFL, flags | O_NONBLOCK);
        global_add_fd(client->local_fd, FD_PIPE_READ, client);
        client->data_command = DC_LIST;
        client->busy = 1;
        ftp_client_data_try_start(client);
        ret = 0;
    }
    if (ret == -1)
        ftp_client_write(client, "425 Server temporary unavailable.\r\n");
    return ret;
}

static int ftp_client_pasv_callback(uint32_t event, void *arg)
{
    ftp_client_t *client = arg;
    if (event & EPOLLIN) {
        int fd = accept4(client->pasv_fd, NULL, NULL, SOCK_NONBLOCK);
        if (fd == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && global.loglevel >= LOGLEVEL_ERROR)
                fprintf(stderr, "E: accept(client#%d.pasv-server#%d): %s\n", client->fd, client->pasv_fd, strerror(errno));
            return 0;
        } else {
            close(client->pasv_fd);
            global_remove_fd(client->pasv_fd);
            client->pasv_fd = -1;
            client->remote_fd = fd;
            global_add_fd(fd, FD_FTP_PASV_CLIENT, client);
            ftp_client_data_try_start(client);
        }
    }
    if (event & (EPOLLERR | EPOLLHUP)) {
        close(client->pasv_fd);
        global_remove_fd(client->pasv_fd);
        client->pasv_fd = -1;
    }
    return 0;
}

static int ftp_client_handle_pasv(ftp_client_t *client, char *arg)
{
    (void) arg;
    int ret = -1;
    clear_data_connection(client);
    if (client->local_addr.sa_family != AF_INET) {
        ftp_client_write(client, "522: No IPv4 address available for PASV. Use EPSV.\r\n");
        return -1;
    }
    client->pasv_fd = socket(client->local_addr.sa_family, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (client->pasv_fd == -1) {
        if (global.loglevel >= LOGLEVEL_ERROR)
            fprintf(stderr, "E: socket(client#%d.pasv-server): %s\n", client->fd, strerror(errno));
    } else {
        struct sockaddr_in addr = *(struct sockaddr_in *) &client->local_addr;
        addr.sin_port = 0;
        socklen_t addrlen = sizeof(addr);
        if (bind(client->pasv_fd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
            if (global.loglevel >= LOGLEVEL_ERROR)
                fprintf(stderr, "E: bind(client#%d.pasv-server#%d): %s\n", client->fd, client->pasv_fd, strerror(errno));
            close(client->pasv_fd);
            client->pasv_fd = -1;
        } else if (getsockname(client->pasv_fd, (struct sockaddr *) &addr, &addrlen) == -1) {
            if (global.loglevel >= LOGLEVEL_ERROR)
                fprintf(stderr, "E: getsockname(client#%d.pasv-server#%d): %s\n", client->fd, client->pasv_fd, strerror(errno));
            close(client->pasv_fd);
            client->pasv_fd = -1;
        } else if (listen(client->pasv_fd, SOMAXCONN) == -1) {
            if (global.loglevel >= LOGLEVEL_ERROR)
                fprintf(stderr, "E: listen(client#%d.pasv-server#%d): %s\n", client->fd, client->pasv_fd, strerror(errno));
            close(client->pasv_fd);
            client->pasv_fd = -1;
        } else {
            struct epoll_event event;
            event.events = EPOLLIN | EPOLLET;
            event.data.ptr = &client->pasv_event_data;
            if (epoll_ctl(global.epfd, EPOLL_CTL_ADD, client->pasv_fd, &event) == -1) {
                if (global.loglevel >= LOGLEVEL_ERROR)
                    fprintf(stderr, "E: epoll_ctl(add: client#%d.pasv-server#%d): %s\n", client->fd, client->pasv_fd, strerror(errno));
                close(client->pasv_fd);
                client->pasv_fd = -1;
            } else {
                client->pasv_event_data.callback = ftp_client_pasv_callback;
                client->pasv_event_data.arg = client;

                global_add_fd(client->pasv_fd, FD_FTP_PASV_SERVER, client);
                unsigned char *host = (unsigned char *) &addr.sin_addr, *port = (unsigned char *) &addr.sin_port;
                ftp_client_write(client, "227 Entering Passive Mode (%u,%u,%u,%u,%u,%u).\r\n",
                                 host[0], host[1], host[2], host[3], port[0], port[1]);
                ret = 0;
            }
        }
    }
    if (ret == -1)
        ftp_client_write(client, "451 Server temporary unavailable.\r\n");
    return 0;
}

static int ftp_client_handle_port(ftp_client_t *client, char *arg)
{
    struct sockaddr_in *addr = (struct sockaddr_in *) &client->port_addr,
            *remote = (struct sockaddr_in *) &client->peer_addr;
    unsigned char *host = (unsigned char *) &addr->sin_addr, *port = (unsigned char *) &addr->sin_port;
    if (remote->sin_family != AF_INET ||
            sscanf(arg, "%hhu,%hhu,%hhu,%hhu,%hhu,%hhu", host, host + 1, host + 2, host + 3, port, port + 1) != 6 ||
            remote->sin_addr.s_addr != addr->sin_addr.s_addr) {
        ftp_client_write(client, "500 Illegal PORT command.\r\n");
        return -1;
    }
    addr->sin_family = AF_INET;
    client->port_addrlen = sizeof(struct sockaddr_in);
    ftp_client_write(client, "200 PORT command successful. Consider using PASV.\r\n");
    return 0;
}

ftp_client_command_t *ftp_client_lookup_command(ftp_client_command_t *commands, size_t len, const char *input)
{
    size_t begin = 0, middle;
    int ret;
    ftp_client_command_t *command;
    while (begin < len) {
        middle = begin + (len - begin) / 2;
        command = commands + middle;
        ret = strncasecmp(command->command, input, command->command_len);
        if (ret == 0) {
            if (input[command->command_len] == '\0' || input[command->command_len] == ' ')
                return command;
            begin = middle + 1;
        } else if (ret < 0)
            begin = middle + 1;
        else
            len = middle;
    }
    return NULL;
}

ftp_client_command_t before_login[] = {
        {"PASS", 4, ftp_client_handle_pass},
        {"QUIT", 4, ftp_client_handle_quit},
        {"USER", 4, ftp_client_handle_user}
}, after_login[] = {
        {"CWD",  3, NULL},
        {"EPSV", 4, NULL},
        {"EPRT", 4, NULL},
        {"LIST", 4, ftp_client_handle_list},
        {"LPSV", 4, NULL},
        {"LPRT", 4, NULL},
        {"MKD",  3, NULL},
        {"PASS", 4, ftp_client_handle_pass},
        {"PASV", 4, ftp_client_handle_pasv},
        {"PORT", 4, ftp_client_handle_port},
        {"QUIT", 4, ftp_client_handle_quit},
        {"RETR", 4, NULL},
        {"RMD",  4, NULL},
        {"STOR", 4, NULL},
        {"SYST", 4, ftp_client_handle_syst},
        {"TYPE", 4, ftp_client_handle_type},
        {"USER", 4, ftp_client_handle_user}
};

int ftp_client_command(ftp_client_t *client, char *input)
{
    if (!*input)
        return 0;
    ftp_client_command_t *command;
    if (!client->logged_in)
        command = ftp_client_lookup_command(before_login, sizeof(before_login) / sizeof(before_login[0]), input);
    else
        command = ftp_client_lookup_command(after_login, sizeof(after_login) / sizeof(after_login[0]), input);
    if (command) {
        if (command->callback)
            return (*command->callback)(client, input[command->command_len] == ' ' ?
                                                input + command->command_len + 1 : input + command->command_len);
        else
            ftp_client_write(client, "502 Command not implemented.\r\n");
    } else if (client->logged_in)
        ftp_client_write(client, "500 Unknown command.\r\n");
    else
        ftp_client_write(client, "530 Please login with USER and PASS.\r\n");
    return -1;
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

int ftp_client_update(ftp_client_t *client)
{
    ssize_t ret;
    if (client->processing)
        return 0;
    client->processing = 1;
    size_t scan = client->recv_buffer_size;
    while (client->fd != -1) {
        if (client->control_events & EPOLLOUT && client->send_buffer_head != client->send_buffer_tail) {
            ret = write(client->fd, client->send_buffer + client->send_buffer_head,
                        client->send_buffer_tail - client->send_buffer_head);
            if (ret == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    client->control_events &= ~EPOLLOUT;
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
        } else if (client->send_buffer_tail == 0 && !client->busy && scan != client->recv_buffer_size) {
            for (; scan < client->recv_buffer_size && client->recv_buffer[scan] != '\n'; ++scan);
            if (scan != client->recv_buffer_size) {
                size_t len = scan != 0 && client->recv_buffer[scan - 1] == '\r' ? scan - 1 : scan;
                client->recv_buffer[len] = '\0';
                ftp_client_command(client, client->recv_buffer);
                ++scan;
                if (client->recv_buffer_size - scan)
                    memmove(client->recv_buffer, client->recv_buffer + scan, client->recv_buffer_size - scan);
                client->recv_buffer_size -= scan;
                scan = 0;
            }
        } else if (client->control_events & EPOLLIN && client->send_buffer_tail == 0 && !client->busy) {
            ret = read(client->fd, client->recv_buffer + client->recv_buffer_size,
                       CLIENT_RECV_BUFFER_SIZE - client->recv_buffer_size);
            if (ret == 0) {
                ftp_client_close(client);
            } else if (ret == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    client->control_events &= ~EPOLLIN;
                else if (global.loglevel >= LOGLEVEL_ERROR) {
                    fprintf(stderr, "E: read(client#%d): %s\n", client->fd, strerror(errno));
                    break;
                }
            } else {
                scan = client->recv_buffer_size;
                if ((client->recv_buffer_size += ret) == CLIENT_RECV_BUFFER_SIZE) {
                    ftp_client_write(client, "500 Input line too long.\r\n");
                    scan = CLIENT_RECV_BUFFER_SIZE;
                    client->exit_on_sent = 1;
                }
            }
        } else
            break;
    }
    client->processing = 0;
    return 0;
}

static int ftp_client_callback(uint32_t events, void *arg)
{
    ftp_client_t *session = arg;
    if (events & EPOLLOUT)
        session->control_events |= EPOLLOUT;
    if (events & EPOLLIN)
        session->control_events |= EPOLLIN;
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
        if (global.loglevel >= LOGLEVEL_ERROR)
            perror("E: malloc(client)");
        close(fd);
        return -1;
    }
    memset(client, 0, sizeof(ftp_client_t));
    client->local_addrlen = sizeof(client->local_addr);
    if (getsockname(fd, &client->local_addr, &client->local_addrlen) == -1) {
        if (global.loglevel >= LOGLEVEL_ERROR)
            fprintf(stderr, "E: getsockname(client#%d): %s\n", fd, strerror(errno));
        close(fd);
        free(client);
        return -1;
    }
    if (get_addrin_info(&client->local_addr, client->local_host,
                        sizeof(client->local_host), &client->local_port) == -1) {
        if (global.loglevel >= LOGLEVEL_ERROR)
            fprintf(stderr, "E: inet_ntop(client#%d.local): %s\n", fd, strerror(errno));
        close(fd);
        free(client);
        return -1;
    }
    if (get_addrin_info(addr, client->peer_host,
                        sizeof(client->peer_host), &client->peer_port) == -1) {
        if (global.loglevel >= LOGLEVEL_ERROR)
            fprintf(stderr, "E: inet_ntop(client#%d.peer): %s\n", fd, strerror(errno));
        close(fd);
        free(client);
        return -1;
    }
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLOUT | EPOLLET;
    event.data.ptr = &client->control_event_data;
    if (epoll_ctl(global.epfd, EPOLL_CTL_ADD, fd, &event) == -1) {
        if (global.loglevel >= LOGLEVEL_ERROR)
            fprintf(stderr, "E: epoll_ctl(add: client#%d): %s\n", fd, strerror(errno));
        close(fd);
        free(client);
        return -1;
    }
    if (global.loglevel >= LOGLEVEL_INFO)
        printf("I: accepted client on descriptor %d (host=%s, port=%u)\n",
               fd, client->peer_host, client->peer_port);
    client->control_event_data.callback = ftp_client_callback;
    client->control_event_data.arg = client;

    client->data_read_event_data.callback = ftp_client_data_read_callback;
    client->data_read_event_data.arg = client;
    client->data_write_event_data.callback = ftp_client_data_write_callback;
    client->data_write_event_data.arg = client;

    client->fd = fd;
    client->local_fd = -1;
    client->remote_fd = -1;
    client->pasv_fd = -1;
    client->data_read_fd = -1;
    client->data_write_fd = -1;
    client->peer_addr = *addr;
    client->peer_addrlen = len;

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
            printf("I: closed client on descriptor %d (host=%s, port=%u)\n",
                   client->fd, client->peer_host, client->peer_port);
        clear_data_connection(client);
        global_remove_fd(client->fd);
        client->fd = -1;
        free_later(client);
        free_later(client->user);
        free_later(client->root);
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
            printf("fd: %d\thost: %s\tport: %u\n", temp->fd, temp->peer_host, temp->peer_port);
            temp = temp->next;
        } while (temp != &global.clients);
    }
    return 0;
}

