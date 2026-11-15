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
    GMenu           *recent; // the Open button's menu
    char            *recent_uri; // the recent file being opened
    GtkSearchBar    *search_bar;
    GtkSearchEntry  *search_entry;
    GtkToggleButton *match_case;
    GtkWidget       *replace_row;
    GtkEntry        *replace_entry;
    GtkSearchBar    *goto_bar;
    GtkEntry        *goto_entry;
    GtkLabel        *position;
    GtkLabel        *encoding;
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
    NOTICE_DELETED,            // or deleted or moved it
    NOTICE_FOLLOWING           // the file is followed as it grows: Stop
};

// What to do once a document's changes are saved or discarded
typedef void (*EditorThen)(Editor *editor);

//
//	Settings, for every window, kept in ~/.config/catch22/neatpad/settings.ini
//
typedef struct
{
    char    *style; // "follow", "light" or "dark"
    gboolean system_font;
    char    *font;
    gboolean restore_session;
    gboolean line_numbers;
    gboolean current_line;
    gboolean highlight_changes;
    int      tab_width;
    gboolean insert_spaces;
    gboolean auto_indent;
} Settings;

static Settings settings;

// GNOME's own settings, for its monospace font: 0 where they aren't installed
static GSettings *desktop;

// The session's saved files, collected as its windows close
static GPtrArray *session_files;
static gboolean   quitting;

static char *settings_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "catch22", "neatpad", "settings.ini", NULL);
}

static gboolean read_boolean(GKeyFile *file, const char *key, gboolean otherwise)
{
    return g_key_file_has_key(file, "editor", key, 0) ? g_key_file_get_boolean(file, "editor", key, 0) : otherwise;
}

static char *read_string(GKeyFile *file, const char *key, const char *otherwise)
{
    char *value = g_key_file_get_string(file, "editor", key, 0);

    return value ? value : g_strdup(otherwise);
}

static void load_settings(void)
{
    GKeyFile *file = g_key_file_new();
    char     *path = settings_path();

    g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, 0);

    settings.style             = read_string(file, "style", "follow");
    settings.system_font       = read_boolean(file, "use-system-font", TRUE);
    settings.font              = read_string(file, "font", "Monospace 11");
    settings.restore_session   = read_boolean(file, "restore-session", TRUE);
    settings.line_numbers      = read_boolean(file, "line-numbers", FALSE);
    settings.current_line      = read_boolean(file, "highlight-current-line", FALSE);
    settings.highlight_changes = read_boolean(file, "highlight-changes", TRUE);
    settings.insert_spaces     = read_boolean(file, "insert-spaces", FALSE);
    settings.auto_indent       = read_boolean(file, "auto-indent", FALSE);
    settings.tab_width         = g_key_file_has_key(file, "editor", "tab-width", 0)
                                     ? CLAMP(g_key_file_get_integer(file, "editor", "tab-width", 0), 1, 16)
                                     : 4;

    g_free(path);
    g_key_file_free(file);
}

