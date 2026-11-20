# Neatpad syntax definitions

A syntax definition says how to colour one language. Each is a plain text file,
`<language>.syntax`, read a line at a time: every line is one statement, and its
first word says which.

```
# neatpad-syntax-v1
#
#   C strings and comments, cut down
#
language C
files    *.c *.h

list keywords  if else for while return

context main
    "//"              comment   enter line-comment
    '"'               string    enter string
    number            number
    word in keywords  keyword

context line-comment  comment  end-of-line

context string  string  end-of-line
    escape            escape
    '"'               string    leave
```

## Statements

| Statement | Says |
| --- | --- |
| `language NAME` | The language's name. Once, and required. |
| `files PATTERN...` | The file names it's for, such as `*.c`. Any number. |
| `list NAME WORD...` | Words for `word in NAME` to look up. A list can go on over several `list` lines. |
| `context NAME [KIND] [end-of-line]` | Starts a context: the rules after it, up to the next context, belong to it. |
| `PATTERN [at line-start] KIND [enter NAME \| leave]` | A rule, in the context above it. `at line-start` matches only at a line's first character, as a Markdown heading's `#`. |

- The first line is the version marker, `# neatpad-syntax-v1`, exactly.
- Words are separated by spaces or tabs. Indenting means nothing, but makes the rules easy to read.
- `#` at the start of a word starts a comment, to the end of the line. Inside a word (`C#`) it's just a character.
- Text in quotes is one word: `"..."` or `'...'`, with no escapes. Use the other quote for a quote: `'"'`.

## Contexts and rules

A line is coloured from its start. At each place, the rules of the context it's
in are tried in order, and the first that matches colours what it matched with
its kind. Where none matches, the character takes the context's kind (`plain`
unless the context says otherwise), and a word is passed over whole, so a number
rule never matches the `2` in `utf8_2`.

Lines start in the first context. A rule can **enter** another context, which
lasts until a rule in it says **leave**, so a block comment can run over many
lines. A context marked `end-of-line` is left at the end of the line it started
on, as a line comment, or a string that isn't closed, is.

The text a rule matches takes the rule's kind, including the quote or marker
that enters or leaves a context.

## Patterns

| Pattern | Matches |
| --- | --- |
| `"text"` or `'text'` | That text, exactly as written. |
| `number` | A digit, or `.` and a digit, and the letters, digits, `.` and `_` after it: `42`, `0x1F`, `3.14`, `10ms`. |
| `timestamp` | A date or a time, as a log's lines start with: `2026-11-20`, `20/11/2026`, `12:34:56.789`, `2026-11-20T12:34:56Z`. Put it before `number`, which would match its first digits. |
| `word` | A word: a letter, `_` or any character beyond ASCII, then any of those or digits. |
| `word in NAME` | A word in the list `NAME`, matching case. |
| `space` | Spaces and tabs. |
| `escape` | A backslash and the character after it. |

## Kinds

What a rule's text is, so the palette can give it a colour:

`plain` `comment` `keyword` `type` `constant` `string` `escape` `number`
`preprocessor` `error` `warning` `info` `debug` `heading` `strong` `emphasis`
`code` `link`

The palette gives each kind its colour, and bold or italic: a definition says
only what text is.

## Errors

A definition with a mistake isn't used. The error gives its line, such as
`c.syntax:12: unknown kind 'coment'`. These are mistakes:

- a missing or different version marker, or no `language` line
- a statement or pattern that isn't one of the above, or an unknown kind
- a quote that isn't closed, or an empty one
- `at` without `line-start` after it
- a rule before the first `context`, or no context at all
- two contexts with the same name
- `enter` a context, or `word in` a list, that isn't defined
