// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

//
//	MODULE:		preferences.c
//
//	PURPOSE:	Settings, kept in ~/.config/catch22/neatpad/settings.ini, and
//				the Preferences dialog that changes them: see preferences.h
//
#include <string.h>

#include "NeatpadTextView.h"
#include "palette.h"
#include "preferences.h"

Settings settings;

// What applies the settings to every window, once one has changed
static void (*settings_changed)(void);

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

// The settings a file has, and the defaults for what it hasn't
static void read_settings(GKeyFile *file)
{
    g_free(settings.style);
    g_free(settings.palette);
    g_free(settings.font);

    settings.style             = read_string(file, "style", "follow");
    settings.palette           = read_string(file, "palette", PALETTE_DEFAULT);
    settings.system_font       = read_boolean(file, "use-system-font", TRUE);
    settings.font              = read_string(file, "font", "Monospace 11");
    settings.restore_session   = read_boolean(file, "restore-session", TRUE);
    settings.status_bar        = read_boolean(file, "status-bar", TRUE);
    settings.line_numbers      = read_boolean(file, "line-numbers", FALSE);
    settings.filter_context    = g_key_file_has_key(file, "editor", "filter-context", 0)
                                     ? CLAMP(g_key_file_get_integer(file, "editor", "filter-context", 0), 0,
                                             TV_MAX_FILTER_CONTEXT)
                                     : 2;
    settings.line_spacing      = g_key_file_has_key(file, "editor", "line-spacing", 0)
                                     ? CLAMP(g_key_file_get_integer(file, "editor", "line-spacing", 0), 0, 40)
                                     : 0;
    settings.current_line      = read_boolean(file, "highlight-current-line", FALSE);
    settings.highlight_changes = read_boolean(file, "highlight-changes", TRUE);
    settings.change_markers    = read_boolean(file, "change-markers", TRUE);
    settings.long_lines        = read_boolean(file, "long-lines", TRUE);
    settings.show_line_endings = read_boolean(file, "show-line-endings", FALSE);
    settings.long_line_column  = g_key_file_has_key(file, "editor", "long-line-column", 0)
                                     ? CLAMP(g_key_file_get_integer(file, "editor", "long-line-column", 0), 1, 999)
                                     : 80;
    settings.insert_spaces     = read_boolean(file, "insert-spaces", FALSE);
    settings.auto_indent       = read_boolean(file, "auto-indent", FALSE);
    settings.tab_width         = g_key_file_has_key(file, "editor", "tab-width", 0)
                                     ? CLAMP(g_key_file_get_integer(file, "editor", "tab-width", 0), 1, 16)
                                     : 4;
}

void load_settings(void)
{
    GKeyFile *file = g_key_file_new();
    char     *path = settings_path();

    g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, 0);
    read_settings(file);

    g_free(path);
    g_key_file_free(file);
}

void save_settings(void)
{
    GKeyFile *file   = g_key_file_new();
    char     *path   = settings_path();
    char     *folder = g_path_get_dirname(path);

    g_key_file_set_string(file, "editor", "style", settings.style);
    g_key_file_set_string(file, "editor", "palette", settings.palette);
    g_key_file_set_boolean(file, "editor", "use-system-font", settings.system_font);
    g_key_file_set_string(file, "editor", "font", settings.font);
    g_key_file_set_boolean(file, "editor", "restore-session", settings.restore_session);
    g_key_file_set_boolean(file, "editor", "status-bar", settings.status_bar);
    g_key_file_set_boolean(file, "editor", "line-numbers", settings.line_numbers);
    g_key_file_set_integer(file, "editor", "line-spacing", settings.line_spacing);
    g_key_file_set_integer(file, "editor", "filter-context", settings.filter_context);
    g_key_file_set_boolean(file, "editor", "highlight-current-line", settings.current_line);
    g_key_file_set_boolean(file, "editor", "highlight-changes", settings.highlight_changes);
    g_key_file_set_boolean(file, "editor", "change-markers", settings.change_markers);
    g_key_file_set_boolean(file, "editor", "long-lines", settings.long_lines);
    g_key_file_set_integer(file, "editor", "long-line-column", settings.long_line_column);
    g_key_file_set_boolean(file, "editor", "show-line-endings", settings.show_line_endings);
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

//
//	Preferences: each row changes a setting, saves it, and applies it to every
//	window
//
static void on_switch_row(GObject *row, GParamSpec *spec, gpointer setting)
{
    (void)spec;
    *(gboolean *)setting = adw_switch_row_get_active(ADW_SWITCH_ROW(row));
    save_settings();
    settings_changed();
}

static void on_spin_row(GObject *row, GParamSpec *spec, gpointer setting)
{
    (void)spec;
    *(int *)setting = (int)adw_spin_row_get_value(ADW_SPIN_ROW(row));
    save_settings();
    settings_changed();
}

// A palette chosen: every window's text in its colours
static void on_palette(GObject *row, GParamSpec *spec, gpointer data)
{
    GPtrArray *palettes = g_object_get_data(row, "palettes");
    guint      selected = adw_combo_row_get_selected(ADW_COMBO_ROW(row));

    (void)spec;
    (void)data;

    if (selected >= palettes->len)
        return;

    g_free(settings.palette);
    settings.palette = g_strdup(((PaletteInfo *)palettes->pdata[selected])->id);
    save_settings();
    settings_changed();
}

// The palettes to choose from, and where the user's own go
static GtkWidget *palette_row(void)
{
    GtkWidget     *row      = adw_combo_row_new();
    GPtrArray     *palettes = palette_list();
    GtkStringList *names    = gtk_string_list_new(0);
    char          *folder   = palette_folder();
    char          *home     = g_strconcat(g_get_home_dir(), "/", NULL);
    char          *subtitle = g_strdup_printf("Add your own as .palette files in %s%s",
                                              g_str_has_prefix(folder, home) ? "~/" : "",
                                              g_str_has_prefix(folder, home) ? folder + strlen(home) : folder);
    guint          selected = 0;

    for (guint i = 0; i < palettes->len; i++)
    {
        PaletteInfo *info = palettes->pdata[i];

        gtk_string_list_append(names, info->name);

        if (g_str_equal(info->id, settings.palette))
            selected = i;
    }

    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), "Palette");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
    adw_combo_row_set_model(ADW_COMBO_ROW(row), G_LIST_MODEL(names));
    adw_combo_row_set_selected(ADW_COMBO_ROW(row), selected);
    g_object_set_data_full(G_OBJECT(row), "palettes", palettes, (GDestroyNotify)g_ptr_array_unref);
    g_signal_connect(row, "notify::selected", G_CALLBACK(on_palette), 0);

    g_object_unref(names);
    g_free(subtitle);
    g_free(home);
    g_free(folder);
    return row;
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
    settings_changed();
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