static void save_settings(void)
{
    GKeyFile *file   = g_key_file_new();
    char     *path   = settings_path();
    char     *folder = g_path_get_dirname(path);

    g_key_file_set_string(file, "editor", "style", settings.style);
    g_key_file_set_boolean(file, "editor", "use-system-font", settings.system_font);
    g_key_file_set_string(file, "editor", "font", settings.font);
    g_key_file_set_boolean(file, "editor", "restore-session", settings.restore_session);
    g_key_file_set_boolean(file, "editor", "line-numbers", settings.line_numbers);
    g_key_file_set_boolean(file, "editor", "highlight-current-line", settings.current_line);
    g_key_file_set_boolean(file, "editor", "highlight-changes", settings.highlight_changes);
    g_key_file_set_integer(file, "editor", "tab-width", settings.tab_width);
    g_key_file_set_boolean(file, "editor", "insert-spaces", settings.insert_spaces);
    g_key_file_set_boolean(file, "editor", "auto-indent", settings.auto_indent);

    g_mkdir_with_parents(folder, 0700);

    if (!g_key_file_save_to_file(file, path, 0))
        g_warning("can't save %s", path);

    g_free(folder);
    g_free(path);
    g_key_file_free(file);
}

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
    g_clear_object(&editor->recent);
    g_free(editor->recent_uri);
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
    guint styles = (settings.line_numbers ? TXS_LINENUMBERS | TXS_SELMARGIN : 0) |
                   (settings.current_line ? TXS_HIGHLIGHTCURLINE : 0) |
                   (settings.highlight_changes ? TXS_HIGHLIGHTCHANGES : 0) |
                   (settings.insert_spaces ? TXS_INSERTSPACES : 0) | (settings.auto_indent ? TXS_AUTOINDENT : 0);

    char *system = settings.system_font && desktop ? g_settings_get_string(desktop, "monospace-font-name") : 0;

    neatpad_text_view_set_font(editor->view, system && *system ? system : settings.font);
    g_free(system);
    neatpad_text_view_set_tab_width(editor->view, (guint)settings.tab_width);
    neatpad_text_view_set_style(editor->view,
                                TXS_LINENUMBERS | TXS_SELMARGIN | TXS_HIGHLIGHTCURLINE | TXS_HIGHLIGHTCHANGES |
                                    TXS_INSERTSPACES | TXS_AUTOINDENT,
                                styles);
}

static void apply_settings_everywhere(void)
{
    GList *windows = gtk_application_get_windows(GTK_APPLICATION(g_application_get_default()));

    for (GList *w = windows; w; w = w->next)
    {
        if (editor_of(w->data))
            apply_settings(editor_of(w->data));
    }
}

