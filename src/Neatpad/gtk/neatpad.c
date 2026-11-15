//
//	MODULE:		neatpad.c
//
//	PURPOSE:	Neatpad for GNOME: a window per document, with a NeatpadTextView,
//				open (and recent files) and save, find and replace, go to line,
//				and a status bar
//
#include <adwaita.h>

#include "NeatpadTextView.h"

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
    gboolean         closing;
} Editor;

// What to do once a document's changes are saved or discarded
typedef void (*EditorThen)(Editor *editor);

static GtkWindow *editor_window_new(GtkApplication *app, GFile *file);

static Editor *editor_of(gpointer window)
{
    return g_object_get_data(G_OBJECT(window), "editor");
}

static void editor_free(gpointer data)
{
    Editor *editor = data;

    g_clear_object(&editor->file);
    g_clear_object(&editor->recent);
    g_free(editor->recent_uri);
    g_free(editor);
}

static void toast(Editor *editor, const char *message)
{
    adw_toast_overlay_add_toast(editor->toasts, adw_toast_new(message));
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
    (void)view;
    update_title(data);
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

static gboolean load(Editor *editor, GFile *file)
{
    char    *path = g_file_get_path(file);
    gboolean ok   = path && neatpad_text_view_open_file(editor->view, path);

    if (ok)
    {
        g_set_object(&editor->file, file);
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

static void save_to(Editor *editor, GFile *file, EditorThen then)
{
    char    *path = g_file_get_path(file);
    gboolean ok   = path && neatpad_text_view_save_file(editor->view, path);

    g_free(path);

    if (!ok)
    {
        toast(editor, "Could not save the file");
        return;
    }

    g_set_object(&editor->file, file);
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
//	Closing: once any changes are saved or discarded
//
static void close_now(Editor *editor)
{
    editor->closing = TRUE;
    gtk_window_close(editor->window);
}

static gboolean on_close_request(GtkWindow *window, gpointer data)
{
    Editor *editor = data;

    (void)window;

    if (editor->closing || !neatpad_text_view_get_modified(editor->view))
        return FALSE;

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
};

//
//	The window: a header bar, the view, the find and go-to-line bars, and the
//	status bar
//
static GMenuModel *primary_menu(void)
{
    GMenu *menu    = g_menu_new();
    GMenu *file    = g_menu_new();
    GMenu *search  = g_menu_new();
    GMenu *general = g_menu_new();

    g_menu_append(file, "_New Window", "win.new-window");
    g_menu_append(file, "_Save", "win.save");
    g_menu_append(file, "Save _As\xe2\x80\xa6", "win.save-as");
    g_menu_append(search, "_Find\xe2\x80\xa6", "win.find");
    g_menu_append(search, "_Replace\xe2\x80\xa6", "win.replace");
    g_menu_append(search, "_Go to Line\xe2\x80\xa6", "win.go-to-line");
    g_menu_append(general, "_About Neatpad", "app.about");
    g_menu_append(general, "_Quit", "app.quit");

    g_menu_append_section(menu, 0, G_MENU_MODEL(file));
    g_menu_append_section(menu, 0, G_MENU_MODEL(search));
    g_menu_append_section(menu, 0, G_MENU_MODEL(general));

    g_object_unref(file);
    g_object_unref(search);
    g_object_unref(general);
    return G_MENU_MODEL(menu);
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
    Editor    *editor  = g_new0(Editor, 1);
    GtkWidget *window  = adw_application_window_new(app);
    GtkWidget *toolbar = adw_toolbar_view_new();
    GtkWidget *scroll  = gtk_scrolled_window_new();
    GtkWidget *view    = neatpad_text_view_new();

    editor->window = GTK_WINDOW(window);
    editor->view   = NEATPAD_TEXT_VIEW(view);
    editor->toasts = ADW_TOAST_OVERLAY(adw_toast_overlay_new());
    g_object_set_data_full(G_OBJECT(window), "editor", editor, editor_free);

    g_action_map_add_action_entries(G_ACTION_MAP(window), window_actions, G_N_ELEMENTS(window_actions), editor);

    // GNOME's scrollbars: overlaid, unless Accessibility says always show them
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), view);
    adw_toast_overlay_set_child(editor->toasts, scroll);

    // the find and go-to-line bars are below the text, above the status bar
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header_bar(editor));
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

    if (file)
        load(editor, file);

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
        {"app.quit", {"<Ctrl>q", 0}},
    };

    (void)data;

    g_action_map_add_action_entries(G_ACTION_MAP(app), app_actions, G_N_ELEMENTS(app_actions), app);

    for (size_t i = 0; i < G_N_ELEMENTS(accels); i++)
        gtk_application_set_accels_for_action(GTK_APPLICATION(app), accels[i].action, accels[i].keys);
}

static void on_activate(GApplication *app, gpointer data)
{
    (void)data;
    gtk_window_present(editor_window_new(GTK_APPLICATION(app), 0));
}

static void on_open(GApplication *app, GFile **files, int count, const char *hint, gpointer data)
{
    (void)hint;
    (void)data;

    for (int i = 0; i < count; i++)
        gtk_window_present(editor_window_new(GTK_APPLICATION(app), files[i]));
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

    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
