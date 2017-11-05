#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <malloc.h>
#include <stdlib.h>

#include "bcrypt.h"
#include "global.h"

static int ftp_cli_help(char *arg);
static int ftp_cli_exit();
static int ftp_cli_run();

const char *banner = ""
        "Minimum ftp server implemented by Sun Ziping\n"
        "%s\n"
        "Type \"help\" for more information\n";

static int ftp_cli_add_server(char *arg)
{
    char *argv[2] = {
            "localhost",
            "21"
    };
    if (parse_arguments(arg, 2, argv) == -1)
        return -1;
    return ftp_server_create(argv[0], argv[1], AF_UNSPEC);
}

static int ftp_cli_add_server4(char *arg)
{
    char *argv[2] = {
            "localhost",
            "21"
    };
    if (parse_arguments(arg, 2, argv) == -1)
        return -1;
    return ftp_server_create(argv[0], argv[1], AF_INET);
}

static int ftp_cli_add_server6(char *arg)
{
    char *argv[2] = {
            "localhost",
            "21"
    };
    if (parse_arguments(arg, 2, argv) == -1)
        return -1;
    return ftp_server_create(argv[0], argv[1], AF_INET6);
}

static int ftp_cli_close_server(char *arg)
{
    char *argv[1];
    int argc = parse_arguments(arg, 1, argv);
    if (argc == -1)
        return -1;
    if (argc != 1) {
        fprintf(stderr, "remove-server <fd>\n");
        return -1;
    }
    int fd = atoi(argv[0]);
    if (fd > 0 && (size_t) fd < global.fd_vector_capacity && global.fd_vector[fd].fd_type == FD_FTP_SERVER)
        return ftp_server_close(global.fd_vector[fd].pointer);
    else
        fprintf(stderr, "Cannot find the server.\n");
    return -1;
}

static int ftp_cli_close_client(char *arg)
{
    char *argv[1];
    int argc = parse_arguments(arg, 1, argv);
    if (argc == -1)
        return -1;
    if (argc != 1) {
        fprintf(stderr, "remove-client <fd>\n");
        return -1;
    }
    int fd = atoi(argv[0]);
    if (fd > 0 && (size_t) fd < global.fd_vector_capacity && global.fd_vector[fd].fd_type == FD_FTP_CLIENT)
        return ftp_client_close(global.fd_vector[fd].pointer);
    else
        fprintf(stderr, "Cannot find the client.\n");
    return -1;
}

static int ftp_cli_add_user(char *arg)
{
    char *argv[3] = {
        NULL,
        NULL,
        NULL
    };
    int argc = parse_arguments(arg, 3, argv);
    if (argc == -1)
        return -1;
    if (argc < 1) {
        fprintf(stderr, "add-user <username> [<root> [<hash>]]\n");
        return -1;
    }
    char path[PATH_MAX];
    getcwd(path, PATH_MAX);
    if (argv[1]) {
        char result[PATH_MAX];
        path_resolve(result, path, argv[1], NULL);
        return ftp_users_add(argv[0], argv[2], path, 1);
    }
    return ftp_users_add(argv[0], argv[2], path, 1);
}

static int ftp_cli_hash_password(char *arg)
{
    char *argv[1];
    int argc = parse_arguments(arg, 1, argv);
    if (argc == -1)
        return -1;
    if (argc < 1) {
        fprintf(stderr, "hash-password <password>\n");
        return -1;
    }
    char salt[BCRYPT_HASHSIZE], password[BCRYPT_HASHSIZE];
    if (bcrypt_gensalt(12, salt) != 0) {
        if (global.loglevel >= LOGLEVEL_ERROR)
            fprintf(stderr, "E: bcrypt_gensalt\n");
        return -1;
    }
    if(bcrypt_hashpw(argv[0], salt, password) != 0) {
        if (global.loglevel >= LOGLEVEL_ERROR)
            fprintf(stderr, "E: bcrypt_hashpw\n");
        return -1;
    }
    printf("%s\n", password);
    return 0;
}

