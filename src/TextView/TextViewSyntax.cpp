// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

//
//	MODULE:		TextViewSyntax.cpp
//
//	PURPOSE:	Syntax colouring: choosing a language's rules for a file, and
//				colouring each line as it's laid out, from the state the line
//				before it ended in.
//
#include "TextViewInternal.h"

namespace
{
// In a longer document every line starts afresh, rather than colouring the
// lines before it to find its state: a comment over many lines can be wrong,
// but an edit stays quick however large the file is. An edit forgets the
// states, and the lexer colours about 50 MB a second, so up to this size the
// lines down to the window take 20ms at most.
const size_w SYNTAX_STATE_LIMIT = 1024 * 1024;

// How many lines apart states are kept, colouring the lines before a line
const int SYNTAX_STATE_EVERY = 64;

char LowerAscii(char ch)
{
    return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
}

// Whether a name matches a pattern of * and ?, regardless of ASCII case
bool MatchesPattern(const char *pattern, const char *name)
{
    if (*pattern == 0)
        return *name == 0;

    if (*pattern == '*')
        return MatchesPattern(pattern + 1, name) || (*name && MatchesPattern(pattern, name + 1));

    if (*name == 0)
        return false;

    if (*pattern != '?' && LowerAscii(*pattern) != LowerAscii(*name))
        return false;

    return MatchesPattern(pattern + 1, name + 1);
}

// A path's file name, without its folder
const char *FileName(const char *path)
{
    const char *name = path;

    for (const char *ch = path; *ch; ch++)
    {
        if (*ch == '/' || *ch == '\\')
            name = ch + 1;
    }

    return name;
}
}

//
//	The first definition whose files match the file's name
//
int TextView::ChooseSyntax(const char *const *definitions, int count, const char *filename)
{
    const char *name = FileName(filename ? filename : "");

    for (int i = 0; i < count; i++)
    {
        SyntaxDefinition definition;

        if (!ParseSyntax(definitions[i], &definition, 0))
            continue;

        for (const std::string &pattern : definition.files)
        {
            if (MatchesPattern(pattern.c_str(), name))
            {
                UseSyntax(&definition);
                return i;
            }
        }
    }

    UseSyntax(0);
    return -1;
}

bool TextView::SetSyntax(const char *definition)
{
    SyntaxDefinition read;

    if (definition == 0)
    {
        UseSyntax(0);
        return true;
    }

    if (!ParseSyntax(definition, &read, 0))
        return false;

    UseSyntax(&read);
    return true;
}

void TextView::SetSyntaxStyle(SyntaxKind kind, uint32_t rgbColour, bool bold, bool italic)
{
    m_syntaxStyles[(int)kind] = {rgbColour, bold, italic};

    ResetLineCache();
    RefreshWindow();
}

void TextView::SetPalette(const TextPalette &palette)
{
    for (int i = 0; i < TXC_MAX_COLOURS; i++)
        m_rgbColourList[i] = palette.colours[i];

    for (int i = 0; i < SYNTAX_KINDS; i++)
        m_syntaxStyles[i] = palette.syntax[i];

    ResetLineCache();
    RefreshWindow();
}

void TextView::UseSyntax(const SyntaxDefinition *definition)
{
    m_pSyntax.reset(definition ? new SyntaxLexer(*definition) : 0);

    ResetLineCache();
    RefreshWindow();
}

//
//	Colour a line's text, over the base coat, by the kind of each unit: its
//	colour, and bold or italic
//
void TextView::SyntaxColour(TextCoord *line, tchar_t *szText, int nTextLen, TextRun *attr)
{
    std::vector<SyntaxKind> kinds(nTextLen);
    int                     state = SyntaxState(line);

    state = m_pSyntax->Line(state, szText, nTextLen, kinds.data());

    // the next line starts where this one ended
    m_syntaxStates[line->line_next.byte_offset] = state;

    for (int i = 0; i < nTextLen; i++)
    {
        const SyntaxStyle &style = m_syntaxStyles[(int)kinds[i]];

        if (kinds[i] == SyntaxKind::Plain)
            continue;

        attr[i].fg     = ResolveColour(style.colour);
        attr[i].bold   = style.bold;
        attr[i].italic = style.italic;
    }
}

//
//	The state a line starts in: from the nearest line before it whose state is
//	known (or the first line), colouring the lines between, a state kept every
//	so often on the way
//
int TextView::SyntaxState(const TextCoord *line)
{
    std::vector<tchar_t> text(LINE_LAYOUT_LIMIT);
    size_w               from  = 0;
    int                  state = SyntaxLexer::START;
    int                  count = 0;

    if (m_pSyntax->Stateless() || m_pTextDoc->byte_length() > SYNTAX_STATE_LIMIT)
        return SyntaxLexer::START;

    auto known = m_syntaxStates.upper_bound(line->line_begin.byte_offset);

    if (known != m_syntaxStates.begin())
    {
        --known;
        from  = known->first;
        state = known->second;
    }

    LineIterator lines = m_pTextDoc->line_at(TextPos(from));

    while (lines.line().line_begin.byte_offset < line->line_begin.byte_offset)
    {
        uint32_t length = lines.text(text.data(), (uint32_t)text.size());

        state = m_pSyntax->Line(state, text.data(), (int)length, 0);

        if (!lines.next())
            break;

        if (++count % SYNTAX_STATE_EVERY == 0)
            m_syntaxStates[lines.line().line_begin.byte_offset] = state;
    }

    return state;
}
