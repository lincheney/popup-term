#include <gtk/gtk.h>

#include "config.h"
#include "window.h"
#include "socket.h"

char** commands = NULL;

static GOptionEntry entries[] = {
    {"id",      'i', 0, G_OPTION_ARG_STRING,       &app_id,          "Application ID", "ID"},
    {"config",  'c', 0, G_OPTION_ARG_FILENAME,     &config_filename, "Config file",    "FILE"},
    {"command", 'C', 0, G_OPTION_ARG_STRING_ARRAY, &commands,        "Run commands",   NULL},
    {NULL}
};

int run_server(int argc, char** argv) {
    if (! config_filename) {
        config_filename = g_build_filename(g_get_user_config_dir(), "vte_terminal", "config.ini", NULL);
    }
    load_config(config_filename);

    GtkCssProvider* css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css_provider, GLOBAL_CSS, -1, NULL);
    GdkScreen* screen = gdk_screen_get_default();
    gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);

    make_new_window_full(NULL, NULL, argc, argv);
    gtk_main();
    return 0;
}

int client_send_line(GSocket* sock, char* line, Buffer* buffer) {
    GError* error = NULL;
    line = g_strescape(line, "\"");
    int len = strlen(line) + 1;
    int result = sock_send_all(sock, line, len);
    free(line);
    if (! result) {
        return 1;
    }

    while (1) {
        len = g_socket_receive(sock, buffer->data + buffer->used, buffer->reserved - buffer->used, NULL, &error);
        if (len < 0) {
            g_warning("Failed to recv(): %s\n", error->message);
            g_error_free(error);
            return 1;
        }

        if (len == 0) {
            // unexpected eof
            g_warning("Unexpected EOF");
            return 1;
        }

        char* end = memchr(buffer->data + buffer->used, 0, len);
        if (end) {
            // end of payload
            buffer->used += len;
            buffer_shift_back(buffer, end - buffer->data + 1);
            break;
        }

        buffer->used = 0;
    }
    return 0;
}

int run_client(GSocket* sock, int argc, char** argv) {
    Buffer* buffer = buffer_new(1024);

    if (commands) {
        for (char** line = commands; *line; line++) {
            client_send_line(sock, *line, buffer);
        }
    }

    if (! commands || argc > 0) {
        char* quoted_argv[argc+3];
        quoted_argv[argc+2] = NULL;

        // command
        quoted_argv[0] = default_open_action;

        // cwd
        char* cwd = g_get_current_dir();
        char* quoted_cwd = g_shell_quote(cwd);
        free(cwd);
        quoted_argv[1] = alloca(sizeof(char) * (strlen(quoted_cwd) + 5));
        strcpy(quoted_argv[1], "cwd=");
        strcpy(quoted_argv[1]+sizeof("cwd=")-1, quoted_cwd);
        free(quoted_cwd);

        // argv
        for (int i = 0; i < argc; i++) {
            quoted_argv[i+2] = g_shell_quote(argv[i]);
        }

        char* line = g_strjoinv(" ", quoted_argv);
        for (int i = 0; i < argc; i++) {
            free(quoted_argv[i+2]); // first 2 strings are static
        }
        *strchr(line, ' ') = ':';
        client_send_line(sock, line, buffer);
        free(line);
    }

    buffer_free(buffer);
    return 0;
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
        snprintf(buffer, sizeof(buffer), APP_PREFIX ".x%s", display+1);
        app_id = strndup(buffer, sizeof(buffer));
    } else if (strcmp(app_id, "") == 0) {
        free(app_id);
        app_id = NULL;
    }

    if (app_id) {
        g_setenv(APP_PREFIX "_ID", app_id, TRUE);
    }

    return app_id;
}

int main(int argc, char *argv[]) {
    int status = 0;
    GError* error = NULL;
    gtk_init(&argc, &argv);

    // cli option parsing
    GOptionContext* context = g_option_context_new(NULL);
    g_option_context_add_main_entries(context, entries, NULL);
    if (! g_option_context_parse(context, &argc, &argv, &error)) {
        g_print("%s\n", error->message);
        g_error_free(error);
        return 1;
    }
    app_id = make_app_id();

    // remove arg0
    argc --;
    argv ++;
    // remove --
    if (argc > 0 && strcmp(argv[0], "--") == 0) {
        argc --;
        argv ++;
    }

    if (! app_id) {
        run_server(argc, argv);
        return 0;
    }

    GSocket* sock;
    GSocketAddress* addr;
    if (! make_sock(app_id, &sock, &addr)) {
        return 1;
    }

    status = commands ? 0 : try_bind_sock(sock, addr);
    if (status > 0) {
        status = run_server(argc, argv);
    } else if (status < 0) {
        return 1;
    } else if (connect_sock(sock, addr) >= 0) {
        status = run_client(sock, argc, argv);
    }
    close_socket(sock);

    return status;
}
