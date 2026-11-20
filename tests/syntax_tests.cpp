// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

//
//	Syntax definition tests: reading .syntax files (syntax/README.md), the
//	mistakes that stop one being used, and colouring lines by one
//
#include "tests.h"
#include "../src/TextView/Syntax.h"
#include "../src/TextView/SyntaxLexer.h"

#include <fstream>
#include <sstream>

namespace
{
// A definition's text: the version marker, then its lines
std::string definition(const char *lines)
{
    return std::string("# neatpad-syntax-v1\n") + lines;
}

// The error reading text gives, or "" if it reads
std::string parse_error(const std::string &text)
{
    SyntaxDefinition def;
    std::string      error;

    if (ParseSyntax(text, &def, &error))
        return "";

    return error;
}

// C, cut down, for colouring lines
const char *const C_RULES = "language C\n"
                            "list keywords  return\n"
                            "list types     int\n"
                            "context main\n"
                            "    \"//\"              comment   enter line-comment\n"
                            "    \"/*\"              comment   enter block-comment\n"
                            "    '\"'               string    enter string\n"
                            "    number            number\n"
                            "    word in keywords  keyword\n"
                            "    word in types     type\n"
                            "context line-comment  comment  end-of-line\n"
                            "context block-comment  comment\n"
                            "    \"*/\"              comment   leave\n"
                            "context string  string  end-of-line\n"
                            "    escape            escape\n"
                            "    '\"'               string    leave\n";

// The letter for each kind, in SyntaxKind's order
const char KIND_LETTERS[] = ".cktCsenpEWIDhSiol";

//
//	Lines coloured one after another, each from the state the line before
//	ended in. colour() gives a line's kinds as a letter a unit: . plain,
//	c comment, k keyword, t type, s string, e escape, n number, p preprocessor,
//	h heading, S strong, i emphasis, o code, l link.
//
struct Colourer
{
    explicit Colourer(const std::string &text)
        : lexer(read(text))
    {
    }

    static SyntaxDefinition read(const std::string &text)
    {
        SyntaxDefinition def;

        CHECK(ParseSyntax(text, &def, 0));
        return def;
    }

    std::string colour(const char32_t *line)
    {
        tstring                 text = tstr(line);
        std::vector<SyntaxKind> kinds(text.size());
        std::string             letters;

        state = lexer.Line(state, text.data(), (int)text.size(), kinds.data());

        for (SyntaxKind kind : kinds)
            letters += KIND_LETTERS[(int)kind];

        return letters;
    }