// The text's colours follow the style: light or dark, and the accent colour
// where libadwaita has one (1.6)
static void apply_scheme(Editor *editor)
{
    AdwStyleManager *manager = adw_style_manager_get_default();
    GdkRGBA         *accent  = 0;

#if ADW_CHECK_VERSION(1, 6, 0)
    accent = adw_style_manager_get_accent_color_rgba(manager);
#endif

    neatpad_text_view_set_scheme(editor->view, adw_style_manager_get_dark(manager), accent);

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
static void update_title(Editor *editor)
{
    gboolean modified = neatpad_text_view_get_modified(editor->view);
    char    *name     = editor->file ? g_file_get_basename(editor->file) : g_strdup("Untitled");
    char    *title    = g_strdup_printf("%s%s", modified ? "\xe2\x80\xa2 " : "", name);
    GFile   *parent   = editor->file ? g_file_get_parent(editor->file) : 0;
    char    *folder   = parent ? g_file_get_parse_name(parent) : g_strdup("");

    adw_window_title_set_title(editor->title, title);
    adw_window_title_set_subtitle(editor->title, folder);
    gtk_window_set_title(editor->window, title);

    g_free(folder);
    g_clear_object(&parent);
    g_free(title);
    g_free(name);
}

// The caret's line and column, counted from 1, and the encoding
static void update_status(Editor *editor)
{
    guint64  line;
    guint64  column;
    gboolean known;
    char     text[96];

    neatpad_text_view_get_cursor(editor->view, &line, &column, &known);
    g_snprintf(text, sizeof(text), "Ln %" G_GUINT64_FORMAT "%s, Col %" G_GUINT64_FORMAT, line + 1, known ? "" : "?",
               column + 1);

    gtk_label_set_text(editor->position, text);
    gtk_label_set_text(editor->encoding, neatpad_text_view_get_encoding(editor->view));
}

static void on_changed(NeatpadTextView *view, gpointer data)
{
    Editor *editor = data;

    (void)view;
    update_title(editor);
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

// A menu label shows a file's name as it is: an underline is doubled, or it
// would mark a mnemonic
static char *menu_label(const char *name)
{
    GString *label = g_string_new(0);

    for (const char *c = name; *c; c++)
    {
        if (*c == '_')
            g_string_append_c(label, '_');

        g_string_append_c(label, *c);
    }

    return g_string_free(label, FALSE);
}

static void update_recent(Editor *editor)
{
    GList *items = gtk_recent_manager_get_items(gtk_recent_manager_get_default());
    int    count = 0;

    g_menu_remove_all(editor->recent);
    items = g_list_sort(items, newest_first);

    for (GList *i = items; i && count < RECENT_MAX; i = i->next)
    {
        GtkRecentInfo *info = i->data;
        GMenuItem     *item;
        char          *label;

        if (!gtk_recent_info_has_application(info, g_get_application_name()) || !gtk_recent_info_exists(info))
            continue;

        label = menu_label(gtk_recent_info_get_display_name(info));
        item  = g_menu_item_new(label, 0);
        g_menu_item_set_action_and_target(item, "win.open-recent", "s", gtk_recent_info_get_uri(info));
        g_menu_append_item(editor->recent, item);

        g_object_unref(item);
        g_free(label);
        count++;
    }

    // without an action, it's shown disabled
    if (count == 0)
        g_menu_append(editor->recent, "No Recent Files", 0);

    g_list_free_full(items, (GDestroyNotify)gtk_recent_info_unref);
}

static void on_recent_changed(GtkRecentManager *manager, gpointer window)
{
    (void)manager;
    update_recent(editor_of(window));
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

    editor->on_disk  = on_disk;
    editor->size     = size;
    editor->modified = modified;
    set_following(editor, FALSE);
    show_notice(editor, on_disk ? NOTICE_CHANGED : NOTICE_DELETED);
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

//
//	Saving. Each takes what to do after it's saved, as saving before closing
//	or opening another file needs.
//
typedef struct
{
    GtkWindow *window;
    EditorThen then;
} Pending;

static Pending *pending_new(Editor *editor, EditorThen then)
{
    Pending *pending = g_new(Pending, 1);

    pending->window = g_object_ref(editor->window);
    pending->then   = then;
    return pending;
}

static void pending_free(Pending *pending)
{
    g_object_unref(pending->window);
    g_free(pending);
}

// The store keeps what it needs from the file before it's saved over
static void save_to(Editor *editor, GFile *file, EditorThen then)
{
    char    *path = g_file_get_path(file);
    gboolean ok;

    recovery_before_save(editor->recovery);
    ok = path && neatpad_text_view_save_file(editor->view, path);
    g_free(path);

    if (!ok)
    {
        toast(editor, "Could not save the file");
        return;
    }

    recovery_saved(editor->recovery);
    hide_notice(editor);
    g_set_object(&editor->file, file);
    watch_file(editor);
    remember(file);
    update_title(editor);
    update_status(editor);

    if (then)
        then(editor);
}

static void on_save_as_chosen(GObject *source, GAsyncResult *result, gpointer data)
{
    Pending *pending = data;
    GFile   *file    = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), result, 0);

    if (file)
    {
        save_to(editor_of(pending->window), file, pending->then);
        g_object_unref(file);
    }

    pending_free(pending);
}

static void save_as(Editor *editor, EditorThen then)
{
    GtkFileDialog *dialog = gtk_file_dialog_new();

    if (editor->file)
        gtk_file_dialog_set_initial_file(dialog, editor->file);
    else
        gtk_file_dialog_set_initial_name(dialog, "Untitled.txt");

    gtk_file_dialog_save(dialog, editor->window, 0, on_save_as_chosen, pending_new(editor, then));
    g_object_unref(dialog);
}

static void save(Editor *editor, EditorThen then)
{
    if (editor->file)
        save_to(editor, editor->file, then);
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
//	Closing. With Restore Session, unsaved changes are kept for next time, and
//	the last window's file (or every window's, quitting) is reopened then.
//	Without it, or if they can't be kept, the changes are saved or discarded.
//
static void close_now(Editor *editor)
{
    editor->closing = TRUE;
    gtk_window_close(editor->window);
}

static gboolean closed(Editor *editor, gboolean kept)
{
    GList *windows = gtk_application_get_windows(gtk_window_get_application(editor->window));

    if (!kept && editor->file && (quitting || g_list_length(windows) == 1))
        g_ptr_array_add(session_files, g_file_get_uri(editor->file));

    recovery_free(editor->recovery);
    editor->recovery = 0;
    return FALSE;
}

static gboolean on_close_request(GtkWindow *window, gpointer data)
{
    Editor *editor = data;

    (void)window;

    if (editor->closing || !neatpad_text_view_get_modified(editor->view))
        return closed(editor, FALSE);

    if (settings.restore_session && recovery_keep_session(editor->recovery))
        return closed(editor, TRUE);

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
        load(editor_of(pending->window), file);
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

    load(editor, file);
    g_object_unref(file);
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

static void on_search_changed(GtkSearchEntry *entry, gpointer data)
{
    (void)data;
    gtk_widget_remove_css_class(GTK_WIDGET(entry), "error");
}

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
    gtk_search_bar_set_search_mode(editor->search_bar, TRUE);
    gtk_widget_grab_focus(GTK_WIDGET(editor->search_entry));
    gtk_editable_select_region(GTK_EDITABLE(editor->search_entry), 0, -1);
}

static void show_goto(Editor *editor)
{
    gtk_search_bar_set_search_mode(editor->search_bar, FALSE);
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
//	another program's change, reload; for a followed file, stop following.
//
static void on_banner_clicked(AdwBanner *banner, gpointer data)
{
    Editor *editor = data;

    (void)banner;

    if (editor->notice == NOTICE_CHANGED)
        unless_discarding(editor, "Reload File?", "_Reload", reload_now);
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

static const GActionEntry window_actions[] = {
    {"new-window", action_new_window, 0, 0, 0, {0}},
    {"open", action_open, 0, 0, 0, {0}},
    {"open-recent", action_open_recent, "s", 0, 0, {0}},
    {"save", action_save, 0, 0, 0, {0}},
    {"save-as", action_save_as, 0, 0, 0, {0}},
    {"find", action_find, 0, 0, 0, {0}},
    {"replace", action_replace, 0, 0, 0, {0}},
    {"find-next", action_find_next, 0, 0, 0, {0}},
    {"find-previous", action_find_previous, 0, 0, 0, {0}},
    {"go-to-line", action_go_to_line, 0, 0, 0, {0}},
    {"close", action_close, 0, 0, 0, {0}},
    {"follow", 0, 0, "false", action_follow, {0}},
};

//
//	The window: a header bar, the view, the find and go-to-line bars, and the
//	status bar
//
static GMenuModel *primary_menu(void)
{
    GMenu     *menu    = g_menu_new();
    GMenu     *style   = g_menu_new();
    GMenu     *file    = g_menu_new();
    GMenu     *search  = g_menu_new();
    GMenu     *general = g_menu_new();
    GMenuItem *styles  = g_menu_item_new(0, 0);

    // the style switcher, a widget of its own (style_switcher)
    g_menu_item_set_attribute(styles, "custom", "s", "style");
    g_menu_append_item(style, styles);
    g_object_unref(styles);

    g_menu_append(file, "_New Window", "win.new-window");
    g_menu_append(file, "_Save", "win.save");
    g_menu_append(file, "Save _As\xe2\x80\xa6", "win.save-as");
    g_menu_append(file, "_Follow File", "win.follow");
    g_menu_append(search, "_Find\xe2\x80\xa6", "win.find");
    g_menu_append(search, "_Replace\xe2\x80\xa6", "win.replace");
    g_menu_append(search, "_Go to Line\xe2\x80\xa6", "win.go-to-line");
    g_menu_append(general, "_Preferences", "app.preferences");
    g_menu_append(general, "_About Neatpad", "app.about");
    g_menu_append(general, "_Quit", "app.quit");

    g_menu_append_section(menu, 0, G_MENU_MODEL(style));
    g_menu_append_section(menu, 0, G_MENU_MODEL(file));
    g_menu_append_section(menu, 0, G_MENU_MODEL(search));
    g_menu_append_section(menu, 0, G_MENU_MODEL(general));

    g_object_unref(style);
    g_object_unref(file);
    g_object_unref(search);
    g_object_unref(general);
    return G_MENU_MODEL(menu);
}

//
//	The style switcher: follow the system, light or dark, as round swatches,
//	as GNOME Text Editor has them. Each is a radio button on app.style.
//
static const char style_css[] = ".style-switcher { margin: 6px 0; }"
                                ".style-switcher button { min-width: 40px; min-height: 40px; padding: 0;"
                                "  border-radius: 9999px; border: 1px solid alpha(black, 0.15); box-shadow: none; }"
                                ".style-switcher button.follow {"
                                "  background: linear-gradient(-45deg, #1d1d20 49.99%, #ffffff 50%); }"
                                ".style-switcher button.light { background: #ffffff; }"
                                ".style-switcher button.dark { background: #1d1d20; }"
                                ".style-switcher button:checked {"
                                "  outline: 2px solid @accent_bg_color; outline-offset: 2px; }";

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

    // Open, with the recent files below its arrow
    editor->recent = g_menu_new();
    adw_split_button_set_label(ADW_SPLIT_BUTTON(open), "_Open");
    adw_split_button_set_use_underline(ADW_SPLIT_BUTTON(open), TRUE);
    adw_split_button_set_menu_model(ADW_SPLIT_BUTTON(open), G_MENU_MODEL(editor->recent));
    adw_split_button_set_dropdown_tooltip(ADW_SPLIT_BUTTON(open), "Recent Files");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(open), "win.open");

    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu), "open-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu), model);
    gtk_popover_menu_add_child(GTK_POPOVER_MENU(gtk_menu_button_get_popover(GTK_MENU_BUTTON(menu))), style_switcher(),
                               "style");
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
    GtkWidget *replacement = gtk_entry_new();
    GtkWidget *one         = gtk_button_new_with_mnemonic("_Replace");
    GtkWidget *all         = gtk_button_new_with_mnemonic("Replace _All");

    gtk_widget_set_size_request(entry, ENTRY_WIDTH, -1);
    gtk_widget_set_size_request(replacement, ENTRY_WIDTH, -1);
    gtk_entry_set_placeholder_text(GTK_ENTRY(replacement), "Replace with");
    gtk_widget_set_tooltip_text(previous, "Previous Match");
    gtk_widget_set_tooltip_text(next, "Next Match");
    gtk_widget_set_tooltip_text(matching, "Match Case");

    gtk_box_append(GTK_BOX(find_row), entry);
    gtk_box_append(GTK_BOX(find_row), previous);
    gtk_box_append(GTK_BOX(find_row), next);
    gtk_box_append(GTK_BOX(find_row), matching);

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

    gtk_widget_add_css_class(label, "heading");
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), entry);
    gtk_editable_set_width_chars(GTK_EDITABLE(entry), 10);
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

    editor->goto_bar   = GTK_SEARCH_BAR(bar);
    editor->goto_entry = GTK_ENTRY(entry);
    return bar;
}

