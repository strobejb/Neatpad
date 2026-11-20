// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

//
//	MODULE:		TextPalette.h
//
//	PURPOSE:	The text's colours, read from .palette files (palettes/README.md):
//				palettes are laid one over another, the first underneath, and
//				resolved for the style (light or dark) and the accent colour.
//
#ifndef TEXTPALETTE_INCLUDED
#define TEXTPALETTE_INCLUDED

#include <stdint.h>
#include <string>
#include <vector>

#include "Syntax.h"
#include "TextViewDefs.h"

// A palette resolved: every colour as 0x00BBGGRR, and each kind of text's style
struct TextPalette
{
    uint32_t    colours[TXC_MAX_COLOURS]; // by TXC_* index
    uint32_t    caret;
    SyntaxStyle syntax[SYNTAX_KINDS];
};

// A palette's name, from its [palette] section: "" if it has none
std::string PaletteName(const std::string &text);

// The palettes' colours, the first underneath the rest, for the style (dark or
// not) and the accent colour (0x00BBGGRR)
void ResolvePalette(const std::vector<std::string> &palettes, bool dark, uint32_t accent, TextPalette *palette);

#endif
