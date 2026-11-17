// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

//
//	MODULE:		neatpad.c
//
//	PURPOSE:	Neatpad for GNOME: a window per document, with a NeatpadTextView,
//				open (and recent files) and save, find and replace, go to line,
//				a status bar, the light or dark style, preferences, and
//				unsaved changes kept safe (recovery.c)
//
#include <adwaita.h>

#include "NeatpadTextView.h"
#include "palette.h"
#include "preferences.h"
#include "recovery.h"

#define APP_ID      "net.catch22.Neatpad"
#define RECENT_MAX  10
#define ENTRY_WIDTH 280

// What a window holds: kept on the window as "editor"
typedef struct
{
    GtkWindow       *window;
    NeatpadTextView *view;
    AdwWindowTitle  *title;
    AdwToastOverlay *toasts;
    AdwBanner       *banner;
    int              notice; // what the banner says: NOTICE_*
    GtkWidget       *recent_popover; // the Open button's recent files
    GtkListBox      *recent_list;
    char            *recent_uri;   // the recent file being opened
    char            *recent_store; // or the unsaved document
    GtkSearchBar    *search_bar;
    GtkSearchEntry  *search_entry;
    GtkToggleButton *match_case;
    gboolean         filtering; // only the lines that match show, or every match is highlighted
    GtkWidget       *replace_row;
    GtkEntry        *replace_entry;
    GtkSearchBar    *goto_bar;
    GtkEntry        *goto_entry;
    GtkWidget       *status_bar;
    GtkLabel        *position; // the status bar's panes, as Win32 Neatpad's
    GtkLabel        *doc_stats;
    GtkLabel        *index;
    GtkLabel        *zoom_pane;
    GtkWidget       *zoom_reset; // the main menu's zoom, between - and +
    GtkLabel        *line_format;
    GtkLabel        *encoding;
    int              zoom; // percent of the font's size
    double           zoom_rest; // a fine-grained wheel's turn, short of a notch
    GFile           *file; // 0 until opened or saved
    GFileMonitor    *monitor;   // watching it for other programs' changes
    gboolean         on_disk;   // the file as last seen: there, its size and time
    goffset          size;
    gint64           modified;
    gboolean         following; // read-only, taking in what's appended
    Recovery        *recovery;
    gboolean         closing;
} Editor;

// What the banner below the header bar says, as Win32 Neatpad's notification bar
enum
{
    NOTICE_NONE,
    NOTICE_RECOVERED,          // a file's unsaved changes, after a crash: Revert to Saved
    NOTICE_RECOVERED_UNTITLED, // an unsaved document: Discard Document
    NOTICE_RESTORED,           // a file's unsaved changes, kept as a session
    NOTICE_RESTORED_UNTITLED,
    NOTICE_RECOVERY_FAILED,    // recovery data can't be written
    NOTICE_CHANGED,            // another program changed the file: Reload
    NOTICE_GREW,               // or added to it, as to a log: Follow
    NOTICE_DELETED,            // or deleted or moved it
    NOTICE_FOLLOWING           // the file is followed as it grows: Stop
};

// What to do once a document's changes are saved or discarded
typedef void (*EditorThen)(Editor *editor);

// GNOME's own settings, for its monospace font: 0 where they aren't installed
static GSettings *desktop;

// Neatpad has started: later starts are a new window
static gboolean started;

// The style setting, as libadwaita's colour scheme
static void apply_style(void)
{
    AdwColorScheme scheme = ADW_COLOR_SCHEME_DEFAULT;

    if (g_str_equal(settings.style, "light"))
        scheme = ADW_COLOR_SCHEME_FORCE_LIGHT;
    else if (g_str_equal(settings.style, "dark"))
        scheme = ADW_COLOR_SCHEME_FORCE_DARK;

    adw_style_manager_set_color_scheme(adw_style_manager_get_default(), scheme);
}

static GtkWindow *editor_window_new(GtkApplication *app, GFile *file);

static Editor *editor_of(gpointer window)
{
    return g_object_get_data(G_OBJECT(window), "editor");
}

static void editor_free(gpointer data)
{
    Editor *editor = data;

    if (editor->recovery)
        recovery_free(editor->recovery);

    if (editor->monitor)
    {
        g_signal_handlers_disconnect_by_data(editor->monitor, editor);
        g_file_monitor_cancel(editor->monitor);
        g_clear_object(&editor->monitor);
    }

    g_clear_object(&editor->file);
    g_free(editor->recent_uri);
    g_free(editor->recent_store);
    g_free(editor);
}

static void toast(Editor *editor, const char *message)
{
    adw_toast_overlay_add_toast(editor->toasts, adw_toast_new(message));
}

static void show_notice(Editor *editor, int notice)
{
    const char *title  = "";
    const char *button = 0;

    switch (notice)
    {
    case NOTICE_RECOVERED:
        title  = "Unsaved changes have been recovered";
        button = "_Revert to Saved";
        break;

    case NOTICE_RECOVERED_UNTITLED:
        title  = "An unsaved document has been recovered";
        button = "_Discard Document";
        break;

    case NOTICE_RESTORED:
        title  = "Previous session restored";
        button = "_Revert to Saved";
        break;

    case NOTICE_RESTORED_UNTITLED:
        title  = "Previous session restored";
        button = "_Discard Document";
        break;

    case NOTICE_RECOVERY_FAILED:
        title = "Unable to save recovery data. Please save your work.";
        break;

    case NOTICE_CHANGED:
        title  = "This file has been changed by another program";
        button = "_Reload";
        break;

    case NOTICE_GREW:
        title  = "Another program is adding to this file";
        button = "_Follow";
        break;

    case NOTICE_DELETED:
        title = "This file has been deleted or moved by another program";
        break;

    case NOTICE_FOLLOWING:
        title  = "Following the end of this file as it grows";
        button = "_Stop";
        break;
    }

    editor->notice = notice;
    adw_banner_set_title(editor->banner, title);
    adw_banner_set_button_label(editor->banner, button);
    adw_banner_set_revealed(editor->banner, notice != NOTICE_NONE);
}

// The document was saved, reverted or replaced: what the banner said is past
static void hide_notice(Editor *editor)
{
    if (editor->notice != NOTICE_NONE)
        show_notice(editor, NOTICE_NONE);
}

static void apply_settings(Editor *editor)
{
    // the change indicators are drawn in the selection margin; the line
    // numbers select lines without it
    guint styles = (settings.line_numbers ? TXS_LINENUMBERS : 0) |
                   (settings.change_markers ? TXS_SELMARGIN | TXS_CHANGEMARKERS : 0) |
                   (settings.current_line ? TXS_HIGHLIGHTCURLINE : 0) |
                   (settings.highlight_changes ? TXS_HIGHLIGHTCHANGES : 0) | (settings.long_lines ? TXS_LONGLINES : 0) |
                   (settings.show_line_endings ? TXS_SHOWLINEENDINGS : 0) |
                   (settings.insert_spaces ? TXS_INSERTSPACES : 0) | (settings.auto_indent ? TXS_AUTOINDENT : 0);

    char *system = settings.system_font && desktop ? g_settings_get_string(desktop, "monospace-font-name") : 0;

    PangoFontDescription *font = pango_font_description_from_string(system && *system ? system : settings.font);
    char                 *zoomed;

    // zoomed, as the window's zoom says
    if (pango_font_description_get_size(font) > 0 && editor->zoom != 100)
        pango_font_description_set_size(font, pango_font_description_get_size(font) * editor->zoom / 100);

    zoomed = pango_font_description_to_string(font);
    neatpad_text_view_set_font(editor->view, zoomed);
    gtk_widget_set_visible(editor->status_bar, settings.status_bar);

    g_free(zoomed);
    pango_font_description_free(font);
    g_free(system);
    neatpad_text_view_set_tab_width(editor->view, (guint)settings.tab_width);
    neatpad_text_view_set_long_line(editor->view, (guint)settings.long_line_column);
    neatpad_text_view_set_filter_context(editor->view, (guint)settings.filter_context);
    neatpad_text_view_set_line_spacing(editor->view, (guint)settings.line_spacing / 2,
                                       (guint)(settings.line_spacing + 1) / 2);
    neatpad_text_view_set_style(editor->view,
                                TXS_LINENUMBERS | TXS_SELMARGIN | TXS_HIGHLIGHTCURLINE | TXS_HIGHLIGHTCHANGES |
                                    TXS_CHANGEMARKERS | TXS_LONGLINES | TXS_SHOWLINEENDINGS | TXS_INSERTSPACES |
                                    TXS_AUTOINDENT,
                                styles);
}

static void apply_scheme(Editor *editor);

// The settings, and the palette, in every window: after one has changed
static void apply_settings_everywhere(void)
{
    GList *windows = gtk_application_get_windows(GTK_APPLICATION(g_application_get_default()));

    for (GList *w = windows; w; w = w->next)
    {
        if (editor_of(w->data))
        {
            apply_settings(editor_of(w->data));
            apply_scheme(editor_of(w->data));
        }
    }
}

// The text's colours are the palette's, which follow the style: light or dark,
// and the accent colour where libadwaita has one (1.6)
static void apply_scheme(Editor *editor)
{
    AdwStyleManager *manager = adw_style_manager_get_default();
    GdkRGBA         *accent  = 0;

#if ADW_CHECK_VERSION(1, 6, 0)
    accent = adw_style_manager_get_accent_color_rgba(manager);
#endif

    palette_apply(settings.palette, editor->view, adw_style_manager_get_dark(manager), accent);

    if (accent)
        gdk_rgba_free(accent);
}

static void on_style_changed(GObject *manager, GParamSpec *spec, gpointer window)
{
    (void)manager;
    (void)spec;
    apply_scheme(editor_of(window));
}

//
//	The title: the file's name, marked when it has unsaved changes, and its
//	folder below
//
// Where a file is, as Text Editor shows it: home as ~
static char *display_path(GFile *file)
{
    char       *path = g_file_get_path(file);
    const char *home = g_get_home_dir();
    size_t      size = strlen(home);
    char       *shown;

    if (!path)
        shown = g_file_get_parse_name(file);
    else if (g_str_has_prefix(path, home) && (path[size] == '/' || path[size] == 0) && size > 1)
        shown = g_strconcat("~", path + size, NULL);
    else
        shown = g_strdup(path);

    g_free(path);
    return shown;
}

// A file's folder, as display_path shows it
static char *display_folder(GFile *file)
{
    GFile *parent = g_file_get_parent(file);
    char  *folder = parent ? display_path(parent) : g_strdup("");

    g_clear_object(&parent);
    return folder;
}

static void update_title(Editor *editor)
{
    gboolean modified = neatpad_text_view_get_modified(editor->view);
    char    *name     = editor->file ? g_file_get_basename(editor->file) : g_strdup("Untitled");
    char    *title    = g_strdup_printf("%s%s", modified ? "\xe2\x80\xa2 " : "", name);
    char    *folder   = editor->file ? display_folder(editor->file) : g_strdup("");

    adw_window_title_set_title(editor->title, title);
    adw_window_title_set_subtitle(editor->title, folder);
    gtk_window_set_title(editor->window, title);

    g_free(folder);
    g_free(title);
    g_free(name);
}

//
//	The status bar, as Win32 Neatpad's: the caret's line and column, counted
//	from 1; the document's size, or the selection's; indexing; the zoom; the
//	line endings; and the encoding
//
static const char *line_format_name(guint format)
{
    switch (format)
    {
    case TXL_LF:
        return "UNIX (LF)";

    case TXL_CR:
        return "Mac (CR)";

    case TXL_CRLF:
        return "Windows (CRLF)";

    case TXL_ALL:
        return "ALL";

    default:
        return "?";
    }
}

