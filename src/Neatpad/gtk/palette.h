// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

//
//	MODULE:		palette.h
//
//	PURPOSE:	The text's colours: Adwaita's, which follow the style and the
//				accent colour, or a palette's in their place. Palettes are
//				built in, or .palette files in ~/.config/catch22/neatpad/palettes:
//
//					[palette]
//					name=Solarized
//
//					[light]
//					background=#fdf6e3
//					text=#657b83
//
//					[dark]
//					background=#002b36
//					text=#839496
//
//				A palette with one of [light] and [dark] has it for both styles.
//				What it leaves out is Adwaita's. The colours (as CSS writes them):
//
//					text, background, caret, selection-text, selection,
//					inactive-selection-text, inactive-selection, margin,
//					line-number-text, line-number, long-line-text, long-line,
//					current-line-text, current-line, match-text, match,
//					changed-text, changed-line, changed-line-background
//
//				And syntax colouring's, each a colour then bold or italic if it's
//				either, such as syntax-keyword=#9141ac bold:
//
//					syntax-comment, syntax-keyword, syntax-type, syntax-constant,
//					syntax-string, syntax-escape, syntax-number, syntax-preprocessor,
//					syntax-error, syntax-warning, syntax-info, syntax-debug,
//					syntax-heading, syntax-strong, syntax-emphasis, syntax-code,
//					syntax-link
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
