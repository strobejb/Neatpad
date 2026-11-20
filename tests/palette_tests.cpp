// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

//
//	Palette tests: reading .palette files (src/TextView/palettes/README.md),
//	laying them over one another, and resolving them for a style and accent
//
#include "tests.h"
#include "../src/TextView/TextPalette.h"

#include <fstream>
#include <sstream>

namespace
{
const uint32_t BLUE = TV_RGB(0x35, 0x84, 0xe4);

// A palette beneath the others, as Adwaita is
const char *const BASE = "[palette]\n"
                         "name=Base\n"
                         "selection=accent 30%\n"
                         "inactive-selection=text 10%\n"
                         "long-line=current-line\n"
                         "\n"
                         "[light]\n"
                         "text=#504e55\n"
                         "background=#ffffff\n"
                         "current-line=#f6f5f4\n"
                         "\n"
                         "[dark]\n"
                         "text=#c0bfbc\n"
                         "background=#1d1d20\n"
                         "\n"
                         "[syntax]\n"
                         "strong=text bold\n"
                         "\n"
                         "[light-syntax]\n"
                         "comment=#77767b\n"
                         "keyword=#9141ac bold\n";

TextPalette resolve(std::vector<std::string> palettes, bool dark = false, uint32_t accent = BLUE)
{
    TextPalette palette;

    ResolvePalette(palettes, dark, accent, &palette);
    return palette;
}

void palette_reads_its_name()
{
    CHECK(PaletteName(BASE) == "Base");
    CHECK(PaletteName("# no name\n[light]\ntext=#000000\n") == "");
}

void palette_resolves_colours_for_the_style()
{
    TextPalette light = resolve({ BASE });
    TextPalette dark  = resolve({ BASE }, true);

    CHECK(light.colours[TXC_FOREGROUND] == TV_RGB(0x50, 0x4e, 0x55));
    CHECK(light.colours[TXC_BACKGROUND] == TV_RGB(0xff, 0xff, 0xff));
    CHECK(dark.colours[TXC_BACKGROUND] == TV_RGB(0x1d, 0x1d, 0x20));

    // a colour can name another, from either section
    CHECK(light.colours[TXC_LONGLINE] == TV_RGB(0xf6, 0xf5, 0xf4));

    // a percentage of a colour over the background, the accent's or another's
    CHECK(light.colours[TXC_HIGHLIGHT] == TV_RGB(194, 218, 247));
    CHECK(resolve({ BASE }, false, TV_RGB(0xff, 0, 0)).colours[TXC_HIGHLIGHT] == TV_RGB(255, 179, 179));
    CHECK(light.colours[TXC_HIGHLIGHT2] == TV_RGB(238, 237, 238));
}

void palette_lies_over_the_ones_beneath()
{
    // a palette with one style has it for both; what it leaves out is the base's
    const char *one = "[palette]\nname=One\n[light]\nbackground=#002b36\n";

    TextPalette light = resolve({ BASE, one });
    TextPalette dark  = resolve({ BASE, one }, true);

    CHECK(light.colours[TXC_BACKGROUND] == TV_RGB(0x00, 0x2b, 0x36));
    CHECK(dark.colours[TXC_BACKGROUND] == TV_RGB(0x00, 0x2b, 0x36));
    CHECK(light.colours[TXC_FOREGROUND] == TV_RGB(0x50, 0x4e, 0x55));
    CHECK(dark.colours[TXC_FOREGROUND] == TV_RGB(0xc0, 0xbf, 0xbc));

    // the base's selection is over this palette's background
    CHECK(light.colours[TXC_HIGHLIGHT] == TV_RGB(16, 70, 106));
}

void palette_resolves_syntax_styles()
{
    const char *over = "[syntax]\n"
                       "comment=italic\n"
                       "[light]\n"
                       "syntax-type=#2b91af\n";

    TextPalette base    = resolve({ BASE });
    TextPalette palette = resolve({ BASE, over });

    CHECK(base.syntax[(int)SyntaxKind::Keyword].colour == TV_RGB(0x91, 0x41, 0xac));
    CHECK(base.syntax[(int)SyntaxKind::Keyword].bold);
    CHECK(!base.syntax[(int)SyntaxKind::Comment].italic);

    // a colour's name, and a kind no palette gives is the text's colour
    CHECK(base.syntax[(int)SyntaxKind::Strong].colour == TV_RGB(0x50, 0x4e, 0x55));
    CHECK(base.syntax[(int)SyntaxKind::Strong].bold);
    CHECK(base.syntax[(int)SyntaxKind::Number].colour == TV_RGB(0x50, 0x4e, 0x55));
    CHECK(!base.syntax[(int)SyntaxKind::Number].bold);

    // only a style keeps the colour beneath it; a syntax- key in a style still reads
    CHECK(palette.syntax[(int)SyntaxKind::Comment].colour == TV_RGB(0x77, 0x76, 0x7b));
    CHECK(palette.syntax[(int)SyntaxKind::Comment].italic);
    CHECK(palette.syntax[(int)SyntaxKind::Type].colour == TV_RGB(0x2b, 0x91, 0xaf));
}

void palette_ends_colours_naming_each_other()
{
    TextPalette palette = resolve({ "[light]\ntext=background\nbackground=text\n" });

    CHECK(palette.colours[TXC_FOREGROUND] == 0);
}

void palette_reads_the_palettes_neatpad_has()
{
#ifdef NEATPAD_PALETTE_DIR
    std::string texts[3];
    const char *names[] = { "adwaita", "classic", "solarized" };

    for (int i = 0; i < 3; i++)
    {
        std::ifstream     file(std::string(NEATPAD_PALETTE_DIR) + "/" + names[i] + ".palette", std::ios::binary);
        std::stringstream text;

        CHECK(file.good());
        text << file.rdbuf();
        texts[i] = text.str();
    }

    CHECK(PaletteName(texts[0]) == "Adwaita");

    // Adwaita's selection is the accent 30% over its background, as libadwaita's
    TextPalette adwaita = resolve({ texts[0] });

    CHECK(adwaita.colours[TXC_HIGHLIGHT] == TV_RGB(194, 218, 247));
    CHECK(adwaita.caret == TV_RGB(0x77, 0x76, 0x7b));

    // Solarized over Adwaita, dark
    TextPalette solarized = resolve({ texts[0], texts[2] }, true);

    CHECK(PaletteName(texts[2]) == "Solarized");
    CHECK(solarized.colours[TXC_BACKGROUND] == TV_RGB(0x00, 0x2b, 0x36));
    CHECK(solarized.colours[TXC_HIGHLIGHTTEXT] == TV_RGB(0x93, 0xa1, 0xa1));
    CHECK(solarized.syntax[(int)SyntaxKind::Comment].colour == TV_RGB(0x58, 0x6e, 0x75));
    CHECK(solarized.syntax[(int)SyntaxKind::Comment].italic);
    CHECK(solarized.syntax[(int)SyntaxKind::Keyword].bold);

    // Classic has one style, for both
    TextPalette classic = resolve({ texts[0], texts[1] }, true);

    CHECK(classic.colours[TXC_BACKGROUND] == TV_RGB(0xff, 0xff, 0xff));
    CHECK(classic.syntax[(int)SyntaxKind::Comment].colour == TV_RGB(0, 0x80, 0));
#endif
}

const test_case tests[] =
{
    { "palette_reads_its_name", palette_reads_its_name },
    { "palette_resolves_colours_for_the_style", palette_resolves_colours_for_the_style },
    { "palette_lies_over_the_ones_beneath", palette_lies_over_the_ones_beneath },
    { "palette_resolves_syntax_styles", palette_resolves_syntax_styles },
    { "palette_ends_colours_naming_each_other", palette_ends_colours_naming_each_other },
    { "palette_reads_the_palettes_neatpad_has", palette_reads_the_palettes_neatpad_has },
};
}

const test_group palette_tests = { "palette", tests, sizeof(tests) / sizeof(tests[0]) };
