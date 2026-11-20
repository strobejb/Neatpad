// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

//
//	MODULE:		palette.c
//
//	PURPOSE:	The text's colours, from a palette: see palette.h
//
#include <string.h>

#include "palette.h"

char *palette_folder(void)
{
    return g_build_filename(g_get_user_config_dir(), "catch22", "neatpad", "palettes", NULL);
}

// A palette's text, the user's file before a built-in one: NULL if there's neither
static char *load_palette(const char *id)
{
    char   *folder = palette_folder();
    char   *name   = g_strconcat(id, ".palette", NULL);
    char   *path   = g_build_filename(folder, name, NULL);
    char   *text   = NULL;
    GBytes *bytes;

    if (!g_file_get_contents(path, &text, NULL, NULL))
    {
        char *resource = g_strconcat(NEATPAD_PALETTES "/", name, NULL);

        bytes = g_resources_lookup_data(resource, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);

        if (bytes)
        {
            text = g_strndup(g_bytes_get_data(bytes, NULL), g_bytes_get_size(bytes));
            g_bytes_unref(bytes);
        }

        g_free(resource);
    }

    g_free(path);
    g_free(name);
    g_free(folder);
    return text;
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
    GKeyFile    *file = g_key_file_new();
    char        *text = NULL;
    char        *name = NULL;
    PaletteInfo *info;

    // listed once, as the user's file if there's one
    for (guint i = 0; i < list->len; i++)
    {
        if (g_str_equal(((PaletteInfo *)list->pdata[i])->id, id))
        {
            g_key_file_free(file);
            return;
        }
    }

    text = load_palette(id);

    if (text && g_key_file_load_from_data(file, text, (gsize)-1, G_KEY_FILE_NONE, NULL))
        name = g_key_file_get_string(file, "palette", "name", NULL);

    if (text)
    {
        info       = g_new0(PaletteInfo, 1);
        info->id   = g_strdup(id);
        info->name = name && *name ? g_strdup(name) : g_strdup(id);
        g_ptr_array_add(list, info);
    }

    g_free(name);
    g_free(text);
    g_key_file_free(file);
}

// Each .palette file of a list of names, by its id
static void add_palettes(GPtrArray *list, char **names)
{
    for (char **name = names; name && *name; name++)
    {
        if (g_str_has_suffix(*name, ".palette"))
        {
            char *id = g_strndup(*name, strlen(*name) - strlen(".palette"));

            add_palette(list, id);
            g_free(id);
        }
    }
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
    GPtrArray  *list     = g_ptr_array_new_with_free_func(palette_info_free);
    char      **builtins = g_resources_enumerate_children(NEATPAD_PALETTES, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
    char       *folder   = palette_folder();
    GDir       *dir      = g_dir_open(folder, 0, 0);
    const char *entry;

    add_palettes(list, builtins);

    while (dir && (entry = g_dir_read_name(dir)) != 0)
    {
        char *names[] = {(char *)entry, NULL};

        add_palettes(list, names);
    }

    if (dir)
        g_dir_close(dir);

    g_ptr_array_sort(list, by_name);

    g_strfreev(builtins);
    g_free(folder);
    return list;
}

void palette_apply(const char *id, NeatpadTextView *view, gboolean dark, const GdkRGBA *accent)
{
    char *text = id ? load_palette(id) : NULL;

    neatpad_text_view_set_palette(view, text, dark, accent);
    g_free(text);
}