// 1,234 KB to a decimal place, or bytes, as Win32's FormatByteSize
static void format_byte_size(char *text, size_t length, guint64 bytes)
{
    static const struct
    {
        guint64     unit;
        const char *suffix;
    } units[] = {{1024ULL * 1024 * 1024, "GB"}, {1024 * 1024, "MB"}, {1024, "KB"}};

    for (size_t i = 0; i < G_N_ELEMENTS(units); i++)
    {
        guint64 whole = bytes / units[i].unit;
        guint64 tenth = ((bytes % units[i].unit) * 10 + units[i].unit / 2) / units[i].unit;

        if (whole == 0)
            continue;

        if (tenth == 10)
        {
            whole++;
            tenth = 0;
        }

        g_snprintf(text, length, "%" G_GUINT64_FORMAT ".%" G_GUINT64_FORMAT " %s", whole, tenth, units[i].suffix);
        return;
    }

    g_snprintf(text, length, "%" G_GUINT64_FORMAT " bytes", bytes);
}

// A count with the locale's thousands separator: 1,234,567
static void format_count(char *text, size_t length, guint64 count)
{
    snprintf(text, length, "%'" G_GUINT64_FORMAT, count);
}

static void update_doc_stats(Editor *editor)
{
    NeatpadDocStats stats;
    guint           format = neatpad_text_view_get_line_format(editor->view);
    char            text[128];
    char            size[32];
    char            count[32];
    char            selected[32];

    if (!neatpad_text_view_get_doc_stats(editor->view, &stats))
    {
        gtk_label_set_text(editor->doc_stats, "");
        gtk_label_set_text(editor->line_format, line_format_name(format));
        return;
    }

    // edits and further line counting can turn up line endings of another kind
    g_snprintf(text, sizeof(text), "%s%s", line_format_name(format),
               (stats.line_endings_seen & ~format) ? ", mixed" : "");
    gtk_label_set_text(editor->line_format, text);

    // with a selection, how much of the document it covers; characters once
    // the file's counted, bytes until then
    if (stats.selected_bytes != 0 && stats.chars_known)
    {
        format_count(selected, sizeof(selected), stats.selected_chars);
        format_count(count, sizeof(count), stats.chars);
        g_snprintf(text, sizeof(text), "Selected %s of %s %s", selected, count,
                   stats.chars == 1 ? "character" : "characters");
    }
    else if (stats.selected_bytes != 0)
    {
        format_count(selected, sizeof(selected), stats.selected_bytes);
        g_snprintf(text, sizeof(text), "Selected %s %s", selected, stats.selected_bytes == 1 ? "byte" : "bytes");
    }
    else if (stats.chars_known)
    {
        format_byte_size(size, sizeof(size), stats.bytes);
        format_count(count, sizeof(count), stats.chars);
        g_snprintf(text, sizeof(text), "%s (%s %s)", size, count, stats.chars == 1 ? "character" : "characters");
    }
    else
    {
        format_byte_size(text, sizeof(text), stats.bytes);
    }

    gtk_label_set_text(editor->doc_stats, text);
}

// A link to start indexing, its progress as a link to stop it, or nothing
// once the whole file is indexed
static void update_index(Editor *editor)
{
    guint    percent;
    gboolean indexing;
    char     markup[64] = "";

    neatpad_text_view_get_index_info(editor->view, &percent, &indexing);

    if (indexing)
        g_snprintf(markup, sizeof(markup), "<a href=\"index\">Indexing %u%%</a>", percent);
    else if (percent < 100)
        g_strlcpy(markup, "<a href=\"index\">Index file</a>", sizeof(markup));

    gtk_label_set_markup(editor->index, markup);
    gtk_widget_set_visible(GTK_WIDGET(editor->index), *markup != 0);
}

static void update_status(Editor *editor)
{
    guint64  line;
    guint64  column;
    gboolean known;
    char     text[96];

    neatpad_text_view_get_cursor(editor->view, &line, &column, &known);

    if (known)
        g_snprintf(text, sizeof(text), "Ln %" G_GUINT64_FORMAT ", Col %" G_GUINT64_FORMAT, line + 1, column + 1);
    else
        g_snprintf(text, sizeof(text), "Ln ---, Col %" G_GUINT64_FORMAT, column + 1);

    gtk_label_set_text(editor->position, text);
    update_doc_stats(editor);
    update_index(editor);

    g_snprintf(text, sizeof(text), "%d%%", editor->zoom);
    gtk_label_set_text(editor->zoom_pane, text);
    gtk_widget_set_visible(GTK_WIDGET(editor->zoom_pane), editor->zoom != 100);
    gtk_button_set_label(GTK_BUTTON(editor->zoom_reset), text);
    gtk_label_set_text(editor->encoding, neatpad_text_view_get_encoding(editor->view));
}

static gboolean on_index_link(GtkLabel *label, const char *uri, gpointer data)
{
    Editor  *editor = data;
    gboolean indexing;

    (void)label;
    (void)uri;

    neatpad_text_view_get_index_info(editor->view, 0, &indexing);
    neatpad_text_view_index_file(editor->view, !indexing);
    update_index(editor);
    return TRUE;
}

static void on_changed(NeatpadTextView *view, gpointer data)
{
    Editor *editor = data;

    (void)view;
    update_title(editor);
    update_status(editor);
    recovery_changed(editor->recovery);
}

// What recovery asks of the window
static char *recovery_filename(gpointer data)
{
    Editor *editor = data;

    return editor->file ? g_file_get_path(editor->file) : 0;
}

static void recovery_failed(gpointer data)
{
    show_notice(data, NOTICE_RECOVERY_FAILED);
}

static void on_cursor_changed(NeatpadTextView *view, gpointer data)
{
    (void)view;
    update_status(data);
}

//
//	Recent files: GNOME's list, those Neatpad opened or saved, newest first
//
static void remember(GFile *file)
{
    char *uri = g_file_get_uri(file);

    gtk_recent_manager_add_item(gtk_recent_manager_get_default(), uri);
    g_free(uri);
}

static int newest_first(gconstpointer a, gconstpointer b)
{
    return g_date_time_compare(gtk_recent_info_get_modified((GtkRecentInfo *)b),
                               gtk_recent_info_get_modified((GtkRecentInfo *)a));
}

// Where a recent file is: its folder, home as ~
static char *recent_folder(GtkRecentInfo *info)
{
    GFile *file   = g_file_new_for_uri(gtk_recent_info_get_uri(info));
    char  *folder = display_folder(file);

    g_object_unref(file);
    return folder;
}

// The files Neatpad opened or saved that are still there, newest first:
// g_list_free_full them with gtk_recent_info_unref
static GList *recent_files(void)
{
    GList *items = gtk_recent_manager_get_items(gtk_recent_manager_get_default());
    GList *files = 0;

    for (GList *i = items; i; i = i->next)
    {
        GtkRecentInfo *info = i->data;

        if (gtk_recent_info_has_application(info, g_get_application_name()) && gtk_recent_info_exists(info))
            files = g_list_prepend(files, gtk_recent_info_ref(info));
    }

    g_list_free_full(items, (GDestroyNotify)gtk_recent_info_unref);
    return g_list_sort(files, newest_first);
}

// Clear Recent Files, as Win32's Recent > Clear: the files leave GNOME's list
static void clear_recent(void)
{
    GList *files = recent_files();

    for (GList *i = files; i; i = i->next)
        gtk_recent_manager_remove_item(gtk_recent_manager_get_default(), gtk_recent_info_get_uri(i->data), 0);

    g_list_free_full(files, (GDestroyNotify)gtk_recent_info_unref);
}

static void restore_recent_now(Editor *editor);
static void unless_unsaved(Editor *editor, EditorThen then);

static void on_recent_activated(GtkListBox *list, GtkListBoxRow *row, gpointer data)
{
    Editor     *editor = data;
    const char *uri    = g_object_get_data(G_OBJECT(row), "uri");
    const char *store  = g_object_get_data(G_OBJECT(row), "store");

    (void)list;

    if (g_object_get_data(G_OBJECT(row), "clear"))
    {
        gtk_popover_popdown(GTK_POPOVER(editor->recent_popover));
        clear_recent();
        return;
    }

    if (store)
    {
        gtk_popover_popdown(GTK_POPOVER(editor->recent_popover));
        g_free(editor->recent_store);
        editor->recent_store = g_strdup(store);
        unless_unsaved(editor, restore_recent_now);
        return;
    }

    if (!uri)
        return;

    gtk_popover_popdown(GTK_POPOVER(editor->recent_popover));
    gtk_widget_activate_action(GTK_WIDGET(editor->window), "win.open-recent", "s", uri);
}

// The row's close button: the file leaves the list, in every window
static void on_recent_remove(GtkButton *button, gpointer data)
{
    (void)data;

    gtk_recent_manager_remove_item(gtk_recent_manager_get_default(), g_object_get_data(G_OBJECT(button), "uri"), 0);
}

static void update_recent(Editor *editor);

// An unsaved document's close button: its changes are thrown away
static void on_recent_discard(GtkButton *button, gpointer data)
{
    recovery_discard(g_object_get_data(G_OBJECT(button), "store"));
    update_recent(data);
}

