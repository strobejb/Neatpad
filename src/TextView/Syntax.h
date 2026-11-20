// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

//
//	MODULE:		Syntax.h
//
//	PURPOSE:	A syntax definition, as read from a .syntax file: the language's
//				lists of words, and its contexts, each with its rules in order.
//				The format is described in syntax/README.md.
//
#ifndef SYNTAX_INCLUDED
#define SYNTAX_INCLUDED

#include <map>
#include <set>
#include <stdint.h>
#include <string>
#include <vector>

// What a piece of text is, so the palette can give it a colour
enum class SyntaxKind
{
    Plain,
    Comment,
    Keyword,
    Type,
    Constant,
    String,
    Escape,
    Number,
    Preprocessor,
    Error,
    Warning,
    Info,
    Debug,
    Heading,
    Strong,
    Emphasis,
    Code,
    Link,
};

// How many kinds there are
const int SYNTAX_KINDS = (int)SyntaxKind::Link + 1;

// How a kind of text is drawn: its colour (0x00BBGGRR), and bold or italic
struct SyntaxStyle
{
    uint32_t colour;
    bool     bold;
    bool     italic;
};

// What a rule matches
enum class SyntaxPattern
{
    Text,      // the rule's text, exactly
    Number,    // a number, and the letters and digits after it
    Timestamp, // a date or a time: 2026-11-20, 12:34:56.789
    Word,      // any word
    WordIn,    // a word in the list the rule's text names
    Space,     // spaces and tabs
    Escape,    // a backslash and the character after it
};

// What a rule does after colouring what it matched
enum class SyntaxAction
{
    Stay,  // nothing: the line goes on in the same context
    Enter, // enters the context the rule's target names
    Leave, // goes back to the context it entered from
};

struct SyntaxRule
{
    SyntaxPattern pattern = SyntaxPattern::Text;
    std::string   text;    // the text to match, or the list's name
    SyntaxKind    kind   = SyntaxKind::Plain;
    SyntaxAction  action = SyntaxAction::Stay;
    bool          lineStart = false; // matches only at the start of a line
    std::string   target;  // the context entered
    int           enter = -1; // the context entered, as an index into contexts
    int           line  = 0;  // where it's defined
};

struct SyntaxContext
{
    std::string             name;
    SyntaxKind              kind      = SyntaxKind::Plain; // for what no rule matches
    bool                    endOfLine = false;             // left at the end of the line
    std::vector<SyntaxRule> rules;
};

struct SyntaxDefinition
{
    std::string                                  language;
    std::vector<std::string>                     files;    // file name patterns, such as *.c
    std::map<std::string, std::set<std::string>> lists;    // words, by list name
    std::vector<SyntaxContext>                   contexts; // lines start in the first
};

//
//	Reads a definition from its text (UTF-8). On a mistake it returns false,
//	with the line and what's wrong in error, such as "12: unknown kind 'coment'".
//
bool ParseSyntax(const std::string &text, SyntaxDefinition *definition, std::string *error);

// A kind's name in a definition, such as "comment", and the kind a name is
const char *SyntaxKindName(SyntaxKind kind);
bool        SyntaxKindFromName(const char *name, SyntaxKind *kind);

#endif
