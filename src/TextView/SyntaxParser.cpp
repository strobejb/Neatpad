// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

//
//	MODULE:		SyntaxParser.cpp
//
//	PURPOSE:	Reads a syntax definition (syntax/README.md). Every line is one
//				statement and its first word says which, so a line is split
//				into words and handed to the function for its statement.
//
#include "Syntax.h"

namespace
{
const char *const MARKER = "# neatpad-syntax-v1";

// The kinds by name, in SyntaxKind's order
const char *const KIND_NAMES[] =
{
    "plain", "comment", "keyword", "type", "constant", "string", "escape",
    "number", "preprocessor", "error", "warning", "info", "debug",
    "heading", "strong", "emphasis", "code", "link",
};

// The patterns that are a name: "word in" and quoted text are the others
struct NamedPattern
{
    const char   *name;
    SyntaxPattern pattern;
};

const NamedPattern PATTERNS[] =
{
    { "number", SyntaxPattern::Number },
    { "word",   SyntaxPattern::Word   },
    { "space",  SyntaxPattern::Space  },
    { "escape", SyntaxPattern::Escape },
};

// A word of a line, and whether it was in quotes
struct Word
{
    std::string text;
    bool        quoted;
};

typedef std::vector<Word> Words;

class Parser
{
  public:
    explicit Parser(SyntaxDefinition *definition)
        : m_def(definition)
    {
    }

    bool Parse(const std::string &text);

    std::string error;

  private:
    bool Split(const std::string &line, Words *words);
    bool Statement(const Words &words);
    bool Language(const Words &words);
    bool Files(const Words &words);
    bool List(const Words &words);
    bool Context(const Words &words);
    bool Rule(const Words &words);
    bool Kind(const Word &word, SyntaxKind *kind);
    bool Resolve();
    bool Fail(int line, const std::string &message);

