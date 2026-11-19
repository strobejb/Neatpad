// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

//
//	MODULE:		SyntaxLexer.cpp
//
//	PURPOSE:	Colours text a line at a time, by a syntax definition's rules.
//				At each place in a line the rules of the context it's in are
//				tried in order, and the first that matches colours its text.
//
#include "SyntaxLexer.h"
#include "../TextDocument/Unicode.h"

#include <type_traits>

namespace
{
// How deep contexts can be entered, so a definition can't enter for ever
const size_t MAX_DEPTH = 16;

uint32_t Unit(tchar_t ch)
{
    return (uint32_t)(std::make_unsigned<tchar_t>::type)ch;
}

bool IsDigit(uint32_t ch)
{
    return ch >= '0' && ch <= '9';
}

// A letter, _ or anything beyond ASCII starts a word; digits go on one
bool IsWordStart(uint32_t ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_' || ch >= 0x80;
}

bool IsWord(uint32_t ch)
{
    return IsWordStart(ch) || IsDigit(ch);
}

// A unit that continues a character, rather than starting one
bool IsContinuation(uint32_t ch)
{
    if (TCHAR_UTF8)
        return (ch & 0xC0) == 0x80;

    return ch >= UNI_SUR_LOW_START && ch <= UNI_SUR_LOW_END;
}

// How many units from at are a word
int WordLength(const tchar_t *text, int at, int length)
{
    int end = at;

    while (end < length && IsWord(Unit(text[end])))
        end++;

    return end - at;
}

// Text from a definition (UTF-8) in the build's units
std::basic_string<tchar_t> FromUtf8(const std::string &text)
{
    std::basic_string<tchar_t> units;
    size_t                     length;

    if (TCHAR_UTF8)
        return std::basic_string<tchar_t>(text.begin(), text.end());

    length = utf8_count_utf16((const UTF8 *)text.data(), text.size());
    units.resize(length);
    utf8_to_utf16((const UTF8 *)text.data(), text.size(), (UTF16 *)&units[0], &length);
    units.resize(length);

    return units;
}

// Which list of rules a unit is in: its own for ASCII, one for the rest
int FirstUnit(uint32_t ch)
{
    return ch < 0x80 ? (int)ch : 0x80;
}

// Whether a rule can match at a character starting with this unit
bool CanStart(SyntaxPattern pattern, uint32_t first, uint32_t ch)
{
    switch (pattern)
    {
    case SyntaxPattern::Text:
        return FirstUnit(first) == FirstUnit(ch);

    case SyntaxPattern::Number:
        return IsDigit(ch) || ch == '.';

    case SyntaxPattern::Word:
    case SyntaxPattern::WordIn:
        return IsWordStart(ch);

    case SyntaxPattern::Space:
        return ch == ' ' || ch == '\t';

    case SyntaxPattern::Escape:
        return ch == '\\';
    }

    return false;
}

void Fill(SyntaxKind *kinds, int count, SyntaxKind kind)
{
    if (kinds == 0)
        return;

    for (int i = 0; i < count; i++)
        kinds[i] = kind;
}
}

//
//	The definition, ready to run: its text in the build's units, and its
//	lists looked up by number rather than name
//
SyntaxLexer::SyntaxLexer(const SyntaxDefinition &definition)
{
    std::map<std::string, int> listNumbers;

    for (const auto &list : definition.lists)
    {
        std::unordered_set<Text> words;

        for (const std::string &word : list.second)
            words.insert(FromUtf8(word));

        listNumbers[list.first] = (int)m_lists.size();
        m_lists.push_back(words);
    }

    for (const SyntaxContext &context : definition.contexts)
    {
        Context ready = { context.kind, context.endOfLine, {} };

        for (const SyntaxRule &rule : context.rules)
        {
            Rule r = { rule.pattern, FromUtf8(rule.text), -1, rule.kind, rule.action, rule.enter };

            if (rule.pattern == SyntaxPattern::WordIn)
                r.list = listNumbers[rule.text];

            ready.rules.push_back(r);
        }

        // for each first unit, the rules that can start there
        for (int ch = 0; ch < FIRST_UNITS; ch++)
        {
            for (size_t i = 0; i < ready.rules.size(); i++)
            {
                const Rule &r     = ready.rules[i];
                uint32_t    first = r.text.empty() ? 0 : Unit(r.text[0]);

                if (CanStart(r.pattern, first, (uint32_t)ch))
                    ready.first[ch].push_back((int)i);
            }
        }

        m_contexts.push_back(ready);
    }

    // the state lines start in: in the first context
    State({ 0 });
}