// A row: the name over where it is, and a button to remove it
static GtkWidget *list_row(const char *title, const char *where, const char *remove_tip)
{
    GtkWidget *row    = gtk_list_box_row_new();
    GtkWidget *box    = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *name   = gtk_label_new(title);
    GtkWidget *folder = gtk_label_new(where);
    GtkWidget *remove = gtk_button_new_from_icon_name("window-close-symbolic");

    gtk_label_set_xalign(GTK_LABEL(name), 0);
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(folder), 0);
    gtk_label_set_ellipsize(GTK_LABEL(folder), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_add_css_class(folder, "dim-label");
    gtk_widget_add_css_class(folder, "caption");
    gtk_label_set_max_width_chars(GTK_LABEL(name), 40);
    gtk_label_set_max_width_chars(GTK_LABEL(folder), 40);
    gtk_widget_set_hexpand(labels, TRUE);
    gtk_box_append(GTK_BOX(labels), name);
    gtk_box_append(GTK_BOX(labels), folder);

    gtk_widget_add_css_class(remove, "flat");
    gtk_widget_add_css_class(remove, "circular");
    gtk_widget_set_valign(remove, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(remove, remove_tip);
    g_object_set_data(G_OBJECT(row), "remove", remove);

    gtk_box_append(GTK_BOX(box), labels);
    gtk_box_append(GTK_BOX(box), remove);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    gtk_widget_set_tooltip_text(row, where);
    return row;
}

static GtkWidget *recent_row(GtkRecentInfo *info)
{
    char      *where  = recent_folder(info);
    GtkWidget *row    = list_row(gtk_recent_info_get_display_name(info), where, "Remove From Recent Files");
    GtkWidget *remove = g_object_get_data(G_OBJECT(row), "remove");
    char      *uri    = g_strdup(gtk_recent_info_get_uri(info));

    g_object_set_data_full(G_OBJECT(remove), "uri", g_strdup(uri), g_free);
    g_signal_connect(remove, "clicked", G_CALLBACK(on_recent_remove), 0);
    g_object_set_data_full(G_OBJECT(row), "uri", uri, g_free);

    g_free(where);
    return row;
}

// A document's unsaved changes, kept but not otherwise listed: untitled, or
// for a file not in the list
static GtkWidget *store_row(Editor *editor, RecoveryStore *store)
{
    GDateTime *time   = g_date_time_new_from_unix_local(store->time);
    char      *when   = g_date_time_format(time, "%e %b, %H:%M");
    char      *name   = store->filename ? g_path_get_basename(store->filename) : g_strdup("Untitled");
    char      *where  = g_strdup_printf("Unsaved changes, %s", g_strstrip(when));
    GtkWidget *row    = list_row(name, where, "Discard Unsaved Changes");
    GtkWidget *remove = g_object_get_data(G_OBJECT(row), "remove");

    g_object_set_data_full(G_OBJECT(remove), "store", g_strdup(store->store), g_free);
    g_signal_connect(remove, "clicked", G_CALLBACK(on_recent_discard), editor);
    g_object_set_data_full(G_OBJECT(row), "store", g_strdup(store->store), g_free);

    if (store->filename)
    {
        GFile *file  = g_file_new_for_path(store->filename);
        char  *shown = display_path(file);

        gtk_widget_set_tooltip_text(row, shown);
        g_free(shown);
        g_object_unref(file);
    }

    g_free(where);
    g_free(name);
    g_free(when);
    g_date_time_unref(time);
    return row;
}

static void update_recent(Editor *editor)
{
    GList     *items  = recent_files();
    GPtrArray *stores = recovery_list();
    int        count  = 0;

    gtk_list_box_remove_all(editor->recent_list);

    // unsaved documents first, those the files below don't bring back
    for (guint i = 0; i < stores->len; i++)
    {
        RecoveryStore *store  = stores->pdata[i];
        gboolean       listed = FALSE;
        int            shown  = 0;

        for (GList *f = items; f && shown < RECENT_MAX && store->filename && !listed; f = f->next, shown++)
        {
            char *path = g_filename_from_uri(gtk_recent_info_get_uri(f->data), 0, 0);

            listed = path && g_str_equal(path, store->filename);
            g_free(path);
        }

        if (!listed)
            gtk_list_box_append(editor->recent_list, store_row(editor, store));
    }

    for (GList *i = items; i && count < RECENT_MAX; i = i->next)
    {
        gtk_list_box_append(editor->recent_list, recent_row(i->data));
        count++;
    }

    if (count == 0 && stores->len == 0)
    {
        GtkWidget *row   = gtk_list_box_row_new();
        GtkWidget *label = gtk_label_new("No Recent Files");

        gtk_widget_add_css_class(label, "dim-label");
        gtk_widget_set_margin_top(label, 6);
        gtk_widget_set_margin_bottom(label, 6);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
        gtk_list_box_append(editor->recent_list, row);
    }
    else if (count > 0)
    {
        GtkWidget *row   = gtk_list_box_row_new();
        GtkWidget *label = gtk_label_new("Clear Recent Files");

        gtk_widget_set_margin_top(label, 4);
        gtk_widget_set_margin_bottom(label, 4);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
        g_object_set_data(G_OBJECT(row), "clear", GINT_TO_POINTER(1));
        gtk_widget_add_css_class(row, "recent-clear");
        gtk_list_box_append(editor->recent_list, row);
    }

    g_ptr_array_unref(stores);
    g_list_free_full(items, (GDestroyNotify)gtk_recent_info_unref);
}

static void on_recent_changed(GtkRecentManager *manager, gpointer window)
{
    (void)manager;
    update_recent(editor_of(window));
}

// Unsaved documents come and go as other windows close: look again as it opens
static void on_recent_shown(GtkWidget *popover, gpointer data)
{
    (void)popover;
    update_recent(data);
}

//
//	Other programs' changes to the file, as Win32 Neatpad notices them: the
//	file's size and time, compared with what they were when it was opened or
//	saved. A monitor says when to look, so Neatpad's own saves aren't news.
//	A followed file takes in what's appended, until it's rewritten shorter.
//
static void set_following(Editor *editor, gboolean following)
{
    GAction *action = g_action_map_lookup_action(G_ACTION_MAP(editor->window), "follow");

    editor->following = following;

    if (action)
        g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_boolean(following));
}

// FALSE if the file can't be looked at just now, which isn't news either
static gboolean look_at_file(Editor *editor, gboolean *on_disk, goffset *size, gint64 *modified)
{
    gboolean   gone;
    GError    *error = 0;
    GFileInfo *info  = g_file_query_info(editor->file, G_FILE_ATTRIBUTE_STANDARD_SIZE "," G_FILE_ATTRIBUTE_TIME_MODIFIED
                                                          "," G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC,
                                        G_FILE_QUERY_INFO_NONE, 0, &error);

    *on_disk  = info != 0;
    *size     = 0;
    *modified = 0;

    if (info)
    {
        *size     = g_file_info_get_size(info);
        *modified = (gint64)g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_MODIFIED) * G_USEC_PER_SEC +
                    g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC);
        g_object_unref(info);
        return TRUE;
    }

    gone = g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);

    g_error_free(error);
    return gone;
}

static void check_file(Editor *editor)
{
    gboolean on_disk;
    goffset  size;
    gint64   modified;
    int      notice;

    if (!editor->file || !look_at_file(editor, &on_disk, &size, &modified))
        return;

    if (on_disk == editor->on_disk && (!on_disk || (size == editor->size && modified == editor->modified)))
        return;

    if (editor->following && on_disk && size >= editor->size && neatpad_text_view_update_tail(editor->view))
    {
        editor->size     = size;
        editor->modified = modified;
        return;
    }

    // a file that grew is offered to follow (which reads it again too), as
    // Win32's bar offers Tail, unless following would drop unsaved changes
    if (!on_disk)
        notice = NOTICE_DELETED;
    else if (editor->on_disk && size > editor->size && !neatpad_text_view_get_modified(editor->view))
        notice = NOTICE_GREW;
    else
        notice = NOTICE_CHANGED;

    editor->on_disk  = on_disk;
    editor->size     = size;
    editor->modified = modified;
    set_following(editor, FALSE);
    show_notice(editor, notice);
}

static void on_file_changed(GFileMonitor *monitor, GFile *file, GFile *other, GFileMonitorEvent event,
                            gpointer data)
{
    (void)monitor;
    (void)file;
    (void)other;
    (void)event;
    check_file(data);
}

// Watch the window's file, as it is now
static void watch_file(Editor *editor)
{
    GAction *follow = g_action_map_lookup_action(G_ACTION_MAP(editor->window), "follow");

    if (editor->monitor)
    {
        g_signal_handlers_disconnect_by_data(editor->monitor, editor);
        g_file_monitor_cancel(editor->monitor);
        g_clear_object(&editor->monitor);
    }

    if (editor->file)
    {
        look_at_file(editor, &editor->on_disk, &editor->size, &editor->modified);
        editor->monitor = g_file_monitor_file(editor->file, G_FILE_MONITOR_WATCH_MOVES, 0, 0);

        if (editor->monitor)
        {
            g_file_monitor_set_rate_limit(editor->monitor, 250);
            g_signal_connect(editor->monitor, "changed", G_CALLBACK(on_file_changed), editor);
        }
    }

    // only a file on disk can be followed
    if (follow)
        g_simple_action_set_enabled(G_SIMPLE_ACTION(follow), editor->file != 0);
}

static gboolean load(Editor *editor, GFile *file)
{
    char    *path = g_file_get_path(file);
    gboolean ok   = path && neatpad_text_view_open_file(editor->view, path);

    if (ok)
    {
        // the document it replaced has gone, and its store with it
        recovery_reset(editor->recovery);
        hide_notice(editor);
        set_following(editor, FALSE);
        g_set_object(&editor->file, file);
        watch_file(editor);
        remember(file);
    }
    else
    {
        toast(editor, "Could not open the file");
    }

    g_free(path);
    update_title(editor);
    update_status(editor);
    return ok;
}

// A store's unsaved changes, in place of the window's document
static gboolean restore_store(Editor *editor, const char *store)
{
    char    *filename;
    gboolean session;

    if (!recovery_restore(editor->recovery, store, &filename, &session))
        return FALSE;

    set_following(editor, FALSE);
    g_clear_object(&editor->file);

    if (filename && *filename)
        editor->file = g_file_new_for_path(filename);

    watch_file(editor);
    update_title(editor);
    update_status(editor);

    if (session)
        show_notice(editor, editor->file ? NOTICE_RESTORED : NOTICE_RESTORED_UNTITLED);
    else
        show_notice(editor, editor->file ? NOTICE_RECOVERED : NOTICE_RECOVERED_UNTITLED);

    g_free(filename);
    return TRUE;
}

// A file opened, with its unsaved changes if any were kept
static gboolean open_file(Editor *editor, GFile *file)
{
    char *path  = g_file_get_path(file);
    char *store = path ? recovery_store_for(path) : 0;

    if (!load(editor, file))
    {
        // the file's gone, but its changes can still be restored
        if (store)
            restore_store(editor, store);
    }
    else if (store)
    {
        restore_store(editor, store);
    }

    g_free(store);
    g_free(path);
    return editor->file != 0;
}

//
//	Saving. Each takes what to do after it's saved, as saving before closing
//	or opening another file needs.
//
typedef struct
{
    GtkWindow *window;
    EditorThen then;

    // Save As: where, and how
    GFile   *file;
    guint    encoding;
    gboolean bom;
    guint    line_break;
} Pending;

static Pending *pending_new(Editor *editor, EditorThen then)
{
    Pending *pending = g_new0(Pending, 1);

    pending->window = g_object_ref(editor->window);
    pending->then   = then;
    return pending;
}

static void pending_free(Pending *pending)
{
    g_object_unref(pending->window);
    g_clear_object(&pending->file);
    g_free(pending);
}

//
//	Save to a file, in an NCP_* encoding, with a byte-order mark or not, and
//	line_break 0 to keep the line endings, or TXL_* to make them all one. The
//	store keeps what it needs from the file before it's saved over. Saved
//	another way, the document is opened again, as the file now is, and its
//	store starts afresh, as Win32's does.
//
static void save_to(Editor *editor, GFile *file, guint encoding, gboolean bom, guint line_break, EditorThen then)
{
    char    *path      = g_file_get_path(file);
    gboolean converted = encoding != neatpad_text_view_get_format(editor->view) || line_break != 0;
    gboolean ok;

    if (!converted)
        recovery_before_save(editor->recovery);

    ok = path && neatpad_text_view_save_file(editor->view, path, encoding, bom, line_break);
    g_free(path);

    if (!ok)
    {
        toast(editor, "Could not save the file");
        return;
    }

    if (converted)
    {
        recovery_reset(editor->recovery);
        set_following(editor, FALSE);
    }
    else
    {
        recovery_saved(editor->recovery);
    }

    hide_notice(editor);
    g_set_object(&editor->file, file);
    watch_file(editor);
    remember(file);
    update_title(editor);
    update_status(editor);

    if (then)
        then(editor);
}

static void save_pending(Pending *pending)
{
    save_to(editor_of(pending->window), pending->file, pending->encoding, pending->bom, pending->line_break,
            pending->then);
}

static void on_ascii_response(AdwAlertDialog *dialog, const char *response, gpointer data)
{
    Pending *pending = data;

    (void)dialog;

    if (g_str_equal(response, "save"))
        save_pending(pending);

    pending_free(pending);
}

//
//	Save As's encodings and line endings, as Win32's Save As has them. A
//	choice can't be a check box shown for one encoding alone, so UTF-8's
//	byte-order mark is an encoding of its own; UTF-16 and UTF-32 always have
//	one. ASCII is read as Latin-1 off Windows, and saved so.
//
static const struct
{
    const char *id;
    const char *label;
    guint       encoding;
    gboolean    bom;
} g_encodings[] = {
    {"ascii", "ASCII (Latin-1)", NCP_ASCII, FALSE},
    {"utf-8", "UTF-8", NCP_UTF8, FALSE},
    {"utf-8-bom", "UTF-8 with BOM", NCP_UTF8, TRUE},
    {"utf-16le", "UTF-16 LE", NCP_UTF16, TRUE},
    {"utf-16be", "UTF-16 BE", NCP_UTF16BE, TRUE},
    {"utf-32le", "UTF-32 LE", NCP_UTF32, TRUE},
    {"utf-32be", "UTF-32 BE", NCP_UTF32BE, TRUE},
};