static GtkWidget *status_bar(Editor *editor)
{
    GtkWidget *box      = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    GtkWidget *position = gtk_label_new("");
    GtkWidget *encoding = gtk_label_new("");

    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 4);
    gtk_widget_set_margin_bottom(box, 4);
    gtk_widget_set_hexpand(position, TRUE);
    gtk_label_set_xalign(GTK_LABEL(position), 1);
    gtk_widget_add_css_class(position, "dim-label");
    gtk_widget_add_css_class(encoding, "dim-label");

    gtk_box_append(GTK_BOX(box), position);
    gtk_box_append(GTK_BOX(box), encoding);

    editor->position = GTK_LABEL(position);
    editor->encoding = GTK_LABEL(encoding);
    return box;
}

static GtkWindow *editor_window_new(GtkApplication *app, GFile *file)
{
    Editor        *editor  = g_new0(Editor, 1);
    GtkWidget     *window  = adw_application_window_new(app);
    RecoveryClient client;
    GtkWidget *toolbar = adw_toolbar_view_new();
    GtkWidget *scroll  = gtk_scrolled_window_new();
    GtkWidget *view    = neatpad_text_view_new();

    editor->window = GTK_WINDOW(window);
    editor->view   = NEATPAD_TEXT_VIEW(view);
    editor->toasts = ADW_TOAST_OVERLAY(adw_toast_overlay_new());
    g_object_set_data_full(G_OBJECT(window), "editor", editor, editor_free);

    client.view      = editor->view;
    client.filename  = recovery_filename;
    client.failed    = recovery_failed;
    client.data      = editor;
    editor->recovery = recovery_new(&client);

    g_action_map_add_action_entries(G_ACTION_MAP(window), window_actions, G_N_ELEMENTS(window_actions), editor);

    // GNOME's scrollbars: overlaid, unless Accessibility says always show them
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), view);
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
    adw_toolbar_view_add_bottom_bar(ADW_TOOLBAR_VIEW(toolbar), status_bar(editor));
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), GTK_WIDGET(editor->toasts));
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), toolbar);

    gtk_window_set_default_size(GTK_WINDOW(window), 900, 650);

    g_signal_connect(view, "changed", G_CALLBACK(on_changed), editor);
    g_signal_connect(view, "cursor-changed", G_CALLBACK(on_cursor_changed), editor);
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
        load(editor, file);
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
    GtkApplication *app = data;
    AdwDialog      *about;

    (void)action;
    (void)parameter;

    about = adw_about_dialog_new();
    adw_about_dialog_set_application_name(ADW_ABOUT_DIALOG(about), "Neatpad");
    adw_about_dialog_set_developer_name(ADW_ABOUT_DIALOG(about), "J Brown");
    adw_about_dialog_set_website(ADW_ABOUT_DIALOG(about), "https://www.catch22.net/tuts/neatpad");
    adw_about_dialog_set_license_type(ADW_ABOUT_DIALOG(about), GTK_LICENSE_MIT_X11);
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