static int ftp_cli_remove_user(char *arg)
{
    char *argv[1];
    int argc = parse_arguments(arg, 1, argv);
    if (argc == -1)
        return -1;
    if (argc < 1) {
        fprintf(stderr, "remove-user <username>\n");
        return -1;
    }
    return ftp_users_remove(argv[0]);
}

ftp_cli_command_t commands[] = {
        {"exit",               (ftp_cli_command_callback_t) ftp_cli_exit,         "exit the program"},
        {"help",               ftp_cli_help,                                      "display this text"},
        {"list-client",        (ftp_cli_command_callback_t) ftp_client_list,      "list clients"},
        {"list-fd",            (ftp_cli_command_callback_t) global_list_fd,       "list file descriptors"},
        {"add-server",         ftp_cli_add_server,                                "add ftp server"},
        {"hash-password",      ftp_cli_hash_password,                             "use bcrypt to hash password"},
        {"add-server4",        ftp_cli_add_server4,                               "add IPv4 ftp server"},
        {"add-server6",        ftp_cli_add_server6,                               "add IPv6 ftp server"},
        {"add-user",           ftp_cli_add_user,                                  "add user"},
        {"list-server",        (ftp_cli_command_callback_t) ftp_server_list,      "list servers"},
        {"list-user",          (ftp_cli_command_callback_t) ftp_users_list,       "list users"},
        {"run",                (ftp_cli_command_callback_t) ftp_cli_run,          "exit cli without closing the server"},
        {"remove-all-clients", (ftp_cli_command_callback_t) ftp_client_close_all, "remove all the ftp clients"},
        {"remove-all-servers", (ftp_cli_command_callback_t) ftp_server_close_all, "remove all the ftp servers"},
        {"remove-client",      ftp_cli_close_client,                              "remove ftp client"},
        {"remove-server",      ftp_cli_close_server,                              "remove ftp server"},
        {"remove-user",        ftp_cli_remove_user,                               "remove user"},
        {NULL, NULL, NULL}
};

int parse_arguments(char *arg, int argc, char *argv[])
{
    int i = 0;
    for (i = 0; i < argc && *arg; ++i) {
        while (*arg && isspace(*arg))
            ++arg;
        if (!*arg)
            break;
        if (*arg == '\"' || *arg == '\'') {
            char quote = *arg;
            char *to = argv[i] = ++arg;
            while (*arg && *arg != quote) {
                if (*arg == '\\' && !*++arg)
                    goto error;
                if (to != arg)
                    *to = *arg;
                ++to;
                ++arg;
            }
            if (!*arg)
                goto error;
            *to = '\0';
            if (*++arg && !isspace(*arg))
                goto error;
        } else {
            argv[i] = arg;
            while (*arg && !isspace(*arg))
                ++arg;
            if (*arg)
                *arg++ = '\0';
        }
    }
    return i;
error:
    fprintf(stderr, "Cannot parse arguments.\n");
    return -1;
}

static int ftp_cli_exit()
{
    return ftp_cli_stop();
}

static int ftp_cli_run()
{
    global.cli.done = 1;
    return ftp_cli_stop();
}

static int ftp_cli_help(char *arg)
{
    int i, printed = 0;
    char *argv[1];
    int argc = parse_arguments(arg, 1, argv);
    if (argc == -1)
        return -1;
    for (i = 0; commands[i].name; i++) {
        if (argc == 0 || strcmp(argv[0], commands[i].name) == 0) {
            printf("%-20s%s\n", commands[i].name, commands[i].doc);
            ++printed;
        }
    }
    if (!printed) {
        printf("No commands match `%s' Possibilities are:\n", argv[0]);
        for (i = 0; commands[i].name; ++i) {
            if (printed == 6) {
                printed = 0;
                printf("\n");
            }
            printf("%s\t", commands[i].name);
            ++printed;
        }
        if (printed)
            printf("\n");
    }
    return 0;
}


