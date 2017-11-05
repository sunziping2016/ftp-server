#ifndef FTP_SERVER_USER_H
#define FTP_SERVER_USER_H

typedef struct ftp_user_data_t {
    char *password;
    char *root;
} ftp_user_data_t;

typedef struct hash_table *ftp_users_t;

void ftp_users_init();
int ftp_users_start();
int ftp_users_stop();

int ftp_users_add(const char *username, const char *password, const char *root, int hashed);
int ftp_users_remove(const char *username);
int ftp_users_check(const char *username, const char *password, ftp_user_data_t **result);
int ftp_users_list();

#endif