static const struct
{
    const char *id;
    const char *label;
    guint       line_break;
} g_line_breaks[] = {
    {"keep", "Keep Existing", 0},
    {"lf", "Unix (LF)", TXL_LF},
    {"crlf", "Windows (CRLF)", TXL_CRLF},
    {"cr", "Mac (CR)", TXL_CR},
};

G_GNUC_BEGIN_IGNORE_DEPRECATIONS

//
//	The file and how to save it are chosen. Unicode to ASCII can't keep every
//	character: ask first.
//
static void on_save_as_response(GtkNativeDialog *native, int response, gpointer data)
{
    Pending        *pending  = data;
    GtkFileChooser *chooser  = GTK_FILE_CHOOSER(native);
    Editor         *editor   = editor_of(pending->window);
    const char     *encoding = gtk_file_chooser_get_choice(chooser, "encoding");
    const char     *ending   = gtk_file_chooser_get_choice(chooser, "line-endings");
    AdwDialog      *dialog;

    pending->file = response == GTK_RESPONSE_ACCEPT ? gtk_file_chooser_get_file(chooser) : 0;
    g_object_unref(native);

    if (!pending->file)
    {
        pending_free(pending);
        return;
    }

    pending->encoding = neatpad_text_view_get_format(editor->view);
    pending->bom      = neatpad_text_view_get_bom(editor->view);

    for (size_t i = 0; encoding && i < G_N_ELEMENTS(g_encodings); i++)
    {
        if (g_str_equal(encoding, g_encodings[i].id))
        {
            pending->encoding = g_encodings[i].encoding;
            pending->bom      = g_encodings[i].bom;
        }
    }

    for (size_t i = 0; ending && i < G_N_ELEMENTS(g_line_breaks); i++)
    {
        if (g_str_equal(ending, g_line_breaks[i].id))
            pending->line_break = g_line_breaks[i].line_break;
    }

    if (pending->encoding != NCP_ASCII || neatpad_text_view_get_format(editor->view) == NCP_ASCII)
    {
        save_pending(pending);
        pending_free(pending);
        return;
    }

    dialog = adw_alert_dialog_new("Save as ASCII?",
                                  "Characters that Latin-1 can\xe2\x80\x99t hold will be saved as \xe2\x80\x9c?\xe2\x80\x9d.");
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog), "cancel", "_Cancel", "save", "_Save", NULL);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "save", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "cancel");
    g_signal_connect(dialog, "response", G_CALLBACK(on_ascii_response), pending);
    adw_dialog_present(dialog, GTK_WIDGET(editor->window));
}

//
//	GtkFileDialog can't add choices, so Save As is a GtkFileChooserNative, as
//	GNOME Text Editor's is: through the portal, GNOME's file dialog shows them
//
static void save_as(Editor *editor, EditorThen then)
{
    GtkFileChooserNative *native;
    GtkFileChooser       *chooser;
    const char           *ids[G_N_ELEMENTS(g_encodings) + 1];
    const char           *labels[G_N_ELEMENTS(g_encodings) + 1];
    guint                 format = neatpad_text_view_get_format(editor->view);
    gboolean              bom    = neatpad_text_view_get_bom(editor->view);

    native  = gtk_file_chooser_native_new("Save As", editor->window, GTK_FILE_CHOOSER_ACTION_SAVE, "_Save", "_Cancel");
    chooser = GTK_FILE_CHOOSER(native);

    if (editor->file)
        gtk_file_chooser_set_file(chooser, editor->file, 0);
    else
        gtk_file_chooser_set_current_name(chooser, "Untitled.txt");

    // the encoding the document has now, chosen to start with
    for (size_t i = 0; i < G_N_ELEMENTS(g_encodings); i++)
    {
        ids[i]    = g_encodings[i].id;
        labels[i] = g_encodings[i].label;
    }

    ids[G_N_ELEMENTS(g_encodings)]    = 0;
    labels[G_N_ELEMENTS(g_encodings)] = 0;

    gtk_file_chooser_add_choice(chooser, "encoding", "Encoding", ids, labels);
    gtk_file_chooser_set_choice(chooser, "encoding", "utf-8");

    for (size_t i = 0; i < G_N_ELEMENTS(g_encodings); i++)
    {
        if (g_encodings[i].encoding == format && (format != NCP_UTF8 || g_encodings[i].bom == bom))
            gtk_file_chooser_set_choice(chooser, "encoding", g_encodings[i].id);
    }

    for (size_t i = 0; i < G_N_ELEMENTS(g_line_breaks); i++)
    {
        ids[i]    = g_line_breaks[i].id;
        labels[i] = g_line_breaks[i].label;
    }

    ids[G_N_ELEMENTS(g_line_breaks)]    = 0;
    labels[G_N_ELEMENTS(g_line_breaks)] = 0;

    gtk_file_chooser_add_choice(chooser, "line-endings", "Line Endings", ids, labels);
    gtk_file_chooser_set_choice(chooser, "line-endings", "keep");

    gtk_native_dialog_set_modal(GTK_NATIVE_DIALOG(native), TRUE);
    g_signal_connect(native, "response", G_CALLBACK(on_save_as_response), pending_new(editor, then));
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(native));
}

G_GNUC_END_IGNORE_DEPRECATIONS

// Save, as the file is now
static void save(Editor *editor, EditorThen then)
{
    if (editor->file)
        save_to(editor, editor->file, neatpad_text_view_get_format(editor->view),
                neatpad_text_view_get_bom(editor->view), 0, then);
    else
        save_as(editor, then);
}

//
//	Unsaved changes: ask whether to save them first, then carry on, unless
//	cancelled
//
static void on_unsaved_response(AdwAlertDialog *dialog, const char *response, gpointer data)
{
    Pending *pending = data;
    Editor  *editor  = editor_of(pending->window);

    (void)dialog;

    if (g_str_equal(response, "save"))
        save(editor, pending->then);
    else if (g_str_equal(response, "discard"))
        pending->then(editor);

    pending_free(pending);
}

static void unless_unsaved(Editor *editor, EditorThen then)
{
    AdwDialog *dialog;
    char      *name;

    if (!neatpad_text_view_get_modified(editor->view))
    {
        then(editor);
        return;
    }

    name   = editor->file ? g_file_get_basename(editor->file) : g_strdup("Untitled");
    dialog = adw_alert_dialog_new("Save Changes?", 0);

    adw_alert_dialog_format_body(ADW_ALERT_DIALOG(dialog), "\xe2\x80\x9c%s\xe2\x80\x9d has unsaved changes.", name);
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog), "cancel", "_Cancel", "discard", "_Discard", "save",
                                   "_Save", NULL);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "discard", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "save", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "save");
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "cancel");

    g_signal_connect(dialog, "response", G_CALLBACK(on_unsaved_response), pending_new(editor, then));
    adw_dialog_present(dialog, GTK_WIDGET(editor->window));
    g_free(name);
}

//
//	Closing. With Restore Session, unsaved changes are kept, to come back as
//	the file opens again. Without it, or if they can't be kept, the changes
//	are saved or discarded.
//
static void close_now(Editor *editor)
{
    editor->closing = TRUE;
    gtk_window_close(editor->window);
}

static gboolean closed(Editor *editor)
{
    recovery_free(editor->recovery);
    editor->recovery = 0;
    return FALSE;
}

static gboolean on_close_request(GtkWindow *window, gpointer data)
{
    Editor *editor = data;

    (void)window;

    if (editor->closing || !neatpad_text_view_get_modified(editor->view))
        return closed(editor);

    if (settings.restore_session && recovery_keep_session(editor->recovery))
        return closed(editor);

    unless_unsaved(editor, close_now);
    return TRUE;
}

//
//	Opening: a file chosen, or a recent one
//
static void on_open_chosen(GObject *source, GAsyncResult *result, gpointer data)
{
    Pending *pending = data;
    GFile   *file    = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), result, 0);

    if (file)
    {
        open_file(editor_of(pending->window), file);
        g_object_unref(file);
    }

    pending_free(pending);
}

static void choose_file_to_open(Editor *editor)
{
    GtkFileDialog *dialog = gtk_file_dialog_new();

    gtk_file_dialog_open(dialog, editor->window, 0, on_open_chosen, pending_new(editor, 0));
    g_object_unref(dialog);
}

static void open_recent_now(Editor *editor)
{
    GFile *file = g_file_new_for_uri(editor->recent_uri);

    open_file(editor, file);
    g_object_unref(file);
}

static void restore_recent_now(Editor *editor)
{
    if (!restore_store(editor, editor->recent_store))
        toast(editor, "Could not restore the unsaved changes");
}

//
//	Find and replace: Enter and the arrows find the next or previous match,
//	from the caret
//
static void find(Editor *editor, gboolean backwards)
{
    const char       *text = gtk_editable_get_text(GTK_EDITABLE(editor->search_entry));
    NeatpadFindResult result;

    if (*text == 0)
        return;

    result = neatpad_text_view_find(editor->view, text, backwards, gtk_toggle_button_get_active(editor->match_case));

    if (result == NEATPAD_FIND_NOT_FOUND)
        gtk_widget_add_css_class(GTK_WIDGET(editor->search_entry), "error");
    else
        gtk_widget_remove_css_class(GTK_WIDGET(editor->search_entry), "error");

    if (result == NEATPAD_FIND_WRAPPED)
        toast(editor, backwards ? "Wrapped round to the end" : "Wrapped round to the start");
}

// The selection, if it's a match, then on to the next
static void replace(Editor *editor)
{
    const char *text = gtk_editable_get_text(GTK_EDITABLE(editor->search_entry));

    if (*text == 0)
        return;

    neatpad_text_view_replace(editor->view, text, gtk_editable_get_text(GTK_EDITABLE(editor->replace_entry)),
                              gtk_toggle_button_get_active(editor->match_case), FALSE);
    find(editor, FALSE);
}

static void replace_all(Editor *editor)
{
    const char *text = gtk_editable_get_text(GTK_EDITABLE(editor->search_entry));
    guint64     count;
    char        message[64];

    if (*text == 0)
        return;

    count = neatpad_text_view_replace(editor->view, text, gtk_editable_get_text(GTK_EDITABLE(editor->replace_entry)),
                                      gtk_toggle_button_get_active(editor->match_case), TRUE);

    if (count == 0)
        gtk_widget_add_css_class(GTK_WIDGET(editor->search_entry), "error");

    g_snprintf(message, sizeof(message), "Replaced %" G_GUINT64_FORMAT " %s", count, count == 1 ? "match" : "matches");
    toast(editor, message);
}

static void on_search_activate(GtkSearchEntry *entry, gpointer data)
{
    (void)entry;
    find(data, FALSE);
}

static void on_search_next(GtkSearchEntry *entry, gpointer data)
{
    (void)entry;
    find(data, FALSE);
}

static void on_search_previous(GtkSearchEntry *entry, gpointer data)
{
    (void)entry;
    find(data, TRUE);
}

// Every match is highlighted as the text is typed, as on Win32: with Filter,
// only their lines show
static void show_matches(Editor *editor)
{
    neatpad_text_view_show_matches(editor->view, gtk_editable_get_text(GTK_EDITABLE(editor->search_entry)),
                                   gtk_toggle_button_get_active(editor->match_case),
                                   editor->filtering);
}