//
//	A line, from the state the line before ended in
//
int SyntaxLexer::Line(int state, const tchar_t *text, int length, SyntaxKind *kinds)
{
    std::vector<int> stack = m_stacks[state];
    int              at    = 0;

    while (at < length)
    {
        const Context &context = m_contexts[stack.back()];
        const Rule    *rule    = 0;
        int            matched = 0;

        for (int r : context.first[FirstUnit(Unit(text[at]))])
        {
            matched = Match(context.rules[r], text, at, length);

            if (matched > 0)
            {
                rule = &context.rules[r];
                break;
            }
        }

        // where no rule matches, a word is passed over whole, so no rule
        // starts part way through one
        if (rule == 0)
        {
            matched = IsWordStart(Unit(text[at])) ? WordLength(text, at, length) : 1;
            Fill(kinds ? kinds + at : 0, matched, context.kind);
        }
        else
        {
            Fill(kinds ? kinds + at : 0, matched, rule->kind);

            if (rule->action == SyntaxAction::Enter && stack.size() < MAX_DEPTH)
                stack.push_back(rule->enter);

            if (rule->action == SyntaxAction::Leave && stack.size() > 1)
                stack.pop_back();
        }

        at += matched;
    }

    // contexts that last only to the end of the line
    while (stack.size() > 1 && m_contexts[stack.back()].endOfLine)
        stack.pop_back();

    return State(stack);
}

bool SyntaxLexer::Stateless() const
{
    for (const Context &context : m_contexts)
    {
        for (const Rule &rule : context.rules)
        {
            if (rule.action == SyntaxAction::Enter && !m_contexts[rule.enter].endOfLine)
                return false;
        }
    }

    return true;
}

//
//	How many units a rule matches at, or 0 if it doesn't
//
int SyntaxLexer::Match(const Rule &rule, const tchar_t *text, int at, int length)
{
    uint32_t ch   = Unit(text[at]);
    int      left = length - at;
    int      end  = at;

    switch (rule.pattern)
    {
    case SyntaxPattern::Text:
        if ((int)rule.text.size() <= left && rule.text.compare(0, rule.text.size(), text + at, rule.text.size()) == 0)
            return (int)rule.text.size();

        return 0;

    case SyntaxPattern::Number:
        if (!IsDigit(ch) && !(ch == '.' && left > 1 && IsDigit(Unit(text[at + 1]))))
            return 0;

        while (end < length && (IsWord(Unit(text[end])) || text[end] == '.'))
            end++;

        return end - at;

    case SyntaxPattern::Word:
        return IsWordStart(ch) ? WordLength(text, at, length) : 0;

    case SyntaxPattern::WordIn:
        if (!IsWordStart(ch))
            return 0;

        end = at + WordLength(text, at, length);
        m_word.assign(text + at, end - at);

        return m_lists[rule.list].count(m_word) ? end - at : 0;

    case SyntaxPattern::Space:
        while (end < length && (text[end] == ' ' || text[end] == '\t'))
            end++;

        return end - at;

    case SyntaxPattern::Escape:
        if (ch != '\\' || left < 2)
            return 0;

        // the backslash, and the whole character after it
        end = at + 2;

        while (end < length && IsContinuation(Unit(text[end])))
            end++;

        return end - at;
    }

    return 0;
}

//
//	The state a stack of contexts is: a number for each stack met so far
//
int SyntaxLexer::State(const std::vector<int> &stack)
{
    auto found = m_states.find(stack);

    if (found != m_states.end())
        return found->second;

    m_stacks.push_back(stack);
    m_states[stack] = (int)m_stacks.size() - 1;

    return (int)m_stacks.size() - 1;
}
