//
//	MODULE:		palette.c
//
//	PURPOSE:	The text's colours, from a palette: see palette.h
//
#include <string.h>

#include "palette.h"

// The palettes Neatpad comes with, as their files would be
static const struct
{
    const char *id;
    const char *file;
} builtins[] = {
    {PALETTE_DEFAULT, "[palette]\n"
                      "name=Adwaita\n"},

    // Win32 Neatpad's own colours, as Windows' default theme has them
    {"classic", "[palette]\n"
                "name=Classic\n"
                "[light]\n"
                "text=#000000\n"
                "background=#ffffff\n"
                "caret=#000000\n"
                "selection-text=#ffffff\n"
                "selection=#0078d7\n"
                "inactive-selection-text=#000000\n"
                "inactive-selection=#f0f0f0\n"
                "margin=#f0f0f0\n"
                "line-number-text=#a0a0a0\n"
                "line-number=#f0f0f0\n"
                "long-line-text=#a0a0a0\n"
                "long-line=#f0f0f0\n"
                "current-line-text=#000000\n"
                "current-line=#e6f0ff\n"
                "match-text=#000000\n"
                "match=#ffe680\n"
                "changed-text=#be3737\n"
                "changed-line=#5884a4\n"
                "changed-line-background=#f0f0f0\n"},

    // Ethan Schoonover's Solarized
    {"solarized", "[palette]\n"
                  "name=Solarized\n"
                  "[light]\n"
                  "text=#657b83\n"
                  "background=#fdf6e3\n"
                  "caret=#586e75\n"
                  "selection-text=#586e75\n"
                  "selection=#e4ddc8\n"
                  "inactive-selection-text=#657b83\n"
                  "inactive-selection=#eee8d5\n"
                  "margin=#fdf6e3\n"
                  "line-number-text=#93a1a1\n"
                  "line-number=#fdf6e3\n"
                  "long-line-text=#657b83\n"
                  "long-line=#eee8d5\n"
                  "current-line-text=#657b83\n"
                  "current-line=#eee8d5\n"
                  "match-text=#fdf6e3\n"
                  "match=#b58900\n"
                  "changed-text=#dc322f\n"
                  "changed-line=#268bd2\n"
                  "changed-line-background=#eee8d5\n"
                  "[dark]\n"
                  "text=#839496\n"
                  "background=#002b36\n"
                  "caret=#93a1a1\n"
                  "selection-text=#93a1a1\n"
                  "selection=#0d4655\n"
                  "inactive-selection-text=#839496\n"
                  "inactive-selection=#073642\n"
                  "margin=#002b36\n"
                  "line-number-text=#586e75\n"
                  "line-number=#002b36\n"
                  "long-line-text=#839496\n"
                  "long-line=#073642\n"
                  "current-line-text=#839496\n"
                  "current-line=#073642\n"
                  "match-text=#002b36\n"
                  "match=#b58900\n"
                  "changed-text=#dc322f\n"
                  "changed-line=#268bd2\n"
                  "changed-line-background=#073642\n"},
};

// Each colour's name in a palette, and what it colours
static const struct
{
    const char *key;
    guint       index;
} colours[] = {
    {"text", TXC_FOREGROUND},
    {"background", TXC_BACKGROUND},
    {"caret", NEATPAD_COLOUR_CARET},
    {"selection-text", TXC_HIGHLIGHTTEXT},
    {"selection", TXC_HIGHLIGHT},
    {"inactive-selection-text", TXC_HIGHLIGHTTEXT2},
    {"inactive-selection", TXC_HIGHLIGHT2},
    {"margin", TXC_SELMARGIN1},
    {"margin", TXC_SELMARGIN2},
    {"line-number-text", TXC_LINENUMBERTEXT},
    {"line-number", TXC_LINENUMBER},
    {"long-line-text", TXC_LONGLINETEXT},
    {"long-line", TXC_LONGLINE},
    {"current-line-text", TXC_CURRENTLINETEXT},
    {"current-line", TXC_CURRENTLINE},
    {"match-text", TXC_SEARCHMATCHTEXT},
    {"match", TXC_SEARCHMATCH},
    {"changed-text", TXC_CHANGEDTEXT},
    {"changed-line", TXC_CHANGEDLINE},
    {"changed-line-background", TXC_CHANGEDLINEBG},
};