//
//	Preferences: each row changes a setting, saves it, and applies it to every
//	window
//
static void on_switch_row(GObject *row, GParamSpec *spec, gpointer setting)
{
    (void)spec;
    *(gboolean *)setting = adw_switch_row_get_active(ADW_SWITCH_ROW(row));
    save_settings();
    apply_settings_everywhere();
}

static void on_tab_width(GObject *row, GParamSpec *spec, gpointer data)
{
    (void)spec;
    (void)data;
    settings.tab_width = (int)adw_spin_row_get_value(ADW_SPIN_ROW(row));
    save_settings();
    apply_settings_everywhere();
}

static void on_font(GObject *button, GParamSpec *spec, gpointer data)
{
    PangoFontDescription *font = gtk_font_dialog_button_get_font_desc(GTK_FONT_DIALOG_BUTTON(button));

    (void)spec;
    (void)data;

    if (!font)
        return;

    g_free(settings.font);
    settings.font = pango_font_description_to_string(font);
    save_settings();
    apply_settings_everywhere();
}

// Only fixed-width fonts: the engine measures a character width
static gboolean is_monospace(gpointer item, gpointer data)
{
    PangoFontFamily *family = PANGO_IS_FONT_FACE(item) ? pango_font_face_get_family(PANGO_FONT_FACE(item))
                                                       : PANGO_IS_FONT_FAMILY(item) ? PANGO_FONT_FAMILY(item) : 0;

    (void)data;
    return family && pango_font_family_is_monospace(family);
}

