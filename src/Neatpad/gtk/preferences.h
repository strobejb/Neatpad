// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

//
//	MODULE:		preferences.h
//
//	PURPOSE:	Settings, for every window, kept in
//				~/.config/catch22/neatpad/settings.ini, and the Preferences
//				dialog that changes them
//
#ifndef NEATPAD_PREFERENCES_INCLUDED
#define NEATPAD_PREFERENCES_INCLUDED

#include <adwaita.h>

G_BEGIN_DECLS

typedef struct
{
    char    *style;   // "follow", "light" or "dark"
    char    *palette; // the text's colours: a palette's id (palette.h)
    gboolean system_font;
    char    *font;
    gboolean restore_session;
    gboolean status_bar;
    gboolean line_numbers;
    int      line_spacing;   // extra pixels between lines
    int      filter_context; // lines shown round each filtered match
    gboolean current_line;
    gboolean highlight_changes;
    gboolean change_markers;    // outlines and deletion marks, in a margin of their own
    gboolean long_lines;        // lines beyond long_line_column highlighted
    int      long_line_column;
    gboolean show_line_endings; // every line ending, not just mismatched ones
    int      tab_width;
    gboolean insert_spaces;
    gboolean auto_indent;
} Settings;

extern Settings settings;

void load_settings(void);
void save_settings(void);

// Preferences, over parent: each row changes a setting and saves it, then
// calls changed, to apply the settings to every window. Restore Defaults puts
// back every setting it shows.
void preferences_show(GtkWindow *parent, void (*changed)(void));

G_END_DECLS

#endif