static void on_search_changed(GtkSearchEntry *entry, gpointer data)
{
    gtk_widget_remove_css_class(GTK_WIDGET(entry), "error");
    show_matches(data);
}

static void on_match_toggled(GtkToggleButton *button, gpointer data)
{
    (void)button;
    show_matches(data);
}

//
//	How matches show, as Win32's search bar has it: Highlight every match, or
//	Filter to the lines that match. A toggle group where libadwaita has one
//	(1.7), as Win32's pill; two linked toggle buttons before.
//
static void set_filtering(Editor *editor, gboolean filtering)
{
    if (editor->filtering == filtering)
        return;

    editor->filtering = filtering;
    show_matches(editor);
}

#if ADW_CHECK_VERSION(1, 7, 0)
static void on_match_mode(GObject *group, GParamSpec *spec, gpointer data)
{
    (void)spec;
    set_filtering(data, g_strcmp0(adw_toggle_group_get_active_name(ADW_TOGGLE_GROUP(group)), "filter") == 0);
}

static GtkWidget *match_mode(Editor *editor)
{
    GtkWidget *group     = adw_toggle_group_new();
    AdwToggle *highlight = adw_toggle_new();
    AdwToggle *filter    = adw_toggle_new();

    adw_toggle_set_name(highlight, "highlight");
    adw_toggle_set_icon_name(highlight, "neatpad-highlight-symbolic");
    adw_toggle_set_tooltip(highlight, "Highlight Every Match");
    adw_toggle_set_name(filter, "filter");
    adw_toggle_set_icon_name(filter, "neatpad-filter-symbolic");
    adw_toggle_set_tooltip(filter, "Show Only the Lines That Match");

    adw_toggle_group_add(ADW_TOGGLE_GROUP(group), highlight);
    adw_toggle_group_add(ADW_TOGGLE_GROUP(group), filter);
    adw_toggle_group_set_active_name(ADW_TOGGLE_GROUP(group), "highlight");
    gtk_widget_set_valign(group, GTK_ALIGN_CENTER);
    g_signal_connect(group, "notify::active-name", G_CALLBACK(on_match_mode), editor);
    return group;
}
#else
static void on_filter_toggled(GtkToggleButton *button, gpointer data)
{
    set_filtering(data, gtk_toggle_button_get_active(button));
}

static GtkWidget *match_mode(Editor *editor)
{
    GtkWidget *box       = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *highlight = gtk_toggle_button_new();
    GtkWidget *filter    = gtk_toggle_button_new();

    gtk_button_set_icon_name(GTK_BUTTON(highlight), "neatpad-highlight-symbolic");
    gtk_widget_set_tooltip_text(highlight, "Highlight Every Match");
    gtk_button_set_icon_name(GTK_BUTTON(filter), "neatpad-filter-symbolic");
    gtk_widget_set_tooltip_text(filter, "Show Only the Lines That Match");
    gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(filter), GTK_TOGGLE_BUTTON(highlight));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(highlight), TRUE);

    gtk_widget_add_css_class(box, "linked");
    gtk_box_append(GTK_BOX(box), highlight);
    gtk_box_append(GTK_BOX(box), filter);
    g_signal_connect(filter, "toggled", G_CALLBACK(on_filter_toggled), editor);
    return box;
}
#endif

static void on_search_stop(GtkSearchEntry *entry, gpointer data)
{
    Editor *editor = data;

    (void)entry;
    gtk_search_bar_set_search_mode(editor->search_bar, FALSE);
    gtk_widget_grab_focus(GTK_WIDGET(editor->view));
}

static void on_find_next_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    find(data, FALSE);
}

static void on_find_previous_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    find(data, TRUE);
}

static void on_replace_activate(GtkEntry *entry, gpointer data)
{
    (void)entry;
    replace(data);
}

static void on_replace_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    replace(data);
}

static void on_replace_all_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    replace_all(data);
}

//
//	Go to line: a line number, from 1
//
static void go_to_line(Editor *editor)
{
    const char *text = gtk_editable_get_text(GTK_EDITABLE(editor->goto_entry));
    guint64     line = 0;

    if (g_ascii_string_to_unsigned(text, 10, 1, G_MAXUINT64, &line, 0) &&
        neatpad_text_view_goto_line(editor->view, line - 1))
    {
        gtk_widget_remove_css_class(GTK_WIDGET(editor->goto_entry), "error");
        gtk_search_bar_set_search_mode(editor->goto_bar, FALSE);
        return;
    }

    gtk_widget_add_css_class(GTK_WIDGET(editor->goto_entry), "error");
}

static void on_goto_activate(GtkEntry *entry, gpointer data)
{
    (void)entry;
    go_to_line(data);
}

static void on_goto_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    go_to_line(data);
}

static void on_goto_changed(GtkEditable *entry, gpointer data)
{
    (void)data;
    gtk_widget_remove_css_class(GTK_WIDGET(entry), "error");
}

// Escape closes the bar, back to the text
static gboolean on_goto_key(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state,
                            gpointer data)
{
    Editor *editor = data;

    (void)controller;
    (void)keycode;
    (void)state;

    if (keyval != GDK_KEY_Escape)
        return FALSE;

    gtk_search_bar_set_search_mode(editor->goto_bar, FALSE);
    gtk_widget_grab_focus(GTK_WIDGET(editor->view));
    return TRUE;
}

// The find bar, with or without its replace row; only one bar shows at a time
static void show_find(Editor *editor, gboolean replacing)
{
    gtk_search_bar_set_search_mode(editor->goto_bar, FALSE);
    gtk_widget_set_visible(editor->replace_row, replacing);
    gtk_widget_set_visible(GTK_WIDGET(editor->search_bar), TRUE);
    gtk_search_bar_set_search_mode(editor->search_bar, TRUE);
    gtk_widget_grab_focus(GTK_WIDGET(editor->search_entry));
    gtk_editable_select_region(GTK_EDITABLE(editor->search_entry), 0, -1);
}

static void show_goto(Editor *editor)
{
    gtk_search_bar_set_search_mode(editor->search_bar, FALSE);
    gtk_widget_set_visible(GTK_WIDGET(editor->goto_bar), TRUE);
    gtk_search_bar_set_search_mode(editor->goto_bar, TRUE);
    gtk_widget_grab_focus(GTK_WIDGET(editor->goto_entry));
    gtk_editable_select_region(GTK_EDITABLE(editor->goto_entry), 0, -1);
}

//
//	Reloading or following the file drops unsaved changes: ask first
//
static void reload_now(Editor *editor)
{
    GFile *file = g_object_ref(editor->file);

    load(editor, file);
    g_object_unref(file);
}

static void follow_now(Editor *editor)
{
    char *path = g_file_get_path(editor->file);

    if (path && neatpad_text_view_tail_file(editor->view, path))
    {
        recovery_reset(editor->recovery);
        set_following(editor, TRUE);
        watch_file(editor);
        show_notice(editor, NOTICE_FOLLOWING);
    }
    else
    {
        set_following(editor, FALSE);
        toast(editor, "Could not open the file");
    }

    g_free(path);
    update_title(editor);
    update_status(editor);
}

static void on_discard_response(AdwAlertDialog *dialog, const char *response, gpointer data)
{
    Pending *pending = data;

    (void)dialog;

    if (g_str_equal(response, "discard"))
        pending->then(editor_of(pending->window));

    pending_free(pending);
}

static void unless_discarding(Editor *editor, const char *heading, const char *verb, EditorThen then)
{
    AdwDialog *dialog;

    if (!neatpad_text_view_get_modified(editor->view))
    {
        then(editor);
        return;
    }

    dialog = adw_alert_dialog_new(heading, "Unsaved changes will be lost.");
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog), "cancel", "_Cancel", "discard", verb, NULL);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "discard", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "cancel");
    g_signal_connect(dialog, "response", G_CALLBACK(on_discard_response), pending_new(editor, then));
    adw_dialog_present(dialog, GTK_WIDGET(editor->window));
}

//
//	The banner's button. For recovered changes: back to the file as saved, or,
//	without a file, an empty document, either dropping the changes. For
//	another program's change, reload, or follow a file it's adding to; for a
//	followed file, stop following.
//
static void on_banner_clicked(AdwBanner *banner, gpointer data)
{
    Editor *editor = data;

    (void)banner;

    if (editor->notice == NOTICE_CHANGED)
        unless_discarding(editor, "Reload File?", "_Reload", reload_now);
    else if (editor->notice == NOTICE_GREW)
        unless_discarding(editor, "Follow File?", "_Follow", follow_now);
    else if (editor->notice == NOTICE_FOLLOWING)
        reload_now(editor);
    else if (editor->notice == NOTICE_RECOVERED_UNTITLED || editor->notice == NOTICE_RESTORED_UNTITLED)
    {
        neatpad_text_view_new_file(editor->view);
        recovery_reset(editor->recovery);
        hide_notice(editor);
        update_title(editor);
        update_status(editor);
    }
    else if (editor->file && !g_file_query_exists(editor->file, 0))
    {
        toast(editor, "The saved file is unavailable. Your recovered changes are still open.");
    }
    else if (editor->file)
    {
        GFile *file = g_object_ref(editor->file);

        load(editor, file);
        g_object_unref(file);
    }
}

// A closed bar is hidden: even folded away, a search bar takes a little room
// below the text
static void on_search_mode(GObject *bar, GParamSpec *spec, gpointer data)
{
    (void)spec;
    (void)data;

    if (!gtk_search_bar_get_search_mode(GTK_SEARCH_BAR(bar)))
        gtk_widget_set_visible(GTK_WIDGET(bar), FALSE);
}

// Closing the find bar shows the whole document again, unhighlighted
static void on_find_mode(GObject *bar, GParamSpec *spec, gpointer data)
{
    Editor *editor = data;

    (void)spec;

    if (gtk_search_bar_get_search_mode(GTK_SEARCH_BAR(bar)))
        show_matches(editor);
    else
        neatpad_text_view_show_matches(editor->view, 0, FALSE, FALSE);
}

//
//	The window's actions, as its menu and keys name them: "win.save"
//
static void action_new_window(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    Editor *editor = data;

    (void)action;
    (void)parameter;
    gtk_window_present(editor_window_new(gtk_window_get_application(editor->window), 0));
}

static void action_open(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    (void)action;
    (void)parameter;
    unless_unsaved(data, choose_file_to_open);
}

static void action_open_recent(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    Editor *editor = data;

    (void)action;
    g_free(editor->recent_uri);
    editor->recent_uri = g_variant_dup_string(parameter, 0);
    unless_unsaved(editor, open_recent_now);
}

// The newest recent file other than this window's, as Win32's Ctrl+Shift+R:
// again, and it's back to the first
static void action_open_previous(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    Editor *editor  = data;
    char   *current = editor->file ? g_file_get_uri(editor->file) : 0;
    GList  *files   = recent_files();

    (void)action;
    (void)parameter;

    for (GList *i = files; i; i = i->next)
    {
        const char *uri = gtk_recent_info_get_uri(i->data);

        if (!current || !g_str_equal(uri, current))
        {
            gtk_widget_activate_action(GTK_WIDGET(editor->window), "win.open-recent", "s", uri);
            break;
        }
    }

    g_list_free_full(files, (GDestroyNotify)gtk_recent_info_unref);
    g_free(current);
}

// Line endings made one kind, in the selection or the whole document, as
// Win32's View > Line Endings: "normalize" (the document's own), "crlf", "lf"
// or "cr". A very large document is too slow to convert in place.
#define CONVERT_IN_PLACE_LIMIT (64 * 1024 * 1024)

