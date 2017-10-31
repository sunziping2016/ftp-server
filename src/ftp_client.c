#include <malloc.h>
#include <reactor.h>
#include <sys/epoll.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include "global.h"

typedef int (*ftp_client_command_callback_t)(ftp_client_t *client, char *arg);

typedef struct ftp_client_command_t {
    char *command;
    size_t command_len;
    ftp_client_command_callback_t callback;
} ftp_client_command_t;

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
            ftp_client_write(client, "230 Login successful.\r\n");
            client->logged_in = 1;
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
        ftp_client_write(client, "200 Switching to Binary mode.\r\n");
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

// Trie tree?
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
        {"LIST", 4, NULL},
        {"LPSV", 4, NULL},
        {"LPRT", 4, NULL},
        {"MKD",  3, NULL},
        {"PASS", 4, ftp_client_handle_pass},
        {"PASV", 4, NULL},
        {"PORT", 4, NULL},
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
                size_t i = client->recv_buffer_size;
                if ((client->recv_buffer_size += ret) == CLIENT_RECV_BUFFER_SIZE) {
                    ftp_client_write(client, "500 Input line too long.\r\n");
                    client->exit_on_sent = 1;
                } else {
                    for (; i < client->recv_buffer_size && client->recv_buffer[i] != '\n'; ++i);
                    if (client->recv_buffer_size != i && client->recv_buffer[i] == '\n') {
                        size_t len = i != 0 && client->recv_buffer[i - 1] == '\r' ? i - 1 : i;
                        client->recv_buffer[len] = '\0';
                        ftp_client_command(client, client->recv_buffer);
                        ++i;
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
    {
        char host[NI_MAXHOST], port[NI_MAXSERV];
        getnameinfo(&client->local_addr, client->local_addrlen, host, NI_MAXHOST, port, NI_MAXSERV,
                    NI_NUMERICHOST | NI_NUMERICSERV);
        printf("%s %s\n", host, port);
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
    if (getnameinfo(addr, len, client->peer_host, NI_MAXHOST,
                    client->peer_port, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV) == -1 &&
            global.loglevel >= LOGLEVEL_WARN)
        fprintf(stderr, "W: getnameinfo(client#%d): %s\n", fd, strerror(errno));
    if (global.loglevel >= LOGLEVEL_INFO)
        printf("I: accepted client on descriptor %d (host=%s, port=%s)\n",
               fd, client->peer_host, client->peer_port);
    client->control_event_data.callback = ftp_client_callback;
    client->control_event_data.arg = client;

    client->fd = fd;
    client->data_fd = -1;
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
        if (client->data_fd != -1) {
            if (close(client->data_fd) == -1) {
                if (global.loglevel >= LOGLEVEL_ERROR)
                    fprintf(stderr, "E: close(client.data#%d): %s\n", client->fd, strerror(errno));
                ret = -1;
            }
            global_remove_fd(client->data_fd);
            client->data_fd = -1;
        }
        if (global.loglevel >= LOGLEVEL_INFO)
            printf("I: closed client on descriptor %d (host=%s, port=%s)\n",
                   client->fd, client->peer_host, client->peer_port);
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
            printf("fd: %d\thost: %s\tport: %s\n", temp->fd, temp->peer_host, temp->peer_port);
            temp = temp->next;
        } while (temp != &global.clients);
    }
    return 0;
}