static int ftp_cli_epoll_callback(uint32_t events, void *arg)
{
    (void) arg;
    if (global.cli.fd != -1 && events & EPOLLIN)
        rl_callback_read_char();
    if (events & EPOLLHUP || events & EPOLLERR)
        ftp_cli_stop();
    return 0;
}

static char *ftp_cli_command_generator(const char *text, int state)
{
    static int list_index;
    static size_t len;
    char *name;
    if (!state) {
        list_index = 0;
        len = strlen(text);
    }
    while ((name = commands[list_index].name)) {
        list_index++;
        if (strncmp(name, text, len) == 0) {
            char *temp = malloc(strlen(name) + 1);
            if (temp) {
                strcpy(temp, name);
                return temp;
            } else if (global.loglevel >= LOGLEVEL_ERROR)
                perror("E: malloc(command)");
        }
    }
    return NULL;
}

static char **ftp_cli_completion(const char *text, int start, int end)
{
    (void) end;
    char **matches = NULL;
    if (start == 0)
        matches = rl_completion_matches(text, ftp_cli_command_generator);
    return matches;
}

static char *strip(char *string)
{
    char *s, *t;
    for (s = string; isspace(*s); ++s);
    if (*s == 0)
        return (s);
    t = s + strlen (s) - 1;
    for (;t > s && isspace(*t); --t);
    *++t = '\0';
    return s;
}


static ftp_cli_command_t *ftp_cli_find_command(char *name)
{
    for (int i = 0; commands[i].name; ++i)
        if (strcmp(name, commands[i].name) == 0)
            return commands + i;
    return NULL;
}

static int ftp_cli_execute_line(char *line)
{
    int i;
    for(i = 0; line[i] && isspace(line[i]); ++i);
    char *word = line + i;
    for (; line[i] && !isspace(line[i]); ++i);
    if (line[i])
        line[i++] = '\0';
    ftp_cli_command_t *command = ftp_cli_find_command(word);
    if (!command) {
        fprintf(stderr, "%s: No such command\n", word);
        return -1;
    }
    for (; isspace(line[i]); ++i);
    return (*(command->func))(line + i);
}

void ftp_cli_rl_callback(char *line)
{
    if (line) {
        if (*line) {
            char *temp = strip(line);
            if (*temp && ftp_cli_execute_line(temp) == 0)
                add_history(temp);
        }
    } else
        ftp_cli_stop();
}

void ftp_cli_init()
{
    global.cli.fd = -1;
    global.cli.done = 0;
    global.cli.epoll_item.callback = ftp_cli_epoll_callback;
    global.cli.epoll_item.arg = NULL;
}

int ftp_cli_start(int prompt)
{
    int ret = -1;
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.ptr = &global.cli.epoll_item;
    if (epoll_ctl(global.epfd, EPOLL_CTL_ADD, STDIN_FILENO, &event) == -1) {
        if (global.loglevel >= LOGLEVEL_ERROR)
            perror("E: epoll_ctl(add: cli)");
    } else {
        ++global.epoll_size;
        global.cli.fd = STDIN_FILENO;
        if (prompt)
            printf(banner, version_str);
        rl_readline_name = "minimum-ftpd";
        rl_attempted_completion_function = ftp_cli_completion;
        rl_callback_handler_install(prompt ? "> " : "", ftp_cli_rl_callback);
        ret = 0;
    }
    return ret;
}

int ftp_cli_stop()
{
    int ret = 0;
    if (global.cli.fd != -1) {
        if (!global.cli.done){
            if (ftp_server_close_all() == -1)
                ret = -1;
        }
        --global.epoll_size;
        if (epoll_ctl(global.epfd, EPOLL_CTL_DEL, STDIN_FILENO, NULL) == -1) {
            if (global.loglevel >= LOGLEVEL_ERROR)
                perror("E: epoll_ctl(del: cli)");
            ret = -1;
        }
        rl_callback_handler_remove();
        global.cli.fd = -1;
    }
    return ret;
}