static void action_convert_line_endings(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    Editor         *editor = data;
    const char     *to     = g_variant_get_string(parameter, 0);
    guint           format = g_str_equal(to, "crlf") ? TXL_CRLF
                             : g_str_equal(to, "lf") ? TXL_LF
                             : g_str_equal(to, "cr") ? TXL_CR
                                                     : 0;
    NeatpadDocStats stats;

    (void)action;

    if (!neatpad_text_view_get_doc_stats(editor->view, &stats))
        return;

    if ((stats.selected_bytes ? stats.selected_bytes : stats.bytes) > CONVERT_IN_PLACE_LIMIT)
        toast(editor, "This file is too large to convert its line endings here");
    else if (!neatpad_text_view_convert_line_endings(editor->view, format))
        toast(editor, "This file is read-only");
}

static void action_save(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    (void)action;
    (void)parameter;
    save(data, 0);
}

static void action_save_as(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    (void)action;
    (void)parameter;
    save_as(data, 0);
}

static void action_find(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    (void)action;
    (void)parameter;
    show_find(data, FALSE);
}

static void action_replace(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    (void)action;
    (void)parameter;
    show_find(data, TRUE);
}

static void action_find_next(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    (void)action;
    (void)parameter;
    find(data, FALSE);
}

static void action_find_previous(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    (void)action;
    (void)parameter;
    find(data, TRUE);
}

static void action_go_to_line(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    (void)action;
    (void)parameter;
    show_goto(data);
}

static void action_close(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    Editor *editor = data;

    (void)action;
    (void)parameter;
    gtk_window_close(editor->window);
}

//
//	Zoom, as Win32 Neatpad's: the font in 10% steps, from 10% to 500%
//
#define ZOOM_STEP 10
#define ZOOM_MIN  10
#define ZOOM_MAX  500

static void set_zoom(Editor *editor, int zoom)
{
    zoom = CLAMP(zoom, ZOOM_MIN, ZOOM_MAX);

    if (zoom != editor->zoom)
    {
        editor->zoom = zoom;
        apply_settings(editor);
        update_status(editor);
    }
}

// Ctrl and the wheel: a step a notch, a fine-grained wheel's turns added up
static gboolean on_zoom_scroll(GtkEventControllerScroll *controller, double dx, double dy, gpointer data)
{
    Editor *editor = data;
    int     steps;

    (void)dx;

    if (!(gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(controller)) & GDK_CONTROL_MASK))
        return FALSE;

    editor->zoom_rest -= dy;
    steps = (int)editor->zoom_rest;
    editor->zoom_rest -= steps;

    if (steps)
        set_zoom(editor, editor->zoom + steps * ZOOM_STEP);

    return TRUE;
}

// win.follow, a check item: follow the file's end, or stop
static void action_follow(GSimpleAction *action, GVariant *state, gpointer data)
{
    Editor *editor = data;

    (void)action;

    if (!editor->file)
        return;

    if (g_variant_get_boolean(state))
        unless_discarding(editor, "Follow File?", "_Follow", follow_now);
    else
        reload_now(editor);
}

static void action_zoom_in(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    Editor *editor = data;

    (void)action;
    (void)parameter;
    set_zoom(editor, editor->zoom + ZOOM_STEP);
}

static void action_zoom_out(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    Editor *editor = data;

    (void)action;
    (void)parameter;
    set_zoom(editor, editor->zoom - ZOOM_STEP);
}

static void action_zoom_reset(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    (void)action;
    (void)parameter;
    set_zoom(data, 100);
}

static const GActionEntry window_actions[] = {
    {"new-window", action_new_window, 0, 0, 0, {0}},
    {"open", action_open, 0, 0, 0, {0}},
    {"open-recent", action_open_recent, "s", 0, 0, {0}},
    {"open-previous", action_open_previous, 0, 0, 0, {0}},
    {"convert-line-endings", action_convert_line_endings, "s", 0, 0, {0}},
    {"save", action_save, 0, 0, 0, {0}},
    {"save-as", action_save_as, 0, 0, 0, {0}},
    {"find", action_find, 0, 0, 0, {0}},
    {"replace", action_replace, 0, 0, 0, {0}},
    {"find-next", action_find_next, 0, 0, 0, {0}},
    {"find-previous", action_find_previous, 0, 0, 0, {0}},
    {"go-to-line", action_go_to_line, 0, 0, 0, {0}},
    {"close", action_close, 0, 0, 0, {0}},
    {"follow", 0, 0, "false", action_follow, {0}},
    {"zoom-in", action_zoom_in, 0, 0, 0, {0}},
    {"zoom-out", action_zoom_out, 0, 0, 0, {0}},
    {"zoom-reset", action_zoom_reset, 0, 0, 0, {0}},
};

//
//	The window: a header bar, the view, the find and go-to-line bars, and the
//	status bar
//
static GMenuModel *primary_menu(void)
{
    GMenu     *menu    = g_menu_new();
    GMenu     *style   = g_menu_new();
    GMenu     *zooming = g_menu_new();
    GMenu     *window  = g_menu_new();
    GMenu     *file    = g_menu_new();
    GMenu     *search  = g_menu_new();
    GMenu     *view    = g_menu_new();
    GMenu     *endings = g_menu_new();
    GMenu     *convert = g_menu_new();
    GMenu     *show    = g_menu_new();
    GMenu     *general = g_menu_new();
    GMenuItem *styles  = g_menu_item_new(0, 0);
    GMenuItem *zoom    = g_menu_item_new(0, 0);

    // the style switcher and the zoom, widgets of their own (style_switcher,
    // zoom_row)
    g_menu_item_set_attribute(styles, "custom", "s", "style");
    g_menu_append_item(style, styles);
    g_object_unref(styles);
    g_menu_item_set_attribute(zoom, "custom", "s", "zoom");
    g_menu_append_item(zooming, zoom);
    g_object_unref(zoom);

    g_menu_append(window, "_New Window", "win.new-window");
    g_menu_append(file, "_Save", "win.save");
    g_menu_append(file, "Save _As\xe2\x80\xa6", "win.save-as");
    g_menu_append(file, "_Follow File", "win.follow");
    g_menu_append(search, "_Find\xe2\x80\xa6", "win.find");
    g_menu_append(search, "_Replace\xe2\x80\xa6", "win.replace");
    g_menu_append(search, "_Go to Line\xe2\x80\xa6", "win.go-to-line");
    // View, as Win32's View menu: line endings (long lines are in Preferences)
    g_menu_append(convert, "_Normalize", "win.convert-line-endings::normalize");
    g_menu_append(convert, "Convert to _Windows (CRLF)", "win.convert-line-endings::crlf");
    g_menu_append(convert, "Convert to _Unix (LF)", "win.convert-line-endings::lf");
    g_menu_append(convert, "Convert to _Mac (CR)", "win.convert-line-endings::cr");
    g_menu_append(show, "_Always Show", "app.show-line-endings::always");
    g_menu_append(show, "Only _Mismatched", "app.show-line-endings::mismatched");
    g_menu_append_section(endings, 0, G_MENU_MODEL(convert));
    g_menu_append_section(endings, 0, G_MENU_MODEL(show));
    g_menu_append_submenu(view, "_Line Endings", G_MENU_MODEL(endings));

    g_menu_append(general, "_Preferences", "app.preferences");
#if ADW_CHECK_VERSION(1, 8, 0)
    // AdwApplication's, from shortcuts-dialog.ui
    g_menu_append(general, "_Keyboard Shortcuts", "app.shortcuts");
#endif
    g_menu_append(general, "_About Neatpad", "app.about");

    g_menu_append_section(menu, 0, G_MENU_MODEL(style));
    g_menu_append_section(menu, 0, G_MENU_MODEL(zooming));
    g_menu_append_section(menu, 0, G_MENU_MODEL(window));
    g_menu_append_section(menu, 0, G_MENU_MODEL(file));
    g_menu_append_section(menu, 0, G_MENU_MODEL(search));
    g_menu_append_section(menu, 0, G_MENU_MODEL(view));
    g_menu_append_section(menu, 0, G_MENU_MODEL(general));

    g_object_unref(style);
    g_object_unref(zooming);
    g_object_unref(window);
    g_object_unref(file);
    g_object_unref(search);
    g_object_unref(view);
    g_object_unref(endings);
    g_object_unref(convert);
    g_object_unref(show);
    g_object_unref(general);
    return G_MENU_MODEL(menu);
}

//
//	The style switcher: follow the system, light or dark, as round swatches,
//	as GNOME Text Editor has them. Each is a radio button on app.style.
//
static const char app_css[] = ".style-switcher { margin: 6px 0; }"
                                ".style-switcher button { min-width: 40px; min-height: 40px; padding: 0;"
                                "  border-radius: 9999px; border: 1px solid alpha(black, 0.15); box-shadow: none; }"
                                ".style-switcher button.follow {"
                                "  background: linear-gradient(-45deg, #1d1d20 49.99%, #ffffff 50%); }"
                                ".style-switcher button.light { background: #ffffff; }"
                                ".style-switcher button.dark { background: #1d1d20; }"
                                ".style-switcher button:checked {"
                                "  outline: 2px solid @accent_bg_color; outline-offset: 2px; }"
                                // hairlines above the text, the find and go-to bars and the
                                // status bar, in libadwaita's border colour
                                ".neatpad-text { border-top: 1px solid alpha(currentColor, 0.15); }"
                                ".neatpad-bar, .neatpad-status { border-top: 1px solid alpha(currentColor, 0.15); }"
                                ".neatpad-status { padding: 7px 12px; }"
                                // the find and go-to bars as Text Editor's: roomier, with taller
                                // entries
                                ".neatpad-bar > revealer > box { padding: 9px 12px; }"
                                ".neatpad-bar entry { min-height: 38px; }"
                                ".zoom-row > button:not(.circular) { min-width: 64px; }"
                                ".recent-files { min-width: 300px; }"
                                ".recent-files row { padding: 4px 4px 4px 10px; }";

// Zoom out, the zoom (to reset it) and zoom in, as Text Editor's menu has them
static GtkWidget *zoom_row(Editor *editor)
{
    GtkWidget *box  = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *out  = gtk_button_new_from_icon_name("list-remove-symbolic");
    GtkWidget *in   = gtk_button_new_from_icon_name("list-add-symbolic");
    GtkWidget *zoom = gtk_button_new_with_label("100%");

    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(box, "zoom-row");
    gtk_widget_add_css_class(out, "flat");
    gtk_widget_add_css_class(out, "circular");
    gtk_widget_add_css_class(in, "flat");
    gtk_widget_add_css_class(in, "circular");
    gtk_widget_add_css_class(zoom, "flat");
    gtk_widget_set_tooltip_text(out, "Zoom Out");
    gtk_widget_set_tooltip_text(in, "Zoom In");
    gtk_widget_set_tooltip_text(zoom, "Reset Zoom");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(out), "win.zoom-out");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(in), "win.zoom-in");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(zoom), "win.zoom-reset");

    gtk_box_append(GTK_BOX(box), out);
    gtk_box_append(GTK_BOX(box), zoom);
    gtk_box_append(GTK_BOX(box), in);

    editor->zoom_reset = zoom;
    return box;
}

static GtkWidget *style_switcher(void)
{
    static const struct
    {
        const char *name;
        const char *tooltip;
    } styles[] = {
        {"follow", "Follow System Style"},
        {"light", "Light Style"},
        {"dark", "Dark Style"},
    };

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);

    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(box, "style-switcher");

    for (size_t i = 0; i < G_N_ELEMENTS(styles); i++)
    {
        GtkWidget *button = gtk_toggle_button_new();
        char      *action = g_strdup_printf("app.style::%s", styles[i].name);

        gtk_widget_add_css_class(button, styles[i].name);
        gtk_widget_set_tooltip_text(button, styles[i].tooltip);
        gtk_actionable_set_detailed_action_name(GTK_ACTIONABLE(button), action);
        gtk_box_append(GTK_BOX(box), button);
        g_free(action);
    }

    return box;
}

