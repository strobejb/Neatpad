// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

//
//	MODULE:		TextPalette.cpp
//
//	PURPOSE:	Reads .palette files (palettes/README.md). Each palette's
//				values are gathered over the ones beneath it, its shared
//				section first and then its style's, and only then resolved, so
//				a value can name a colour any palette gives.
//
#include "TextPalette.h"

#include <map>

namespace
{
typedef std::map<std::string, std::string> Values;

// Each colour's name in a palette, and the TXC_* index it colours
const struct
{
    const char *key;
    int         index;
} COLOURS[] = {
    {"text", TXC_FOREGROUND},
    {"background", TXC_BACKGROUND},
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

// How many names a colour can go through, so two naming each other end
const int MAX_NAMES = 8;

std::string Trim(const std::string &text)
{
    size_t start = text.find_first_not_of(" \t\r");
    size_t end   = text.find_last_not_of(" \t\r");

    return start == std::string::npos ? "" : text.substr(start, end - start + 1);
}

std::vector<std::string> Words(const std::string &text)
{
    std::vector<std::string> words;
    size_t                   at = 0;

    while ((at = text.find_first_not_of(" \t", at)) != std::string::npos)
    {
        size_t end = text.find_first_of(" \t", at);

        words.push_back(text.substr(at, end == std::string::npos ? std::string::npos : end - at));
        at = end;
    }

    return words;
}

//
//	A palette's sections: each line is a [section], key=value, or a comment
//	starting with # or ;
//
std::map<std::string, Values> Sections(const std::string &text)
{
    std::map<std::string, Values> sections;
    std::string                   section;
    size_t                        start = 0;

    while (start < text.size())
    {
        size_t      end  = text.find('\n', start);
        std::string line = Trim(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        size_t      equals;

        start = end == std::string::npos ? text.size() : end + 1;

        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;

        if (line[0] == '[' && line.back() == ']')
        {
            section = Trim(line.substr(1, line.size() - 2));
            continue;
        }

        equals = line.find('=');

        if (equals != std::string::npos)
            sections[section][Trim(line.substr(0, equals))] = Trim(line.substr(equals + 1));
    }

    return sections;
}

// Values over the ones gathered so far
void Lay(Values &values, const Values &over)
{
    for (const auto &value : over)
        values[value.first] = value.second;
}

bool ParseHex(const std::string &word, uint32_t *colour)
{
    unsigned value = 0;

    if (word.size() != 7 || word[0] != '#')
        return false;

    for (size_t i = 1; i < 7; i++)
    {
        char ch = word[i];
        int  digit;

        if (ch >= '0' && ch <= '9')
            digit = ch - '0';
        else if (ch >= 'a' && ch <= 'f')
            digit = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F')
            digit = ch - 'A' + 10;
        else
            return false;

        value = value * 16 + digit;
    }

    *colour = TV_RGB(value >> 16, (value >> 8) & 0xff, value & 0xff);
    return true;
}

// A percentage, such as 30%
bool ParsePercent(const std::string &word, double *amount)
{
    int value = 0;

    if (word.size() < 2 || word.back() != '%')
        return false;

    for (size_t i = 0; i + 1 < word.size(); i++)
    {
        if (word[i] < '0' || word[i] > '9')
            return false;

        value = value * 10 + (word[i] - '0');
    }

    *amount = value / 100.0;
    return true;
}

// Part of the way from one colour to another
uint32_t Mix(uint32_t from, uint32_t to, double amount)
{
    uint32_t mixed = 0;

    for (int shift = 0; shift < 24; shift += 8)
    {
        double a = (from >> shift) & 0xff;
        double b = (to >> shift) & 0xff;

        mixed |= (uint32_t)(a + (b - a) * amount + 0.5) << shift;
    }

    return mixed;
}

//
//	The colours, gathered from every palette, resolved as they're asked for
//
class Resolver
{
  public:
    Resolver(const Values &colours, uint32_t accent)
        : m_colours(colours), m_accent(accent)
    {
    }

    // A colour by its name in a palette: black if no palette gives it
    uint32_t Named(const std::string &key, int depth = 0)
    {
        auto found = m_colours.find(key);
        uint32_t colour = 0;

        if (found != m_colours.end() && depth < MAX_NAMES)
            Value(Words(found->second), &colour, depth + 1);

        return colour;
    }

    //
    //	A value's colour, from its first word or two: #rrggbb, accent or a
    //	colour's name, then perhaps a percentage of it over the background.
    //	Returns how many words it was.
    //
    size_t Value(const std::vector<std::string> &words, uint32_t *colour, int depth = 0)
    {
        double amount;

        if (words.empty() || words[0] == "bold" || words[0] == "italic")
            return 0;

        if (words[0] == "accent")
            *colour = m_accent;
        else if (!ParseHex(words[0], colour))
            *colour = Named(words[0], depth);

        if (words.size() > 1 && ParsePercent(words[1], &amount))
        {
            *colour = Mix(Named("background", depth), *colour, amount);
            return 2;
        }

        return 1;
    }

  private:
    const Values &m_colours;
    uint32_t      m_accent;
};
}

std::string PaletteName(const std::string &text)
{
    std::map<std::string, Values> sections = Sections(text);

    return sections["palette"]["name"];
}

//
//	Each palette over the ones beneath it: its shared colours, then its
//	style's (or, if it has only the other style's, those). A syntax style
//	without a colour keeps the colour beneath it.
//
void ResolvePalette(const std::vector<std::string> &palettes, bool dark, uint32_t accent, TextPalette *palette)
{
    Values colours;
    Values syntax;

    for (const std::string &text : palettes)
    {
        std::map<std::string, Values> sections = Sections(text);
        const char                   *style    = dark ? "dark" : "light";
        Values                        shared   = sections["palette"];

        if (sections.count(style) == 0)
            style = dark ? "light" : "dark";

        shared.erase("name");
        Lay(colours, shared);
        Lay(colours, sections[style]);

        // syntax styles, and the syntax- keys palettes had in a style before
        Values over = sections["syntax"];

        Lay(over, sections[std::string(style) + "-syntax"]);

        for (const auto &value : sections[style])
        {
            if (value.first.compare(0, 7, "syntax-") == 0)
                over[value.first.substr(7)] = value.second;
        }

        for (const auto &value : over)
        {
            std::vector<std::string> words = Words(value.second);
            std::string              below = syntax[value.first];

            // only bold or italic: the colour stays as it was
            if (!words.empty() && (words[0] == "bold" || words[0] == "italic"))
            {
                std::vector<std::string> kept = Words(below);
                std::string              base;

                for (const std::string &word : kept)
                {
                    if (word != "bold" && word != "italic")
                        base += word + " ";
                }

                syntax[value.first] = base + value.second;
            }
            else
            {
                syntax[value.first] = value.second;
            }
        }
    }

    Resolver resolve(colours, accent);

    for (const auto &colour : COLOURS)
        palette->colours[colour.index] = resolve.Named(colour.key);

    palette->caret = resolve.Named("caret");

    for (int i = 0; i < SYNTAX_KINDS; i++)
    {
        std::vector<std::string> words  = Words(syntax[SyntaxKindName((SyntaxKind)i)]);
        SyntaxStyle             &style  = palette->syntax[i];
        size_t                   colour = resolve.Value(words, &style.colour);

        if (colour == 0)
            style.colour = palette->colours[TXC_FOREGROUND];

        style.bold   = false;
        style.italic = false;

        for (size_t w = colour; w < words.size(); w++)
        {
            style.bold   = style.bold || words[w] == "bold";
            style.italic = style.italic || words[w] == "italic";
        }
    }
}