    SyntaxDefinition *m_def;
    int               m_line = 0;
};

//
//	The definition, a line at a time, then the names its rules refer to
//
bool Parser::Parse(const std::string &text)
{
    size_t start = 0;

    while (start < text.size())
    {
        size_t      end  = text.find('\n', start);
        std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        Words       words;

        start = end == std::string::npos ? text.size() : end + 1;
        m_line++;

        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (m_line == 1 && line != MARKER)
            return Fail(1, std::string("the first line must be '") + MARKER + "'");

        if (!Split(line, &words))
            return false;

        if (!words.empty() && !Statement(words))
            return false;
    }

    if (m_line == 0)
        return Fail(1, std::string("the first line must be '") + MARKER + "'");

    if (m_def->language.empty())
        return Fail(m_line, "there's no 'language' line");

    if (m_def->contexts.empty())
        return Fail(m_line, "there's no context");

    return Resolve();
}

//
//	A line's words, up to a # that starts a word. Quoted text is one word,
//	without its quotes.
//
bool Parser::Split(const std::string &line, Words *words)
{
    size_t at = 0;

    while (at < line.size())
    {
        char ch = line[at];

        if (ch == ' ' || ch == '\t')
        {
            at++;
        }
        else if (ch == '#')
        {
            break;
        }
        else if (ch == '"' || ch == '\'')
        {
            size_t close = line.find(ch, at + 1);

            if (close == std::string::npos)
                return Fail(m_line, "a quote isn't closed");

            if (close == at + 1)
                return Fail(m_line, "an empty quote matches nothing");

            words->push_back({ line.substr(at + 1, close - at - 1), true });
            at = close + 1;
        }
        else
        {
            size_t end = line.find_first_of(" \t", at);

            if (end == std::string::npos)
                end = line.size();

            words->push_back({ line.substr(at, end - at), false });
            at = end;
        }
    }

    return true;
}

//
//	A statement, by its first word: anything else is a rule
//
bool Parser::Statement(const Words &words)
{
    const Word &first = words[0];

    if (!first.quoted && first.text == "language")
        return Language(words);

    if (!first.quoted && first.text == "files")
        return Files(words);

    if (!first.quoted && first.text == "list")
        return List(words);

    if (!first.quoted && first.text == "context")
        return Context(words);

    return Rule(words);
}

//
//	language NAME
//
bool Parser::Language(const Words &words)
{
    if (!m_def->language.empty())
        return Fail(m_line, "the language is already named");

    if (words.size() != 2)
        return Fail(m_line, "'language' takes one name");

    m_def->language = words[1].text;
    return true;
}

//
//	files PATTERN...
//
bool Parser::Files(const Words &words)
{
    if (words.size() < 2)
        return Fail(m_line, "'files' takes one or more patterns");

    for (size_t i = 1; i < words.size(); i++)
        m_def->files.push_back(words[i].text);

    return true;
}

//
//	list NAME WORD...
//
bool Parser::List(const Words &words)
{
    if (words.size() < 3)
        return Fail(m_line, "'list' takes a name and one or more words");

    std::set<std::string> &list = m_def->lists[words[1].text];

    for (size_t i = 2; i < words.size(); i++)
        list.insert(words[i].text);

    return true;
}

//
//	context NAME [KIND] [end-of-line]
//
bool Parser::Context(const Words &words)
{
    SyntaxContext context;
    size_t        i = 2;

    if (words.size() < 2)
        return Fail(m_line, "'context' takes a name");

    context.name = words[1].text;

    for (const SyntaxContext &other : m_def->contexts)
    {
        if (other.name == context.name)
            return Fail(m_line, "there's already a context '" + context.name + "'");
    }

    if (i < words.size() && words[i].text != "end-of-line")
    {
        if (!Kind(words[i], &context.kind))
            return false;

        i++;
    }

    if (i < words.size() && words[i].text == "end-of-line")
    {
        context.endOfLine = true;
        i++;
    }

    if (i < words.size())
        return Fail(m_line, "unexpected '" + words[i].text + "'");

    m_def->contexts.push_back(context);
    return true;
}

//
//	PATTERN [at line-start] KIND [enter NAME | leave]
//
bool Parser::Rule(const Words &words)
{
    SyntaxRule rule;
    size_t     i     = 0;
    bool       named = false;

    if (m_def->contexts.empty())
        return Fail(m_line, "a rule must follow a 'context'");

    rule.line = m_line;

    // what it matches
    if (words[i].quoted)
    {
        rule.pattern = SyntaxPattern::Text;
        rule.text    = words[i].text;
        i++;
    }
    else if (words[i].text == "word" && i + 2 < words.size() && words[i + 1].text == "in")
    {
        rule.pattern = SyntaxPattern::WordIn;
        rule.text    = words[i + 2].text;
        i += 3;
    }
    else
    {
        for (const NamedPattern &p : PATTERNS)
        {
            if (words[i].text == p.name)
            {
                rule.pattern = p.pattern;
                named        = true;
            }
        }

        if (!named)
            return Fail(m_line, "unknown statement or pattern '" + words[i].text + "'");

        i++;
    }

    // where it can match
    if (i < words.size() && !words[i].quoted && words[i].text == "at")
    {
        if (i + 1 == words.size() || words[i + 1].text != "line-start")
            return Fail(m_line, "'at' takes 'line-start'");

        rule.lineStart = true;
        i += 2;
    }

    // how it's coloured
    if (i == words.size())
        return Fail(m_line, "a rule needs a kind after its pattern");

    if (!Kind(words[i], &rule.kind))
        return false;

    i++;

    // where the line goes on
    if (i < words.size() && words[i].text == "enter")
    {
        if (i + 1 == words.size())
            return Fail(m_line, "'enter' takes a context's name");

        rule.action = SyntaxAction::Enter;
        rule.target = words[i + 1].text;
        i += 2;
    }
    else if (i < words.size() && words[i].text == "leave")
    {
        rule.action = SyntaxAction::Leave;
        i++;
    }

    if (i < words.size())
        return Fail(m_line, "unexpected '" + words[i].text + "'");

    m_def->contexts.back().rules.push_back(rule);
    return true;
}

bool Parser::Kind(const Word &word, SyntaxKind *kind)
{
    for (size_t i = 0; i < sizeof(KIND_NAMES) / sizeof(KIND_NAMES[0]); i++)
    {
        if (!word.quoted && word.text == KIND_NAMES[i])
        {
            *kind = (SyntaxKind)i;
            return true;
        }
    }

    return Fail(m_line, "unknown kind '" + word.text + "'");
}

//
//	The contexts and lists the rules name, now that every one is defined
//
bool Parser::Resolve()
{
    for (SyntaxContext &context : m_def->contexts)
    {
        for (SyntaxRule &rule : context.rules)
        {
            if (rule.pattern == SyntaxPattern::WordIn && m_def->lists.count(rule.text) == 0)
                return Fail(rule.line, "there's no list '" + rule.text + "'");

            if (rule.action != SyntaxAction::Enter)
                continue;

            for (size_t i = 0; i < m_def->contexts.size(); i++)
            {
                if (m_def->contexts[i].name == rule.target)
                    rule.enter = (int)i;
            }

            if (rule.enter < 0)
                return Fail(rule.line, "there's no context '" + rule.target + "'");
        }
    }

    return true;
}

bool Parser::Fail(int line, const std::string &message)
{
    error = std::to_string(line) + ": " + message;
    return false;
}
}

bool ParseSyntax(const std::string &text, SyntaxDefinition *definition, std::string *error)
{
    Parser parser(definition);

    *definition = SyntaxDefinition();

    if (parser.Parse(text))
        return true;

    if (error)
        *error = parser.error;

    return false;
}

const char *SyntaxKindName(SyntaxKind kind)
{
    return KIND_NAMES[(int)kind];
}

bool SyntaxKindFromName(const char *name, SyntaxKind *kind)
{
    for (int i = 0; i < SYNTAX_KINDS; i++)
    {
        if (std::string(name) == KIND_NAMES[i])
        {
            *kind = (SyntaxKind)i;
            return true;
        }
    }

    return false;
}