static GtkWidget *header_bar(Editor *editor)
{
    GtkWidget  *header = adw_header_bar_new();
    GtkWidget  *open   = adw_split_button_new();
    GtkWidget  *menu   = gtk_menu_button_new();
    GMenuModel *model  = primary_menu();

    // Open, with the recent files below its arrow: each its name and folder,
    // and a button to remove it, as Text Editor's
    editor->recent_list    = GTK_LIST_BOX(gtk_list_box_new());
    editor->recent_popover = gtk_popover_new();
    gtk_list_box_set_selection_mode(editor->recent_list, GTK_SELECTION_NONE);
    gtk_widget_add_css_class(GTK_WIDGET(editor->recent_list), "navigation-sidebar");
    gtk_widget_add_css_class(GTK_WIDGET(editor->recent_list), "recent-files");
    g_signal_connect(editor->recent_list, "row-activated", G_CALLBACK(on_recent_activated), editor);
    g_signal_connect(editor->recent_popover, "show", G_CALLBACK(on_recent_shown), editor);
    gtk_popover_set_child(GTK_POPOVER(editor->recent_popover), GTK_WIDGET(editor->recent_list));
    adw_split_button_set_label(ADW_SPLIT_BUTTON(open), "_Open");
    adw_split_button_set_use_underline(ADW_SPLIT_BUTTON(open), TRUE);
    adw_split_button_set_popover(ADW_SPLIT_BUTTON(open), GTK_POPOVER(editor->recent_popover));
    adw_split_button_set_dropdown_tooltip(ADW_SPLIT_BUTTON(open), "Recent Files");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(open), "win.open");

    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu), "open-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu), model);
    gtk_popover_menu_add_child(GTK_POPOVER_MENU(gtk_menu_button_get_popover(GTK_MENU_BUTTON(menu))), style_switcher(),
                               "style");
    gtk_popover_menu_add_child(GTK_POPOVER_MENU(gtk_menu_button_get_popover(GTK_MENU_BUTTON(menu))),
                               zoom_row(editor), "zoom");
    gtk_menu_button_set_primary(GTK_MENU_BUTTON(menu), TRUE);
    gtk_widget_set_tooltip_text(menu, "Main Menu");
    g_object_unref(model);

    editor->title = ADW_WINDOW_TITLE(adw_window_title_new("", ""));
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), GTK_WIDGET(editor->title));

    adw_header_bar_pack_start(ADW_HEADER_BAR(header), open);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), menu);
    return header;
}

static GtkWidget *search_bar(Editor *editor)
{
    GtkWidget *bar         = gtk_search_bar_new();
    GtkWidget *rows        = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *find_row    = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *replace_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *entry       = gtk_search_entry_new();
    GtkWidget *previous    = gtk_button_new_from_icon_name("go-up-symbolic");
    GtkWidget *next        = gtk_button_new_from_icon_name("go-down-symbolic");
    GtkWidget *matching    = gtk_toggle_button_new_with_label("Aa");
    GtkWidget *mode        = match_mode(editor);
    GtkWidget *replacement = gtk_entry_new();
    GtkWidget *one         = gtk_button_new_with_mnemonic("_Replace");
    GtkWidget *all         = gtk_button_new_with_mnemonic("Replace _All");

    gtk_widget_add_css_class(bar, "neatpad-bar");
    gtk_widget_set_size_request(entry, ENTRY_WIDTH, -1);
    gtk_widget_set_size_request(replacement, ENTRY_WIDTH, -1);
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(entry), "Search");
    gtk_entry_set_placeholder_text(GTK_ENTRY(replacement), "Replace");
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(replacement), GTK_ENTRY_ICON_PRIMARY, "edit-find-replace-symbolic");
    gtk_widget_set_tooltip_text(previous, "Previous Match");
    gtk_widget_set_tooltip_text(next, "Next Match");
    gtk_widget_set_tooltip_text(matching, "Match Case");

    gtk_box_append(GTK_BOX(find_row), entry);
    gtk_box_append(GTK_BOX(find_row), previous);
    gtk_box_append(GTK_BOX(find_row), next);
    gtk_box_append(GTK_BOX(find_row), matching);
    gtk_box_append(GTK_BOX(find_row), mode);

    gtk_box_append(GTK_BOX(replace_row), replacement);
    gtk_box_append(GTK_BOX(replace_row), one);
    gtk_box_append(GTK_BOX(replace_row), all);

    gtk_box_append(GTK_BOX(rows), find_row);
    gtk_box_append(GTK_BOX(rows), replace_row);

    gtk_search_bar_set_child(GTK_SEARCH_BAR(bar), rows);
    gtk_search_bar_connect_entry(GTK_SEARCH_BAR(bar), GTK_EDITABLE(entry));
    gtk_search_bar_set_show_close_button(GTK_SEARCH_BAR(bar), TRUE);

    g_signal_connect(entry, "activate", G_CALLBACK(on_search_activate), editor);
    g_signal_connect(entry, "next-match", G_CALLBACK(on_search_next), editor);
    g_signal_connect(entry, "previous-match", G_CALLBACK(on_search_previous), editor);
    g_signal_connect(entry, "search-changed", G_CALLBACK(on_search_changed), editor);
    g_signal_connect(entry, "stop-search", G_CALLBACK(on_search_stop), editor);
    g_signal_connect(previous, "clicked", G_CALLBACK(on_find_previous_clicked), editor);
    g_signal_connect(next, "clicked", G_CALLBACK(on_find_next_clicked), editor);
    g_signal_connect(replacement, "activate", G_CALLBACK(on_replace_activate), editor);
    g_signal_connect(one, "clicked", G_CALLBACK(on_replace_clicked), editor);
    g_signal_connect(all, "clicked", G_CALLBACK(on_replace_all_clicked), editor);
    g_signal_connect(matching, "toggled", G_CALLBACK(on_match_toggled), editor);

    gtk_widget_set_visible(bar, FALSE);
    g_signal_connect(bar, "notify::search-mode-enabled", G_CALLBACK(on_search_mode), 0);
    g_signal_connect(bar, "notify::search-mode-enabled", G_CALLBACK(on_find_mode), editor);

    editor->search_bar    = GTK_SEARCH_BAR(bar);
    editor->search_entry  = GTK_SEARCH_ENTRY(entry);
    editor->match_case    = GTK_TOGGLE_BUTTON(matching);
    editor->replace_row   = replace_row;
    editor->replace_entry = GTK_ENTRY(replacement);
    return bar;
}

static GtkWidget *goto_bar(Editor *editor)
{
    GtkWidget          *bar   = gtk_search_bar_new();
    GtkWidget          *box   = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget          *label = gtk_label_new_with_mnemonic("_Go to Line");
    GtkWidget          *entry = gtk_entry_new();
    GtkWidget          *go    = gtk_button_new_with_label("Go");
    GtkEventController *keys  = gtk_event_controller_key_new();

    gtk_widget_add_css_class(bar, "neatpad-bar");
    gtk_widget_add_css_class(label, "heading");
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), entry);
    gtk_editable_set_width_chars(GTK_EDITABLE(entry), 10);
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Line number");
    gtk_entry_set_input_purpose(GTK_ENTRY(entry), GTK_INPUT_PURPOSE_DIGITS);
    gtk_widget_add_css_class(go, "suggested-action");

    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), entry);
    gtk_box_append(GTK_BOX(box), go);
    gtk_search_bar_set_child(GTK_SEARCH_BAR(bar), box);
    gtk_search_bar_set_show_close_button(GTK_SEARCH_BAR(bar), TRUE);

    g_signal_connect(entry, "activate", G_CALLBACK(on_goto_activate), editor);
    g_signal_connect(entry, "changed", G_CALLBACK(on_goto_changed), editor);
    g_signal_connect(go, "clicked", G_CALLBACK(on_goto_clicked), editor);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_goto_key), editor);
    gtk_widget_add_controller(bar, keys);

    gtk_widget_set_visible(bar, FALSE);
    g_signal_connect(bar, "notify::search-mode-enabled", G_CALLBACK(on_search_mode), 0);

    editor->goto_bar   = GTK_SEARCH_BAR(bar);
    editor->goto_entry = GTK_ENTRY(entry);
    return bar;
}

// A status bar pane: dim text, as GNOME's status lines are
static GtkLabel *pane(GtkWidget *box)
{
    GtkWidget *label = gtk_label_new("");

    gtk_widget_add_css_class(label, "dim-label");
    gtk_box_append(GTK_BOX(box), label);
    return GTK_LABEL(label);
}

// The panes in Win32 Neatpad's order: the caret and the document's size on
// the left, the rest on the right
// The zoom pane, clicked: back to 100%
static void on_zoom_pane_clicked(GtkGestureClick *gesture, int presses, double x, double y, gpointer data)
{
    Editor *editor = data;

    (void)gesture;
    (void)presses;
    (void)x;
    (void)y;
    gtk_widget_activate_action(GTK_WIDGET(editor->window), "win.zoom-reset", 0);
}

static GtkWidget *status_bar(Editor *editor)
{
    GtkWidget  *box   = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    GtkGesture *click = gtk_gesture_click_new();

    gtk_widget_add_css_class(box, "neatpad-status");

    editor->position  = pane(box);
    editor->doc_stats = pane(box);
    gtk_widget_set_hexpand(GTK_WIDGET(editor->doc_stats), TRUE);
    gtk_label_set_xalign(editor->doc_stats, 0);

    // the index link isn't dimmed: it's a link
    editor->index = pane(box);
    gtk_widget_remove_css_class(GTK_WIDGET(editor->index), "dim-label");
    g_signal_connect(editor->index, "activate-link", G_CALLBACK(on_index_link), editor);

    // the zoom shows only while it isn't 100%, as a browser's does
    editor->zoom_pane   = pane(box);
    editor->line_format = pane(box);
    editor->encoding    = pane(box);

    gtk_widget_set_tooltip_text(GTK_WIDGET(editor->zoom_pane), "Reset Zoom");
    gtk_widget_set_cursor_from_name(GTK_WIDGET(editor->zoom_pane), "pointer");
    gtk_widget_add_controller(GTK_WIDGET(editor->zoom_pane), GTK_EVENT_CONTROLLER(click));
    g_signal_connect(click, "released", G_CALLBACK(on_zoom_pane_clicked), editor);
    return box;
}