static GtkWidget *editor_page(void);

// Restore Defaults: every setting Preferences shows back as it was at first,
// and the page shown again with them
static void on_restore_response(AdwAlertDialog *alert, const char *response, gpointer data)
{
    AdwPreferencesDialog *dialog  = data;
    GtkWidget            *page    = g_object_get_data(G_OBJECT(dialog), "page");
    GKeyFile             *none    = g_key_file_new();
    char                 *style   = g_strdup(settings.style);
    gboolean              endings = settings.show_line_endings;

    (void)alert;

    if (g_str_equal(response, "restore"))
    {
        // the style and the line endings shown are the main menu's
        read_settings(none);
        g_free(settings.style);
        settings.style             = g_steal_pointer(&style);
        settings.show_line_endings = endings;
        save_settings();
        settings_changed();

        adw_preferences_dialog_remove(dialog, ADW_PREFERENCES_PAGE(page));
        page = editor_page();
        g_object_set_data(G_OBJECT(dialog), "page", page);
        adw_preferences_dialog_add(dialog, ADW_PREFERENCES_PAGE(page));
    }

    g_free(style);
    g_key_file_free(none);
}

static void on_restore_clicked(GtkButton *button, gpointer data)
{
    GtkWidget *dialog = gtk_widget_get_ancestor(GTK_WIDGET(button), ADW_TYPE_PREFERENCES_DIALOG);
    AdwDialog *alert  = adw_alert_dialog_new("Restore Default Settings?",
                                            "Every preference goes back to its default.");

    (void)data;

    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(alert), "cancel", "_Cancel", "restore", "_Restore Defaults", NULL);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(alert), "restore", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(alert), "cancel");
    g_signal_connect(alert, "response", G_CALLBACK(on_restore_response), dialog);
    adw_dialog_present(alert, dialog);
}