    SyntaxLexer lexer;
    int         state = SyntaxLexer::START;
};

// How many units a character is in this build
size_t units(const char32_t *character)
{
    return tstr(character).size();
}

void syntax_reads_a_definition()
{
    SyntaxDefinition def;
    std::string      error;

    bool read = ParseSyntax(definition("language C\n"
                                       "files    *.c *.h\n"
                                       "\n"
                                       "list keywords  if else\n"
                                       "list keywords  return    # a list goes on\n"
                                       "\n"
                                       "context main\n"
                                       "    \"/*\"              comment   enter block-comment\n"
                                       "    '\"'               string    enter string\n"
                                       "    number            number\n"
                                       "    word in keywords  keyword\n"
                                       "\n"
                                       "context block-comment  comment\n"
                                       "    \"*/\"              comment   leave\n"
                                       "\n"
                                       "context string  string  end-of-line\n"
                                       "    escape            escape\n"
                                       "    '\"'               string    leave\n"),
                            &def, &error);

    CHECK(read);
    CHECK(error.empty());
    CHECK(def.language == "C");
    CHECK(def.files == std::vector<std::string>({ "*.c", "*.h" }));
    CHECK(def.lists["keywords"] == std::set<std::string>({ "if", "else", "return" }));
    CHECK(def.contexts.size() == 3);

    const SyntaxContext &main = def.contexts[0];

    CHECK(main.name == "main");
    CHECK(main.kind == SyntaxKind::Plain);
    CHECK(!main.endOfLine);
    CHECK(main.rules.size() == 4);

    CHECK(main.rules[0].pattern == SyntaxPattern::Text);
    CHECK(main.rules[0].text == "/*");
    CHECK(main.rules[0].kind == SyntaxKind::Comment);
    CHECK(main.rules[0].action == SyntaxAction::Enter);
    CHECK(main.rules[0].enter == 1);
    CHECK(main.rules[0].line == 9);

    CHECK(main.rules[1].text == "\"");
    CHECK(main.rules[1].enter == 2);

    CHECK(main.rules[2].pattern == SyntaxPattern::Number);
    CHECK(main.rules[2].action == SyntaxAction::Stay);

    CHECK(main.rules[3].pattern == SyntaxPattern::WordIn);
    CHECK(main.rules[3].text == "keywords");
    CHECK(main.rules[3].kind == SyntaxKind::Keyword);

    CHECK(def.contexts[1].kind == SyntaxKind::Comment);
    CHECK(def.contexts[1].rules[0].action == SyntaxAction::Leave);

    CHECK(def.contexts[2].endOfLine);
    CHECK(def.contexts[2].rules[0].pattern == SyntaxPattern::Escape);
    CHECK(!main.rules[0].lineStart);

    // a rule can match only at a line's start
    CHECK(ParseSyntax(definition("language M\ncontext main\n    \"#\"  at line-start  heading\n"), &def, &error));
    CHECK(def.contexts[0].rules[0].lineStart);
    CHECK(def.contexts[0].rules[0].kind == SyntaxKind::Heading);
}

void syntax_reads_quotes_comments_and_line_endings()
{
    SyntaxDefinition def;

    // # inside a word or a quote isn't a comment; CRLF lines read as LF ones
    CHECK(ParseSyntax("# neatpad-syntax-v1\r\n"
                      "language C#\r\n"
                      "list keywords  ab#c 'in quotes'\r\n"
                      "context main\r\n"
                      "    \"#\"  preprocessor\r\n"
                      "    'say \"hi\"'  string   # a comment\r\n",
                      &def, 0));

    CHECK(def.language == "C#");
    CHECK(def.lists["keywords"] == std::set<std::string>({ "ab#c", "in quotes" }));
    CHECK(def.contexts[0].rules[0].text == "#");
    CHECK(def.contexts[0].rules[1].text == "say \"hi\"");

    // a quoted word is text to match, never a statement
    CHECK(ParseSyntax(definition("language C\ncontext main\n    \"list\"  keyword\n"), &def, 0));
    CHECK(def.contexts[0].rules[0].text == "list");
}

void syntax_reports_mistakes_by_line()
{
    const char *ok = "language C\ncontext main\n";

    CHECK(parse_error(definition(ok)) == "");

    CHECK(parse_error("language C\ncontext main\n") == "1: the first line must be '# neatpad-syntax-v1'");
    CHECK(parse_error("") == "1: the first line must be '# neatpad-syntax-v1'");
    CHECK(parse_error(definition("context main\n")) == "2: there's no 'language' line");
    CHECK(parse_error(definition("language C\n")) == "2: there's no context");
    CHECK(parse_error(definition("language C\nlanguage D\ncontext main\n")) == "3: the language is already named");
    CHECK(parse_error(definition("language C\n\"x\" keyword\ncontext main\n")) == "3: a rule must follow a 'context'");

    CHECK(parse_error(definition("language C\ncontext main\ncontext main\n")) ==
          "4: there's already a context 'main'");
    CHECK(parse_error(definition("language C\ncontext main coment\n")) == "3: unknown kind 'coment'");
    CHECK(parse_error(definition("language C\ncontext main\n  numbers number\n")) ==
          "4: unknown statement or pattern 'numbers'");
    CHECK(parse_error(definition("language C\ncontext main\n  number\n")) == "4: a rule needs a kind after its pattern");
    CHECK(parse_error(definition("language C\ncontext main\n  \"x  keyword\n")) == "4: a quote isn't closed");
    CHECK(parse_error(definition("language C\ncontext main\n  \"\"  keyword\n")) == "4: an empty quote matches nothing");
    CHECK(parse_error(definition("language C\ncontext main\n  \"x\"  keyword  enter\n")) ==
          "4: 'enter' takes a context's name");
    CHECK(parse_error(definition("language C\ncontext main\n  \"x\"  keyword  leave now\n")) == "4: unexpected 'now'");
    CHECK(parse_error(definition("language C\ncontext main\n  \"#\"  at start  heading\n")) ==
          "4: 'at' takes 'line-start'");

    // names are checked once everything is defined, at the rule's line
    CHECK(parse_error(definition("language C\ncontext main\n  \"x\"  keyword  enter later\ncontext later\n")) == "");
    CHECK(parse_error(definition("language C\ncontext main\n  \"x\"  keyword  enter nowhere\n")) ==
          "4: there's no context 'nowhere'");
    CHECK(parse_error(definition("language C\ncontext main\n  word in keywords  keyword\n")) ==
          "4: there's no list 'keywords'");
}

void syntax_reads_the_definitions_neatpad_has()
{
#ifdef NEATPAD_SYNTAX_DIR
    for (const char *name : { "c.syntax", "log.syntax", "markdown.syntax" })
    {
        std::ifstream     file(std::string(NEATPAD_SYNTAX_DIR) + "/" + name, std::ios::binary);
        std::stringstream text;
        SyntaxDefinition  def;
        std::string       error;

        CHECK(file.good());
        text << file.rdbuf();

        bool read = ParseSyntax(text.str(), &def, &error);

        if (!read)
            printf("%s:%s\n", name, error.c_str());

        CHECK(read);
    }
#endif
}

void lexer_colours_words_numbers_and_strings()
{
    Colourer c(definition(C_RULES));

    CHECK(c.colour(U"int x = 42;") == "ttt.....nn.");
    CHECK(c.colour(U"return \"a\\\"b\";") == "kkkkkk.sseess.");
    CHECK(c.colour(U"0x1F 3.14 .5 10ms") == "nnnn.nnnn.nn.nnnn");

    // a word no rule matches is passed over whole: no number inside it
    CHECK(c.colour(U"utf8_2 x2") == ".........");
    CHECK(c.state == SyntaxLexer::START);
}

void lexer_carries_a_block_comment_to_the_next_line()
{
    Colourer c(definition(C_RULES));

    CHECK(c.colour(U"a /* b") == "..cccc");
    CHECK(c.state != SyntaxLexer::START);

    CHECK(c.colour(U"int */ x") == "cccccc..");
    CHECK(c.state == SyntaxLexer::START);
}

void lexer_leaves_end_of_line_contexts_at_the_end()
{
    Colourer c(definition(C_RULES));

    // a string that isn't closed, and a line comment, end with their line
    CHECK(c.colour(U"x = \"open") == "....sssss");
    CHECK(c.state == SyntaxLexer::START);

    CHECK(c.colour(U"a // b") == "..cccc");
    CHECK(c.state == SyntaxLexer::START);

    CHECK(c.colour(U"int") == "ttt");
}

void lexer_colours_whole_characters()
{
    Colourer    c(definition(C_RULES));
    std::string e(units(U"\u00e9"), '.');
    std::string u(units(U"\U0001F600"), 'e');

    // a word beyond ASCII, and an escaped emoji: each unit of a character
    // takes its kind
    CHECK(c.colour(U"\u00e9 1") == e + ".n");
    CHECK(c.colour(U"\"\\\U0001F600\"") == "se" + u + "s");
}

void lexer_matches_at_line_start()
{
    Colourer c(definition("language M\n"
                          "context main\n"
                          "    \"#\"  at line-start  heading  enter heading\n"
                          "context heading  heading  end-of-line\n"));

    CHECK(c.colour(U"# Title") == "hhhhhhh");
    CHECK(c.state == SyntaxLexer::START);
    CHECK(c.colour(U"a # b") == ".....");
}

void lexer_colours_markdown()
{
#ifdef NEATPAD_SYNTAX_DIR
    std::ifstream     file(std::string(NEATPAD_SYNTAX_DIR) + "/markdown.syntax", std::ios::binary);
    std::stringstream text;

    text << file.rdbuf();

    Colourer c(text.str());

    CHECK(c.colour(U"## Notes") == "hhhhhhhh");
    CHECK(c.colour(U"Some **bold** and *it* and `code` [link](url)") ==
          "....." "SSSSSSSS" "....." "iiii" "....." "oooooo" "." "llllll" ".....");
    CHECK(c.colour(U"* item") == "kk....");
    CHECK(c.colour(U"snake_case_name") == "...............");

    // a fenced block goes on over lines, and has no headings
    CHECK(c.colour(U"```") == "ooo");
    CHECK(c.colour(U"# not a heading") == "ooooooooooooooo");
    CHECK(c.colour(U"```") == "ooo");
    CHECK(c.state == SyntaxLexer::START);
#endif
}

void lexer_colours_dates_and_times()
{
    Colourer c(definition("language L\n"
                          "context main\n"
                          "    timestamp  debug\n"
                          "    number     number\n"));

    CHECK(c.colour(U"2026-11-20 12:34:56,789 x") == "DDDDDDDDDD.DDDDDDDDDDDD..");
    CHECK(c.colour(U"2026-11-20T12:34:56.5+01:00") == "DDDDDDDDDDDDDDDDDDDDDDDDDDD");
    CHECK(c.colour(U"20/11/2026 9:05Z") == "DDDDDDDDDD.DDDDD");

    // numbers, versions and a file's line and column aren't times
    CHECK(c.colour(U"42 1.2.3 10-20 x.c:123:4") == "nn.nnnnn.nn.nn.....nnn.n");
}

void lexer_reads_the_definitions_neatpad_has()
{
#ifdef NEATPAD_SYNTAX_DIR
    std::ifstream     file(std::string(NEATPAD_SYNTAX_DIR) + "/c.syntax", std::ios::binary);
    std::stringstream text;

    text << file.rdbuf();

    Colourer c(text.str());

    CHECK(c.colour(U"#include <x.h> // c") == "ppppppppppppppp" "cccc");
    CHECK(c.colour(U"if (p == NULL) return 0;") == "kk.......CCCC..kkkkkk.n.");
    CHECK(c.state == SyntaxLexer::START);
#endif
}

const test_case tests[] =
{
    { "syntax_reads_a_definition", syntax_reads_a_definition },
    { "syntax_reads_quotes_comments_and_line_endings", syntax_reads_quotes_comments_and_line_endings },
    { "syntax_reports_mistakes_by_line", syntax_reports_mistakes_by_line },
    { "syntax_reads_the_definitions_neatpad_has", syntax_reads_the_definitions_neatpad_has },
    { "lexer_colours_words_numbers_and_strings", lexer_colours_words_numbers_and_strings },
    { "lexer_carries_a_block_comment_to_the_next_line", lexer_carries_a_block_comment_to_the_next_line },
    { "lexer_leaves_end_of_line_contexts_at_the_end", lexer_leaves_end_of_line_contexts_at_the_end },
    { "lexer_colours_whole_characters", lexer_colours_whole_characters },
    { "lexer_matches_at_line_start", lexer_matches_at_line_start },
    { "lexer_colours_markdown", lexer_colours_markdown },
    { "lexer_colours_dates_and_times", lexer_colours_dates_and_times },
    { "lexer_reads_the_definitions_neatpad_has", lexer_reads_the_definitions_neatpad_has },
};
}

const test_group syntax_tests = { "syntax", tests, sizeof(tests) / sizeof(tests[0]) };
