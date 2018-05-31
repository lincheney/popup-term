#include <gtk/gtk.h>
#include <vte/vte.h>
#include "window.h"
#include "config.h"

const gint ERROR_EXIT_CODE = 127;
GtkWidget* make_window();

void term_exited(VteTerminal* terminal, gint status, GtkWidget* container) {
    gtk_widget_destroy(GTK_WIDGET(container));
}

void term_spawn_callback(GtkWidget* terminal, GPid pid, GError *error, gpointer user_data) {
    if (error) {
        fprintf(stderr, "Could not start terminal: %s\n", error->message);
        exit(ERROR_EXIT_CODE);
    }
}

VteTerminal* get_nth_terminal(GtkWidget* window, int index) {
    GtkWidget* notebook = g_object_get_data(G_OBJECT(window), "notebook");
    GtkWidget* grid = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), index);
    GtkWidget* terminal = g_object_get_data(G_OBJECT(grid), "terminal");
    return VTE_TERMINAL(terminal);
}

VteTerminal* get_active_terminal(GtkWidget* window) {
    if (! window) window = GTK_WIDGET(get_active_window());
    GtkWidget* notebook = g_object_get_data(G_OBJECT(window), "notebook");
    int index = gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook));
    return get_nth_terminal(window, index);
}

GtkWidget* make_terminal(GtkWidget* grid, int argc, char** argv) {
    GtkWidget *terminal;
    GtkWidget *scrollbar;

    terminal = vte_terminal_new();
    g_object_set_data(G_OBJECT(grid), "terminal", terminal);
    gtk_container_add(GTK_CONTAINER(grid), GTK_WIDGET(terminal));
    configure_terminal(terminal);

    g_signal_connect(terminal, "child-exited", G_CALLBACK(term_exited), grid);
    g_object_set(terminal, "expand", 1, NULL);
    vte_terminal_set_cursor_blink_mode(VTE_TERMINAL(terminal), VTE_CURSOR_BLINK_OFF);

    // populate palette
    vte_terminal_set_colors(VTE_TERMINAL(terminal), palette+1, palette, palette+2, PALETTE_SIZE);

    char **args;
    char *fallback_args[] = {NULL, NULL};
    char *user_shell = NULL;

    if (argc > 1) {
        args = argv + 1;
    } else if (default_args) {
        args = default_args;
    } else {
        user_shell = vte_get_user_shell();
        fallback_args[0] = user_shell ? user_shell : DEFAULT_SHELL;
        args = fallback_args;
    }

    vte_terminal_spawn_async(
            VTE_TERMINAL(terminal),
            VTE_PTY_DEFAULT, //pty flags
            NULL, // pwd
            args, // args
            NULL, // env
            G_SPAWN_SEARCH_PATH, // g spawn flags
            NULL, // child setup
            NULL, // child setup data
            NULL, // child setup data destroy
            -1, // timeout
            NULL, // cancellable
            (VteTerminalSpawnAsyncCallback) term_spawn_callback, // callback
            NULL // user data
    );
    free(user_shell);

    if (show_scrollbar) {
        scrollbar = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(terminal)));
        gtk_container_add(GTK_CONTAINER(grid), GTK_WIDGET(scrollbar));
    }

    return terminal;
}

gboolean key_pressed(GtkWidget* window, GdkEventKey* event, gpointer data) {
    guint modifiers = event->state & gtk_accelerator_get_default_mod_mask();
    gboolean handled = FALSE;
    for (int i = 0; i < keyboard_shortcuts->len; i++) {
        KeyCombo* combo = &g_array_index(keyboard_shortcuts, KeyCombo, i);
        if (combo->key == event->keyval && combo->modifiers == modifiers) {
            VteTerminal* terminal = get_active_terminal(window);
            combo->callback(terminal, combo->data);
            handled = TRUE;
        }
    }
    return handled;
}

void notebook_tab_removed(GtkWidget* notebook, GtkWidget *child, guint page_num) {
    if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook)) == 0) {
        gtk_widget_destroy(gtk_widget_get_toplevel(notebook));
    }
}

GtkNotebook* notebook_create_window(GtkNotebook* notebook, GtkWidget* page, gint x, gint y) {
    GtkWidget* window = make_window();
    gtk_widget_show_all(window);
    return GTK_NOTEBOOK(g_object_get_data(G_OBJECT(window), "notebook"));
}

void notebook_switch_page(GtkNotebook* notebook, GtkWidget* page, guint num) {
    GtkWidget* term = g_object_get_data(G_OBJECT(page), "terminal");
    gtk_widget_grab_focus(term);
}

void add_terminal(GtkWidget* window) {
    GtkWidget* notebook = g_object_get_data(G_OBJECT(window), "notebook");

    GtkWidget* grid = gtk_grid_new();
    GtkWidget* term = make_terminal(grid, 0, NULL);

    int page = gtk_notebook_append_page(GTK_NOTEBOOK(notebook), grid, NULL);
    gtk_notebook_set_tab_detachable(GTK_NOTEBOOK(notebook), grid, TRUE);
    configure_tab(GTK_CONTAINER(notebook), grid);

    gtk_widget_show_all(grid);
    gtk_widget_realize(term);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), page);
}

GtkWidget* make_window() {
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_application_add_window(app, GTK_WINDOW(window));

    GtkWidget *notebook = gtk_notebook_new();
    gtk_widget_set_can_focus(notebook, FALSE);
    gtk_notebook_set_group_name(GTK_NOTEBOOK(notebook), "terminals");
    g_object_set_data(G_OBJECT(window), "notebook", notebook);

    gtk_notebook_set_show_border(GTK_NOTEBOOK(notebook), FALSE);
    g_signal_connect(notebook, "page-removed", G_CALLBACK(notebook_tab_removed), NULL);
    g_signal_connect(notebook, "create-window", G_CALLBACK(notebook_create_window), NULL);
    g_signal_connect(notebook, "switch-page", G_CALLBACK(notebook_switch_page), NULL);

    g_signal_connect(window, "key-press-event", G_CALLBACK(key_pressed), NULL);
    gtk_container_add(GTK_CONTAINER(window), notebook);

    configure_window(GTK_WINDOW(window));
    return window;
}

GtkWidget* new_window() {
    GtkWidget* window = make_window();
    add_terminal(window);
    gtk_widget_show_all(window);
    return window;
}