static GtkWidget *switch_row(GtkWidget *group, const char *title, const char *subtitle, gboolean *setting)
{
    GtkWidget *row = adw_switch_row_new();

    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);

    if (subtitle)
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);

    adw_switch_row_set_active(ADW_SWITCH_ROW(row), *setting);
    g_signal_connect(row, "notify::active", G_CALLBACK(on_switch_row), setting);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(group), row);
    return row;
}

static void action_preferences(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    AdwDialog            *dialog      = adw_preferences_dialog_new();
    GtkWidget            *page        = adw_preferences_page_new();
    GtkWidget            *fonts       = adw_preferences_group_new();
    GtkWidget            *display     = adw_preferences_group_new();
    GtkWidget            *indentation = adw_preferences_group_new();
    GtkWidget            *font_row    = adw_action_row_new();
    GtkFontDialog        *chooser     = gtk_font_dialog_new();
    GtkWidget            *font_button = gtk_font_dialog_button_new(chooser); // the button owns chooser
    GtkFilter            *monospace   = GTK_FILTER(gtk_custom_filter_new(is_monospace, 0, 0));
    PangoFontDescription *font        = pango_font_description_from_string(settings.font);
    GtkWidget            *tabs        = adw_spin_row_new_with_range(1, 16, 1);
    GtkWidget            *behaviour   = adw_preferences_group_new();
    GtkWidget            *system_font;

    (void)action;
    (void)parameter;

    adw_preferences_page_set_title(ADW_PREFERENCES_PAGE(page), "Editor");
    adw_preferences_page_set_icon_name(ADW_PREFERENCES_PAGE(page), "document-edit-symbolic");

    // the font
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(fonts), "Font");
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(font_row), "Editor Font");
    gtk_font_dialog_set_filter(chooser, monospace);
    gtk_font_dialog_button_set_level(GTK_FONT_DIALOG_BUTTON(font_button), GTK_FONT_LEVEL_FONT);
    gtk_font_dialog_button_set_use_font(GTK_FONT_DIALOG_BUTTON(font_button), TRUE);
    gtk_font_dialog_button_set_font_desc(GTK_FONT_DIALOG_BUTTON(font_button), font);
    gtk_widget_set_valign(font_button, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(font_row), font_button);
    g_signal_connect(font_button, "notify::font-desc", G_CALLBACK(on_font), 0);

    // the font is chosen only when it isn't the system's
    system_font = switch_row(fonts, "Use System Font", 0, &settings.system_font);
    g_object_bind_property(system_font, "active", font_row, "sensitive",
                           G_BINDING_SYNC_CREATE | G_BINDING_INVERT_BOOLEAN);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(fonts), font_row);

    // what's kept between runs
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(behaviour), "Behavior");
    switch_row(behaviour, "Restore Session", "Reopen files and unsaved changes when Neatpad starts",
               &settings.restore_session);

    // what's shown
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(display), "Display");
    switch_row(display, "Line Numbers", 0, &settings.line_numbers);
    switch_row(display, "Highlight Current Line", 0, &settings.current_line);
    switch_row(display, "Highlight Changes", "Colour text typed since the file was opened",
               &settings.highlight_changes);

    // tabs and indentation
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(indentation), "Indentation");
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(tabs), "Tab Width");
    adw_spin_row_set_value(ADW_SPIN_ROW(tabs), settings.tab_width);
    g_signal_connect(tabs, "notify::value", G_CALLBACK(on_tab_width), 0);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(indentation), tabs);
    switch_row(indentation, "Insert Spaces Instead of Tabs", 0, &settings.insert_spaces);
    switch_row(indentation, "Automatic Indentation", 0, &settings.auto_indent);

    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(fonts));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(display));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(indentation));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(behaviour));
    adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(dialog), ADW_PREFERENCES_PAGE(page));

    adw_dialog_present(dialog, GTK_WIDGET(gtk_application_get_active_window(GTK_APPLICATION(data))));

    pango_font_description_free(font);
    g_object_unref(monospace);
}

