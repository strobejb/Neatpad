// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

//
//	MODULE:		palette.h
//
//	PURPOSE:	The text's colours, from a palette: a .palette file, as
//				src/TextView/palettes/README.md describes. The built-in ones
//				are compiled into the view (NEATPAD_PALETTES), and .palette
//				files in ~/.config/catch22/neatpad/palettes add more, or take a
//				built-in one's place. Every palette is laid over Adwaita's.
//
#ifndef NEATPAD_PALETTE_INCLUDED
#define NEATPAD_PALETTE_INCLUDED

#include "NeatpadTextView.h"

G_BEGIN_DECLS

typedef struct
{
    char *id;   // the file's name without .palette: "solarized"
    char *name; // as it's shown: the [palette] name, or the id
} PaletteInfo;

#define PALETTE_DEFAULT "adwaita"

// The palettes to choose from: Adwaita, then the others by name. A file of the
// user's takes the place of a built-in palette with its id.
GPtrArray *palette_list(void);

// The folder the user's palettes are in: g_free it
char *palette_folder(void);

// The view in a palette's colours, for the style (dark or not) and the accent
// colour (NULL for Adwaita blue). An unknown palette is Adwaita.
void palette_apply(const char *id, NeatpadTextView *view, gboolean dark, const GdkRGBA *accent);

G_END_DECLS

#endif