static GtkWidget *editor_page(void)
{
    GtkWidget            *page         = adw_preferences_page_new();
    GtkWidget            *appearance   = adw_preferences_group_new();
    GtkWidget            *margin       = adw_preferences_group_new();
    GtkWidget            *highlighting = adw_preferences_group_new();
    GtkWidget            *indentation  = adw_preferences_group_new();
    GtkWidget            *font_row     = adw_action_row_new();
    GtkFontDialog        *chooser      = gtk_font_dialog_new();
    GtkWidget            *font_button  = gtk_font_dialog_button_new(chooser); // the button owns chooser
    GtkFilter            *monospace    = GTK_FILTER(gtk_custom_filter_new(is_monospace, 0, 0));
    PangoFontDescription *font         = pango_font_description_from_string(settings.font);
    GtkWidget            *tabs         = adw_spin_row_new_with_range(1, 16, 1);
    GtkWidget            *long_line    = adw_spin_row_new_with_range(1, 999, 1);
    GtkWidget            *spacing      = adw_spin_row_new_with_range(0, 40, 1);
    GtkWidget            *filtering    = adw_preferences_group_new();
    GtkWidget            *context      = adw_spin_row_new_with_range(0, TV_MAX_FILTER_CONTEXT, 1);
    GtkWidget            *behaviour    = adw_preferences_group_new();
    GtkWidget            *defaults     = adw_preferences_group_new();
    GtkWidget            *restore      = gtk_button_new_with_mnemonic("_Restore Defaults");
    GtkWidget            *system_font;
    GtkWidget            *long_lines;

    adw_preferences_page_set_title(ADW_PREFERENCES_PAGE(page), "Editor");
    adw_preferences_page_set_icon_name(ADW_PREFERENCES_PAGE(page), "document-edit-symbolic");

    // the colours, the font, and the window round the text
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(appearance), "Appearance");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(appearance), palette_row());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(font_row), "Editor Font");
    gtk_font_dialog_set_filter(chooser, monospace);
    gtk_font_dialog_button_set_level(GTK_FONT_DIALOG_BUTTON(font_button), GTK_FONT_LEVEL_FONT);
    gtk_font_dialog_button_set_use_font(GTK_FONT_DIALOG_BUTTON(font_button), TRUE);
    gtk_font_dialog_button_set_font_desc(GTK_FONT_DIALOG_BUTTON(font_button), font);
    gtk_widget_set_valign(font_button, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(font_row), font_button);
    g_signal_connect(font_button, "notify::font-desc", G_CALLBACK(on_font), 0);

    // the font is chosen only when it isn't the system's
    system_font = switch_row(appearance, "Use System Font", 0, &settings.system_font);
    g_object_bind_property(system_font, "active", font_row, "sensitive",
                           G_BINDING_SYNC_CREATE | G_BINDING_INVERT_BOOLEAN);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(appearance), font_row);

    // extra pixels between lines, half above and half below, as Win32's
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(spacing), "Extra Line Spacing");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(spacing), "Pixels between lines");
    adw_spin_row_set_value(ADW_SPIN_ROW(spacing), settings.line_spacing);
    g_signal_connect(spacing, "notify::value", G_CALLBACK(on_spin_row), &settings.line_spacing);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(appearance), spacing);
    switch_row(appearance, "Status Bar", 0, &settings.status_bar);

    // left of the text
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(margin), "Margin");
    switch_row(margin, "Line Numbers", 0, &settings.line_numbers);
    switch_row(margin, "Change Indicators", "Mark changed and deleted lines", &settings.change_markers);

    // what's coloured
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(highlighting), "Highlighting");
    switch_row(highlighting, "Current Line", 0, &settings.current_line);
    switch_row(highlighting, "Changed Text", "Colour text typed since the file was opened",
               &settings.highlight_changes);
    long_lines = switch_row(highlighting, "Long Lines", 0, &settings.long_lines);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(long_line), "Long Line Column");
    adw_spin_row_set_value(ADW_SPIN_ROW(long_line), settings.long_line_column);
    g_object_bind_property(long_lines, "active", long_line, "sensitive", G_BINDING_SYNC_CREATE);
    g_signal_connect(long_line, "notify::value", G_CALLBACK(on_spin_row), &settings.long_line_column);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(highlighting), long_line);

    // tabs and indentation
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(indentation), "Indentation");
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(tabs), "Tab Width");
    adw_spin_row_set_value(ADW_SPIN_ROW(tabs), settings.tab_width);
    g_signal_connect(tabs, "notify::value", G_CALLBACK(on_spin_row), &settings.tab_width);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(indentation), tabs);
    switch_row(indentation, "Insert Spaces Instead of Tabs", 0, &settings.insert_spaces);
    switch_row(indentation, "Automatic Indentation", 0, &settings.auto_indent);

    // the find bar's filter
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(filtering), "Filter");
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(context), "Context Lines");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(context), "Lines shown before and after each match");
    adw_spin_row_set_value(ADW_SPIN_ROW(context), settings.filter_context);
    g_signal_connect(context, "notify::value", G_CALLBACK(on_spin_row), &settings.filter_context);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(filtering), context);

    // what's kept between runs
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(behaviour), "Behavior");
    switch_row(behaviour, "Restore Session", "Keep unsaved changes when a window closes, to come back as the file opens",
               &settings.restore_session);

    // last, on its own: every setting back as it was
    gtk_widget_add_css_class(restore, "destructive-action");
    gtk_widget_add_css_class(restore, "pill");
    gtk_widget_set_halign(restore, GTK_ALIGN_CENTER);
    g_signal_connect(restore, "clicked", G_CALLBACK(on_restore_clicked), 0);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(defaults), restore);

    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(appearance));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(margin));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(highlighting));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(indentation));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(filtering));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(behaviour));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), ADW_PREFERENCES_GROUP(defaults));

    pango_font_description_free(font);
    g_object_unref(monospace);
    return page;
}

void preferences_show(GtkWindow *parent, void (*changed)(void))
{
    AdwDialog *dialog = adw_preferences_dialog_new();
    GtkWidget *page   = editor_page();

    settings_changed = changed;

    g_object_set_data(G_OBJECT(dialog), "page", page);
    adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(dialog), ADW_PREFERENCES_PAGE(page));
    adw_dialog_present(dialog, GTK_WIDGET(parent));
}
