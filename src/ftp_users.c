#include <malloc.h>
#include "global.h"
#include "bcrypt.h"
#include "string.h"
#include "hash_table.h"

// From https://stackoverflow.com/questions/7666509/hash-function-for-string
static uint32_t hash_function(const void *key)
{
    const char *str = key;
    uint32_t hash = 5381, c;
    while ((c = (uint32_t) *str++))
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    return hash;
}

static int key_equals_function(const void *a, const void *b)
{
    return !strcmp(a, b);
}

void delete_function(struct hash_entry *entry)
{
    ftp_user_data_t *data = entry->data;
    free((void *) entry->key);
    free((void *) data->password);
    free((void *) data->root);
    free((void *) data);
}

void ftp_users_init()
{
    global.users = NULL;
}

int ftp_users_start()
{
    global.users = hash_table_create(hash_function, key_equals_function);
    if (!global.users) {
        if (global.loglevel >= LOGLEVEL_ERROR)
            fprintf(stderr, "E: hash_table_create\n");
        return -1;
    }
    return 0;
}

int ftp_users_add(const char *username, const char *password, const char *root)
{
    uint32_t hash = hash_function(username);
    if (hash_table_search_pre_hashed(global.users, hash, username)) {
        if (global.loglevel >= LOGLEVEL_ERROR)
            fprintf(stderr, "E: user already exists\n");
        return 1;
    }

    char *key = malloc(strlen(username) + 1);
    if (key == NULL) {
        if (global.loglevel >= LOGLEVEL_ERROR)
            perror("E: malloc(user.key)");
        return -1;
    }
    strcpy(key, username);
    ftp_user_data_t *data = malloc(sizeof(ftp_user_data_t));
    if (data == NULL) {
        if (global.loglevel >= LOGLEVEL_ERROR)
            perror("E: malloc(user.data)");
        free(key);
        return -1;
    }
    if (password) {
        data->password = malloc(BCRYPT_HASHSIZE);
        if (!data->password) {
            if (global.loglevel >= LOGLEVEL_ERROR)
                perror("E: malloc(user.data.password)");
            free(key);
            free(data);
            return -1;
        }
        char salt[BCRYPT_HASHSIZE];
        if (bcrypt_gensalt(12, salt) != 0) {
            if (global.loglevel >= LOGLEVEL_ERROR)
                fprintf(stderr, "E: bcrypt_gensalt\n");
            free(data->password);
            free(key);
            free(data);
            return -1;
        }
        if(bcrypt_hashpw(password, salt, data->password) != 0) {
            if (global.loglevel >= LOGLEVEL_ERROR)
                fprintf(stderr, "E: bcrypt_hashpw\n");
            free(data->password);
            free(key);
            free(data);
            return -1;
        }
    } else
        data->password = NULL;
    if (root) {
        data->root = malloc(strlen(root) + 1);
        if (!data->root) {
            if (global.loglevel >= LOGLEVEL_ERROR)
                perror("E: malloc(user.root)");
            free(data->password);
            free(key);
            free(data);
            return -1;
        }
        strcpy(data->root, root);
    } else
        data->root = NULL;
    if (!hash_table_insert_pre_hashed(global.users, hash, key, data)) {
        if (global.loglevel >= LOGLEVEL_ERROR)
            fprintf(stderr, "E: hash_table_insert\n");
        free(data->root);
        free(data->password);
        free(key);
        free(data);
        return -1;
    }
    return 0;
}

int ftp_users_check(const char *username, const char *password, ftp_user_data_t **result)
{
    struct hash_entry *user = hash_table_search(global.users, username);
    if (!user)
        return 1;
    ftp_user_data_t *data = user->data;
    if (data->password) {
        if (!password)
            return 1;
        int ret = bcrypt_checkpw(password, data->password);
        if (ret == -1) {
            if (global.loglevel >= LOGLEVEL_ERROR)
                fprintf(stderr, "E: bcrypt_checkpw\n");
            return -1;
        } else if (ret > 0)
            return 1;
    }
    *result = data;
    return 0;
}


int ftp_users_stop()
{
    if (global.users) {
        hash_table_destroy(global.users, delete_function);
        global.users = NULL;
    }
    return 0;
}

static const char * yes_or_no[] = {"no", "yes"};

int ftp_users_list()
{
    int users = 0;
    struct hash_entry *entry;
    hash_table_foreach(global.users, entry) {
        ftp_user_data_t *data = entry->data;
        printf("user: %-16s\tpassword: %-6s\troot: %s\n", (char *) entry->key,
               yes_or_no[data->password != NULL], data->root ? data->root : "/");
        ++users;
    }
    if (!users)
        printf("No users\n");
    return 0;
}
