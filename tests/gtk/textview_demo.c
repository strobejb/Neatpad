//
//	A NeatpadTextView in a window, to try it:
//
//		textview-demo [--line-numbers] [--select-all] [--screenshot FILE.png] [file]
//
//	--screenshot saves what the view draws, once it has drawn it, and quits.
//
#include <gtk/gtk.h>
#include <string.h>

#include "NeatpadTextView.h"

static const char *g_file       = 0;
static const char *g_screenshot = 0;
static gboolean    g_lines      = FALSE;
static gboolean    g_select     = FALSE;

// The caret's place, in the window's title
static void on_cursor_changed(NeatpadTextView *view, gpointer data)
{
    guint64  line;
    guint64  column;
    gboolean known;
    char     title[128];

    neatpad_text_view_get_cursor(view, &line, &column, &known);
    g_snprintf(title, sizeof(title), "Line %" G_GUINT64_FORMAT "%s, column %" G_GUINT64_FORMAT, line + 1,
               known ? "" : "?", column + 1);
    gtk_window_set_title(GTK_WINDOW(data), title);
}

// Save the view as it's drawn, then close its window
static gboolean save_screenshot(gpointer data)
{
    GtkWidget     *view      = GTK_WIDGET(data);
    int            width     = gtk_widget_get_width(view);
    int            height    = gtk_widget_get_height(view);
    GdkPaintable  *paintable = gtk_widget_paintable_new(view);
    GtkSnapshot   *snapshot  = gtk_snapshot_new();
    GskRenderNode *node;

    gdk_paintable_snapshot(paintable, GDK_SNAPSHOT(snapshot), width, height);
    node = gtk_snapshot_free_to_node(snapshot);

    if (node)
    {
        GskRenderer *renderer = gtk_native_get_renderer(gtk_widget_get_native(view));
        GdkTexture  *texture  = gsk_renderer_render_texture(renderer, node, &GRAPHENE_RECT_INIT(0, 0, width, height));

        if (!gdk_texture_save_to_png(texture, g_screenshot))
            g_printerr("can't save %s\n", g_screenshot);

        g_object_unref(texture);
        gsk_render_node_unref(node);
    }

    g_object_unref(paintable);
    gtk_window_destroy(GTK_WINDOW(gtk_widget_get_root(view)));
    return G_SOURCE_REMOVE;
}

static void on_activate(GtkApplication *app, gpointer data)
{
    GtkWidget *window = gtk_application_window_new(app);
    GtkWidget *view   = neatpad_text_view_new();

    (void)data;

    gtk_window_set_default_size(GTK_WINDOW(window), 800, 500);
    gtk_window_set_child(GTK_WINDOW(window), view);

    if (g_lines)
        neatpad_text_view_set_style(NEATPAD_TEXT_VIEW(view), TXS_LINENUMBERS | TXS_SELMARGIN,
                                    TXS_LINENUMBERS | TXS_SELMARGIN);

    if (g_file && !neatpad_text_view_open_file(NEATPAD_TEXT_VIEW(view), g_file))
        g_printerr("can't open %s\n", g_file);

    if (g_select)
        neatpad_text_view_select_all(NEATPAD_TEXT_VIEW(view));

    g_signal_connect(view, "cursor-changed", G_CALLBACK(on_cursor_changed), window);

    gtk_window_present(GTK_WINDOW(window));
    gtk_widget_grab_focus(view);

    if (g_screenshot)
        g_timeout_add(500, save_screenshot, view);
}

int main(int argc, char *argv[])
{
    GtkApplication *app;
    int             status;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--line-numbers") == 0)
            g_lines = TRUE;
        else if (strcmp(argv[i], "--select-all") == 0)
            g_select = TRUE;
        else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
            g_screenshot = argv[++i];
        else
            g_file = argv[i];
    }

    app = gtk_application_new("net.catch22.neatpad.TextViewDemo", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), 0);

    status = g_application_run(G_APPLICATION(app), 0, 0);
    g_object_unref(app);
    return status;
}