// Each window asks about its own unsaved changes as it closes
static void action_quit(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    GList *windows = gtk_application_get_windows(GTK_APPLICATION(data));

    (void)action;
    (void)parameter;

    quitting = TRUE;

    for (GList *w = windows; w; w = w->next)
        gtk_window_close(GTK_WINDOW(w->data));

    // a window kept open by Cancel: quitting stopped
    quitting = FALSE;
}

static const GActionEntry app_actions[] = {
    {"style", 0, "s", "'follow'", action_style, {0}},
    {"preferences", action_preferences, 0, 0, 0, {0}},
    {"about", action_about, 0, 0, 0, {0}},
    {"quit", action_quit, 0, 0, 0, {0}},
};

static void on_startup(GApplication *app, gpointer data)
{
    static const struct
    {
        const char *action;
        const char *keys[3];
    } accels[] = {
        {"win.new-window", {"<Ctrl>n", 0}},
        {"win.open", {"<Ctrl>o", 0}},
        {"win.save", {"<Ctrl>s", 0}},
        {"win.save-as", {"<Ctrl><Shift>s", 0}},
        {"win.find", {"<Ctrl>f", 0}},
        {"win.replace", {"<Ctrl>h", 0}},
        {"win.find-next", {"<Ctrl>g", "F3", 0}},
        {"win.find-previous", {"<Ctrl><Shift>g", "<Shift>F3", 0}},
        {"win.go-to-line", {"<Ctrl>i", 0}},
        {"win.close", {"<Ctrl>w", 0}},
        {"app.preferences", {"<Ctrl>comma", 0}},
        {"app.quit", {"<Ctrl>q", 0}},
    };

    GtkCssProvider *css = gtk_css_provider_new();

    (void)data;

    g_action_map_add_action_entries(G_ACTION_MAP(app), app_actions, G_N_ELEMENTS(app_actions), app);

    // the saved style, and the style switcher's look
    load_settings();
    session_files = g_ptr_array_new_with_free_func(g_free);
    watch_desktop_font();
    g_action_group_change_action_state(G_ACTION_GROUP(app), "style", g_variant_new_string(settings.style));
    apply_style();

    gtk_css_provider_load_from_string(css, style_css);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    for (size_t i = 0; i < G_N_ELEMENTS(accels); i++)
        gtk_application_set_accels_for_action(GTK_APPLICATION(app), accels[i].action, accels[i].keys);
}