static GtkWindow *editor_window_new(GtkApplication *app, GFile *file)
{
    Editor             *editor     = g_new0(Editor, 1);
    GtkEventController *zoom_wheel = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    GtkWidget     *window  = adw_application_window_new(app);
    RecoveryClient client;
    GtkWidget *toolbar = adw_toolbar_view_new();
    GtkWidget *scroll  = gtk_scrolled_window_new();
    GtkWidget *view    = neatpad_text_view_new();

    editor->window = GTK_WINDOW(window);
    editor->view   = NEATPAD_TEXT_VIEW(view);
    editor->zoom   = 100;
    editor->toasts = ADW_TOAST_OVERLAY(adw_toast_overlay_new());
    g_object_set_data_full(G_OBJECT(window), "editor", editor, editor_free);

    client.view      = editor->view;
    client.filename  = recovery_filename;
    client.failed    = recovery_failed;
    client.data      = editor;
    editor->recovery = recovery_new(&client);

    g_action_map_add_action_entries(G_ACTION_MAP(window), window_actions, G_N_ELEMENTS(window_actions), editor);

    // GNOME's scrollbars: overlaid, unless Accessibility says always show them.
    // Ctrl and the wheel zoom, before the view scrolls.
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), view);
    gtk_widget_add_css_class(scroll, "neatpad-text");
    gtk_event_controller_set_propagation_phase(zoom_wheel, GTK_PHASE_CAPTURE);
    g_signal_connect(zoom_wheel, "scroll", G_CALLBACK(on_zoom_scroll), editor);
    gtk_widget_add_controller(scroll, zoom_wheel);
    adw_toast_overlay_set_child(editor->toasts, scroll);

    // the banner is below the header bar; the find and go-to-line bars are
    // below the text, above the status bar
    editor->banner = ADW_BANNER(adw_banner_new(""));
    adw_banner_set_use_markup(editor->banner, FALSE);
    g_signal_connect(editor->banner, "button-clicked", G_CALLBACK(on_banner_clicked), editor);

    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header_bar(editor));
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), GTK_WIDGET(editor->banner));
    adw_toolbar_view_add_bottom_bar(ADW_TOOLBAR_VIEW(toolbar), search_bar(editor));
    adw_toolbar_view_add_bottom_bar(ADW_TOOLBAR_VIEW(toolbar), goto_bar(editor));
    editor->status_bar = status_bar(editor);
    adw_toolbar_view_add_bottom_bar(ADW_TOOLBAR_VIEW(toolbar), editor->status_bar);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), GTK_WIDGET(editor->toasts));
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), toolbar);

    gtk_window_set_default_size(GTK_WINDOW(window), 900, 650);

    g_signal_connect(view, "changed", G_CALLBACK(on_changed), editor);
    g_signal_connect(view, "cursor-changed", G_CALLBACK(on_cursor_changed), editor);
    g_signal_connect(view, "selection-changed", G_CALLBACK(on_cursor_changed), editor);
    g_signal_connect(view, "index-changed", G_CALLBACK(on_cursor_changed), editor);
    g_signal_connect(window, "close-request", G_CALLBACK(on_close_request), editor);
    g_signal_connect_object(gtk_recent_manager_get_default(), "changed", G_CALLBACK(on_recent_changed), window, 0);
    g_signal_connect_object(adw_style_manager_get_default(), "notify::dark", G_CALLBACK(on_style_changed), window, 0);
#if ADW_CHECK_VERSION(1, 6, 0)
    g_signal_connect_object(adw_style_manager_get_default(), "notify::accent-color-rgba",
                            G_CALLBACK(on_style_changed), window, 0);
#endif

    apply_settings(editor);
    apply_scheme(editor);

    if (file)
        open_file(editor, file);
    else
        watch_file(editor);

    update_recent(editor);
    update_title(editor);
    update_status(editor);
    gtk_widget_grab_focus(view);
    return GTK_WINDOW(window);
}

//
//	The application: a window for each file it's given, or an empty one
//
static void action_about(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    static const char *const developers[] = {"James Brown https://www.catch22.net/about", 0};

    GtkApplication *app = data;
    AdwDialog      *about;

    (void)action;
    (void)parameter;

    about = adw_about_dialog_new();
    adw_about_dialog_set_application_name(ADW_ABOUT_DIALOG(about), "Neatpad");
    adw_about_dialog_set_application_icon(ADW_ABOUT_DIALOG(about), APP_ID);
    adw_about_dialog_set_developer_name(ADW_ABOUT_DIALOG(about), "J Brown\nCatch22 Solutions");
    adw_about_dialog_set_version(ADW_ABOUT_DIALOG(about), NEATPAD_VERSION);
    adw_about_dialog_set_copyright(ADW_ABOUT_DIALOG(about), "\u00a9 1999\u20132026 James Brown, Catch22 Solutions");
    adw_about_dialog_set_website(ADW_ABOUT_DIALOG(about), "https://www.catch22.net/projects/neatpad");
    adw_about_dialog_set_license_type(ADW_ABOUT_DIALOG(about), GTK_LICENSE_MIT_X11);

    // Credits: the developers, each with a link
    adw_about_dialog_set_developers(ADW_ABOUT_DIALOG(about), (const char **)developers);
    adw_dialog_present(about, GTK_WIDGET(gtk_application_get_active_window(app)));
}

// GNOME's monospace font, which Use System Font follows as it changes
static void on_desktop_font(GSettings *changed, const char *key, gpointer data)
{
    (void)changed;
    (void)key;
    (void)data;
    apply_settings_everywhere();
}

static void watch_desktop_font(void)
{
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    GSettingsSchema       *schema =
        source ? g_settings_schema_source_lookup(source, "org.gnome.desktop.interface", TRUE) : 0;

    if (schema && g_settings_schema_has_key(schema, "monospace-font-name"))
    {
        desktop = g_settings_new("org.gnome.desktop.interface");
        g_signal_connect(desktop, "changed::monospace-font-name", G_CALLBACK(on_desktop_font), 0);
    }

    if (schema)
        g_settings_schema_unref(schema);
}

// app.style: follow the system, light or dark
static void action_style(GSimpleAction *action, GVariant *state, gpointer data)
{
    (void)data;

    g_simple_action_set_state(action, state);
    g_free(settings.style);
    settings.style = g_variant_dup_string(state, 0);
    apply_style();
    save_settings();
}

// app.show-line-endings: "always", or "mismatched" (those that aren't the
// document's own)
static void action_show_line_endings(GSimpleAction *action, GVariant *state, gpointer data)
{
    (void)data;

    g_simple_action_set_state(action, state);
    settings.show_line_endings = g_str_equal(g_variant_get_string(state, 0), "always");
    save_settings();
    apply_settings_everywhere();
}

//
//	Preferences, applied to every window as they change
//
static void action_preferences(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    (void)action;
    (void)parameter;
    preferences_show(gtk_application_get_active_window(GTK_APPLICATION(data)), apply_settings_everywhere);
}

// Each window asks about its own unsaved changes as it closes
static void action_quit(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    GList *windows = gtk_application_get_windows(GTK_APPLICATION(data));

    (void)action;
    (void)parameter;

    for (GList *w = windows; w; w = w->next)
        gtk_window_close(GTK_WINDOW(w->data));
}

static const GActionEntry app_actions[] = {
    {"style", 0, "s", "'follow'", action_style, {0}},
    {"show-line-endings", 0, "s", "'mismatched'", action_show_line_endings, {0}},
    {"preferences", action_preferences, 0, 0, 0, {0}},
    {"about", action_about, 0, 0, 0, {0}},
    {"quit", action_quit, 0, 0, 0, {0}},
};

static void on_startup(GApplication *app, gpointer data)
{
    static const struct
    {
        const char *action;
        const char *keys[4];
    } accels[] = {
        {"win.new-window", {"<Ctrl>n", 0}},
        {"win.open", {"<Ctrl>o", 0}},
        {"win.open-previous", {"<Ctrl><Shift>r", 0}},
        {"win.save", {"<Ctrl>s", 0}},
        {"win.save-as", {"<Ctrl><Shift>s", 0}},
        {"win.find", {"<Ctrl>f", 0}},
        {"win.replace", {"<Ctrl>h", 0}},
        {"win.find-next", {"<Ctrl>g", "F3", 0}},
        {"win.find-previous", {"<Ctrl><Shift>g", "<Shift>F3", 0}},
        {"win.go-to-line", {"<Ctrl>i", 0}},
        {"win.close", {"<Ctrl>w", 0}},
        {"app.preferences", {"<Ctrl>comma", 0}},
        {"win.zoom-in", {"<Ctrl>plus", "<Ctrl>equal", "<Ctrl>KP_Add", 0}},
        {"win.zoom-out", {"<Ctrl>minus", "<Ctrl>KP_Subtract", 0}},
        {"win.zoom-reset", {"<Ctrl>0", "<Ctrl>KP_0", 0}},
        {"app.quit", {"<Ctrl>q", 0}},
    };

    GtkCssProvider *css = gtk_css_provider_new();

    (void)data;

    g_action_map_add_action_entries(G_ACTION_MAP(app), app_actions, G_N_ELEMENTS(app_actions), app);

    // the saved style, and the app's CSS: the style switcher and hairlines
    load_settings();
    watch_desktop_font();
    g_action_group_change_action_state(G_ACTION_GROUP(app), "style", g_variant_new_string(settings.style));
    g_simple_action_set_state(G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(app), "show-line-endings")),
                              g_variant_new_string(settings.show_line_endings ? "always" : "mismatched"));
    apply_style();

    gtk_css_provider_load_from_string(css, app_css);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    for (size_t i = 0; i < G_N_ELEMENTS(accels); i++)
        gtk_application_set_accels_for_action(GTK_APPLICATION(app), accels[i].action, accels[i].keys);
}

//
//	Starting: without files, the document last worked on; with them, those
//	files. Either way, a file's unsaved changes come back as it opens.
//
// The window a file is open in, or 0
static GtkWindow *window_of(GtkApplication *app, GFile *file)
{
    for (GList *w = gtk_application_get_windows(app); w; w = w->next)
    {
        Editor *editor = editor_of(w->data);

        if (editor && editor->file && g_file_equal(editor->file, file))
            return GTK_WINDOW(w->data);
    }

    return 0;
}

// A file that's open already has its window brought forward
static void present_file(GtkApplication *app, GFile *file)
{
    GtkWindow *window = window_of(app, file);

    gtk_window_present(window ? window : editor_window_new(app, file));
}

// The newest of the unsaved documents and the recent files
static void open_last(Editor *editor)
{
    GPtrArray     *stores = recovery_list();
    GList         *files  = recent_files();
    RecoveryStore *store  = stores->len ? stores->pdata[0] : 0;
    GtkRecentInfo *recent = files ? files->data : 0;

    if (store && recent && g_date_time_to_unix(gtk_recent_info_get_modified(recent)) > store->time)
        store = 0;

    if (!store || !restore_store(editor, store->store))
    {
        if (recent)
        {
            GFile *file = g_file_new_for_uri(gtk_recent_info_get_uri(recent));

            open_file(editor, file);
            g_object_unref(file);
        }
    }

    g_list_free_full(files, (GDestroyNotify)gtk_recent_info_unref);
    g_ptr_array_unref(stores);
}

static void on_activate(GApplication *app, gpointer data)
{
    GtkWindow *window = editor_window_new(GTK_APPLICATION(app), 0);

    (void)data;

    if (!started)
        open_last(editor_of(window));

    started = TRUE;
    gtk_window_present(window);
}

static void on_open(GApplication *app, GFile **files, int count, const char *hint, gpointer data)
{
    (void)hint;
    (void)data;

    started = TRUE;

    for (int i = 0; i < count; i++)
        present_file(GTK_APPLICATION(app), files[i]);
}

static void on_shutdown(GApplication *app, gpointer data)
{
    (void)app;
    (void)data;

    g_clear_object(&desktop);
}

int main(int argc, char *argv[])
{
    AdwApplication *app;
    int             status;

    // the name recent files are recorded under
    g_set_application_name("Neatpad");

    // the icon scripts/install-dev-desktop.sh installs, named for the app
    gtk_window_set_default_icon_name(APP_ID);

    app = adw_application_new(APP_ID, G_APPLICATION_HANDLES_OPEN);
    g_signal_connect(app, "startup", G_CALLBACK(on_startup), 0);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), 0);
    g_signal_connect(app, "open", G_CALLBACK(on_open), 0);
    g_signal_connect(app, "shutdown", G_CALLBACK(on_shutdown), 0);

    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