char *palette_folder(void)
{
    return g_build_filename(g_get_user_config_dir(), "catch22", "neatpad", "palettes", NULL);
}

// A palette's file, the user's before a built-in one: NULL if there's neither
static GKeyFile *load_palette(const char *id)
{
    GKeyFile *file   = g_key_file_new();
    char     *folder = palette_folder();
    char     *name   = g_strconcat(id, ".palette", NULL);
    char     *path   = g_build_filename(folder, name, NULL);
    gboolean  loaded = g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, 0);

    for (size_t i = 0; i < G_N_ELEMENTS(builtins) && !loaded; i++)
    {
        if (g_str_equal(builtins[i].id, id))
            loaded = g_key_file_load_from_data(file, builtins[i].file, (gsize)-1, G_KEY_FILE_NONE, 0);
    }

    g_free(path);
    g_free(name);
    g_free(folder);

    if (!loaded)
    {
        g_key_file_free(file);
        return 0;
    }

    return file;
}

static void palette_info_free(gpointer data)
{
    PaletteInfo *info = data;

    g_free(info->id);
    g_free(info->name);
    g_free(info);
}

static void add_palette(GPtrArray *list, const char *id)
{
    GKeyFile    *file = load_palette(id);
    PaletteInfo *info;
    char        *name;

    if (!file)
        return;

    // listed once, as the user's file if there's one
    for (guint i = 0; i < list->len; i++)
    {
        if (g_str_equal(((PaletteInfo *)list->pdata[i])->id, id))
        {
            g_key_file_free(file);
            return;
        }
    }

    name       = g_key_file_get_string(file, "palette", "name", 0);
    info       = g_new0(PaletteInfo, 1);
    info->id   = g_strdup(id);
    info->name = name && *name ? name : g_strdup(id);

    if (name && !*name)
        g_free(name);

    g_ptr_array_add(list, info);
    g_key_file_free(file);
}

// Adwaita first, then by name
static int by_name(gconstpointer a, gconstpointer b)
{
    const PaletteInfo *x = *(const PaletteInfo *const *)a;
    const PaletteInfo *y = *(const PaletteInfo *const *)b;

    if (g_str_equal(x->id, PALETTE_DEFAULT) != g_str_equal(y->id, PALETTE_DEFAULT))
        return g_str_equal(x->id, PALETTE_DEFAULT) ? -1 : 1;

    return g_utf8_collate(x->name, y->name);
}

GPtrArray *palette_list(void)
{
    GPtrArray  *list   = g_ptr_array_new_with_free_func(palette_info_free);
    char       *folder = palette_folder();
    GDir       *dir    = g_dir_open(folder, 0, 0);
    const char *entry;

    for (size_t i = 0; i < G_N_ELEMENTS(builtins); i++)
        add_palette(list, builtins[i].id);

    while (dir && (entry = g_dir_read_name(dir)) != 0)
    {
        if (g_str_has_suffix(entry, ".palette"))
        {
            char *id = g_strndup(entry, strlen(entry) - strlen(".palette"));

            add_palette(list, id);
            g_free(id);
        }
    }

    if (dir)
        g_dir_close(dir);

    g_ptr_array_sort(list, by_name);

    g_free(folder);
    return list;
}

void palette_apply(const char *id, NeatpadTextView *view, gboolean dark, const GdkRGBA *accent)
{
    GKeyFile   *file = id ? load_palette(id) : 0;
    const char *style;

    neatpad_text_view_set_scheme(view, dark, accent);

    if (!file)
        return;

    // the style's own colours, or the only ones the palette has
    style = dark ? "dark" : "light";

    if (!g_key_file_has_group(file, style))
        style = dark ? "light" : "dark";

    for (size_t i = 0; i < G_N_ELEMENTS(colours); i++)
    {
        char   *value = g_key_file_get_string(file, style, colours[i].key, 0);
        GdkRGBA rgba;

        if (value && gdk_rgba_parse(&rgba, value))
        {
            neatpad_text_view_set_colour(view, colours[i].index,
                                         TV_RGB((int)(rgba.red * 255 + 0.5), (int)(rgba.green * 255 + 0.5),
                                                (int)(rgba.blue * 255 + 0.5)));
        }

        g_free(value);
    }

    g_key_file_free(file);
}