//
//	Starting: unsaved changes a crash or a session left come back first, each
//	in a window, then (for a session) the files it had open
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

static int restore(GtkApplication *app, gboolean session_files_too)
{
    static gboolean done;

    char **stores;
    int    count = 0;

    if (done)
        return 0;

    done   = TRUE;
    stores = recovery_stores();

    for (char **store = stores; *store; store++)
    {
        GtkWindow *window = editor_window_new(app, 0);
        Editor    *editor = editor_of(window);
        char      *filename;
        gboolean   session;

        if (!recovery_restore(editor->recovery, *store, &filename, &session))
        {
            gtk_window_destroy(window);
            continue;
        }

        if (filename && *filename)
        {
            editor->file = g_file_new_for_path(filename);
            watch_file(editor);
        }

        update_title(editor);
        update_status(editor);
        gtk_window_present(window);

        if (session)
            show_notice(editor, editor->file ? NOTICE_RESTORED : NOTICE_RESTORED_UNTITLED);
        else
            show_notice(editor, editor->file ? NOTICE_RECOVERED : NOTICE_RECOVERED_UNTITLED);
        g_free(filename);
        count++;
    }

    g_strfreev(stores);

    if (session_files_too && settings.restore_session)
    {
        char **uris = recovery_session_files();

        for (char **uri = uris; *uri; uri++)
        {
            GFile *file = g_file_new_for_uri(*uri);

            if (!window_of(app, file) && g_file_query_exists(file, 0))
            {
                gtk_window_present(editor_window_new(app, file));
                count++;
            }

            g_object_unref(file);
        }

        g_strfreev(uris);
    }

    return count;
}

static void on_activate(GApplication *app, gpointer data)
{
    (void)data;

    if (restore(GTK_APPLICATION(app), TRUE) == 0)
        gtk_window_present(editor_window_new(GTK_APPLICATION(app), 0));
}

// Files to open: lost changes come back too, but not the session's files
static void on_open(GApplication *app, GFile **files, int count, const char *hint, gpointer data)
{
    (void)hint;
    (void)data;

    restore(GTK_APPLICATION(app), FALSE);

    for (int i = 0; i < count; i++)
        present_file(GTK_APPLICATION(app), files[i]);
}

// The session's saved files, for next time
static void on_shutdown(GApplication *app, gpointer data)
{
    (void)app;
    (void)data;

    g_ptr_array_add(session_files, 0);
    recovery_set_session_files(settings.restore_session ? (const char *const *)session_files->pdata : 0);
    g_ptr_array_unref(session_files);
    g_clear_object(&desktop);
}

int main(int argc, char *argv[])
{
    AdwApplication *app;
    int             status;

    // the name recent files are recorded under
    g_set_application_name("Neatpad");

    app = adw_application_new(APP_ID, G_APPLICATION_HANDLES_OPEN);
    g_signal_connect(app, "startup", G_CALLBACK(on_startup), 0);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), 0);
    g_signal_connect(app, "open", G_CALLBACK(on_open), 0);
    g_signal_connect(app, "shutdown", G_CALLBACK(on_shutdown), 0);

    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
