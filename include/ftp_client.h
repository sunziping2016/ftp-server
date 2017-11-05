#ifndef FTP_SERVER_FTP_CLIENT_H
#define FTP_SERVER_FTP_CLIENT_H

#include <netdb.h>
#include "helper.h"

#define CLIENT_RECV_BUFFER_SIZE 4096
#define CLIENT_SEND_BUFFER_SIZE 4096

#define CLIENT_CHUNK_SIZE 4096
#define CLIENT_MAX_CHUNK_NUM (1024 * 16)
#define CLIENT_MAX_CHUNK_POOL_NUM (1024 * 512)

enum ftp_client_data_command_t {
    DC_RETR,
    DC_STOR,
    DC_LIST
};

typedef struct ftp_client_truck_t {
    char chunk[CLIENT_CHUNK_SIZE];
    struct ftp_client_truck_t *next;
} ftp_client_truck_t;

typedef struct ftp_client_t {
    epoll_item_t control_event_data;
    int fd;
    uint32_t control_events;

    epoll_item_t data_read_event_data;
    epoll_item_t data_write_event_data;
    int data_read_fd, data_write_fd;
    uint32_t data_events;

    epoll_item_t pasv_event_data;
    int local_fd, remote_fd, pasv_fd;
    struct sockaddr_storage port_addr;
    socklen_t port_addrlen;

    enum ftp_client_data_command_t data_command;
    ftp_client_truck_t *head, *tail;
    size_t head_pos, tail_pos, chunk_num;
    ssize_t data_size;

    char *user, *root;
    size_t root_len;
    int processing, busy, logged_in;
    int exit_on_sent;
    char wd[PATH_MAX];

    struct sockaddr_storage local_addr, peer_addr;
    socklen_t local_addrlen, peer_addrlen;
    char local_host[ADDRSTRLEN];
    char peer_host[ADDRSTRLEN];
    uint16_t local_port, peer_port;

    char recv_buffer[CLIENT_RECV_BUFFER_SIZE];
    char send_buffer[CLIENT_SEND_BUFFER_SIZE];

    size_t recv_buffer_size;
    size_t send_buffer_head, send_buffer_tail;

    struct ftp_client_t *prev, *next;

} ftp_client_t;

void ftp_client_init();
int ftp_client_add(int fd, struct sockaddr_storage *addr, socklen_t len);
int ftp_client_close(ftp_client_t *client);
int ftp_client_close_all();
int ftp_client_list();
int ftp_client_update(ftp_client_t *client);

int ftp_client_write(ftp_client_t *client, const char *format, ...);
int ftp_client_command(ftp_client_t *client, char *input);

#endif