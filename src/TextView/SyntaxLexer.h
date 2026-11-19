// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

//
//	MODULE:		SyntaxLexer.h
//
//	PURPOSE:	Colours text a line at a time, by a syntax definition's rules
//				(syntax/README.md). A line starts in the state the line before
//				ended in, which is the stack of contexts it's in, as a number.
//
#ifndef SYNTAXLEXER_INCLUDED
#define SYNTAXLEXER_INCLUDED

#include <map>
#include <string>
#include <unordered_set>
#include <vector>

#include "../TextDocument/TextDocument.h"
#include "Syntax.h"

class SyntaxLexer
{
  public:
    explicit SyntaxLexer(const SyntaxDefinition &definition);

    // The state the first line starts in
    static const int START = 0;

    // Colours a line, starting in the state the line before ended in: a kind
    // for each of its units. Returns the state the line ends in.
    int Line(int state, const tchar_t *text, int length, SyntaxKind *kinds);

  private:
    typedef std::basic_string<tchar_t> Text;

    // A rule, with its text in the build's units
    struct Rule
    {
        SyntaxPattern pattern;
        Text          text;
        int           list; // the list a WordIn rule looks in
        SyntaxKind    kind;
        SyntaxAction  action;
        int           enter;
    };

    struct Context
    {
        SyntaxKind        kind;
        bool              endOfLine;
        std::vector<Rule> rules;
    };

    int Match(const Rule &rule, const tchar_t *text, int at, int length) const;
    int State(const std::vector<int> &stack);

    std::vector<Context>                  m_contexts;
    std::vector<std::unordered_set<Text>> m_lists;
    std::vector<std::vector<int>>         m_stacks; // the stack of contexts each state is
    std::map<std::vector<int>, int>       m_states; // and the state each stack is
};

#endif
