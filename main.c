#include <gtk/gtk.h>

#include "config.h"
#include "window.h"
#include "socket.h"
#include "utils.h"
#include "server.h"
#include "client.h"

char* commands[255];
char* sock_connect = NULL;
gboolean no_connect_stdin = FALSE;
gboolean no_connect_stdout = FALSE;

void print_help(int argc, char** argv) {
    fprintf(stderr,
            "usage: %s [OPTIONS] [ARGS...]\n" \
            "       %s -h|--help\n" \
            "\n" \
            "Options:\n" \
            "  -i ID, --id ID\n" \
            "  -C CONFIG, --config CONFIG\n" \
            "  -c COMMAND, --command COMMAND\n" \
            "  --connect COMMAND\n" \
            "  --no-connect-stdin\n" \
            "  --no-connect-stdout\n" \
        , argv[0], argv[0]);
}

char** parse_args(int* argc, char** argv) {

#define MATCH_FLAG_WITH_ARG(flag, dest) \
        if (STR_STARTSWITH(argv[i], (flag))) { \
            if (argv[i][sizeof(flag)-1] == '=') { \
                /* -f=value */ \
                dest = argv[i] + sizeof(flag); \
                continue; \
            } else if (argv[i][1] != '-' && argv[i][sizeof(flag)-1]) { \
                /* -fvalue */ \
                dest = argv[i] + sizeof(flag) - 1; \
                continue; \
            } else if (argv[i][sizeof(flag)-1]) { \
                /* unknown flag */ \
            } else if (i + 1 < *argc) { \
                /* -f value */ \
                i ++; \
                dest = argv[i]; \
                continue; \
            } else { \
                print_help(*argc, argv); \
                fprintf(stderr, "%s: expected one argument\n", flag); \
                exit(1); \
            } \
        }

#define MATCH_FLAG(flag, dest) \
    if (STR_EQUAL(argv[i], (flag))) { \
        dest = TRUE; \
        continue; \
    }

    int i, command_ix = -1;
    // skip arg0
    for (i = 1; i < *argc; i ++) {
        if (STR_EQUAL(argv[i], "-h") || STR_EQUAL(argv[i], "--help")) {
            print_help(*argc, argv);
            exit(0);
        }

        MATCH_FLAG_WITH_ARG("-C", config_filename);
        MATCH_FLAG_WITH_ARG("--config", config_filename);
        MATCH_FLAG_WITH_ARG("-i", app_id);
        MATCH_FLAG_WITH_ARG("--id", app_id);
        MATCH_FLAG_WITH_ARG("-c", command_ix ++; commands[command_ix]);
        MATCH_FLAG_WITH_ARG("--command", command_ix ++; commands[command_ix]);
        MATCH_FLAG_WITH_ARG("--connect", sock_connect);
        MATCH_FLAG("--no-connect-stdin", no_connect_stdin);
        MATCH_FLAG("--no-connect-stdout", no_connect_stdout);
        if (STR_EQUAL(argv[i], "--")) {
            i ++;
        }
        break;
    }

    command_ix ++;
    commands[command_ix] = 0;
    *argc -= i;
    if (! argc) return NULL;
    return argv+i;
}

char* make_app_id() {
    char buffer[256];
    if (! app_id) {
        const char* id = g_getenv(APP_PREFIX "_ID");
        if (id) {
            app_id = strdup(id);
        }
    }

    if (! app_id) {
        const char* display = gdk_display_get_name(gdk_display_get_default());
        snprintf(buffer, sizeof(buffer), APP_PREFIX "." GIT_REF ".%s", display);
        app_id = strndup(buffer, sizeof(buffer));
    } else if (STR_EQUAL(app_id, "")) {
        app_id = NULL;
    }

    if (app_id) {
        g_setenv(APP_PREFIX "_ID", app_id, TRUE);
    } else {
        g_unsetenv(APP_PREFIX "_ID");
    }

    return app_id;
}

char* find_app_path(char* arg0) {
    if (! strchr(arg0, '/') || g_path_is_absolute(arg0)) {
        return arg0;
    }

    char* cwd = g_get_current_dir();
    char* path = g_build_filename(cwd, arg0, NULL);
    return path;
}

int main(int argc, char *argv[]) {
    int status = 0;
    gtk_init(&argc, &argv);
    app_path = find_app_path(argv[0]);
    argv = parse_args(&argc, argv);

    app_id = make_app_id();
    if (! app_id) {
        run_server(argc, argv);
        return 0;
    }

    GSocket* sock;
    GSocketAddress* addr;
    if (! make_sock(app_id, &sock, &addr)) {
        return 1;
    }

    status = (commands[0] || sock_connect) ? 0 : try_bind_sock(sock, addr, (GSourceFunc)server_recv);
    if (status > 0) {
        status = run_server(argc, argv);
    } else if (status < 0) {
        return 1;
    } else if (connect_sock(sock, addr) >= 0) {
        status = run_client(sock, commands, argc, argv, sock_connect, !no_connect_stdin, !no_connect_stdout);
    }
    close_socket(sock);

    return status;
}
