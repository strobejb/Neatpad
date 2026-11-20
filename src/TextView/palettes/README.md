# Neatpad palettes

A palette is the text's colours, as a `.palette` file. The palettes here are
built into Neatpad; more can go in a folder of your own, and one with a
built-in palette's file name takes its place. On Linux that's
`~/.config/catch22/neatpad/palettes`.

Palettes are laid one over another: `adwaita.palette` underneath, then the
one chosen. What a palette leaves out is Adwaita's.

```ini
# a comment, on a line of its own
[palette]
name=Solarized
selection-text=caret

[light]
background=#fdf6e3
text=#657b83

[dark]
background=#002b36
text=#839496

[syntax]
keyword=#859900 bold

[dark-syntax]
comment=#586e75 italic
```

## Sections

| Section | Holds |
| --- | --- |
| `[palette]` | `name`, and colours for both styles |
| `[light]`, `[dark]` | Colours for that style, over `[palette]`'s. A palette with only one of them has it for both. |
| `[syntax]` | Syntax styles for both styles |
| `[light-syntax]`, `[dark-syntax]` | Syntax styles for that style, over `[syntax]`'s |

Each line is a `[section]`, `key=value`, or a comment starting with `#` or `;`
(a comment has a line of its own: `#` in a value is a colour's).

## Colours

| Key | Colours |
| --- | --- |
| `text`, `background` | The text, and behind it |
| `caret` | The caret |
| `selection-text`, `selection` | The selection, where the view has focus |
| `inactive-selection-text`, `inactive-selection` | The selection, where it hasn't |
| `margin` | The selection margin, left of the text |
| `line-number-text`, `line-number` | The line numbers |
| `long-line-text`, `long-line` | Text past the long-line column |
| `current-line-text`, `current-line` | The caret's line, highlighted |
| `match-text`, `match` | Search matches, highlighted |
| `changed-text`, `changed-line`, `changed-line-background` | Changes since the file was saved |

A colour is any of these, perhaps followed by a percentage, which is that
much of it over the background (`accent 30%`):

- `#rrggbb`
- `accent`: the desktop's accent colour
- another colour's key, such as `text` or `current-line`, from any palette

## Syntax styles

A key for each kind of text a syntax definition names (`src/TextView/syntax`):
`comment`, `keyword`, `type`, `constant`, `string`, `escape`, `number`,
`preprocessor`, `error`, `warning`, `info`, `debug`, `heading`, `strong`,
`emphasis`, `code` and `link`.

A style is a colour, then `bold` or `italic` if it's either: `keyword=#9141ac
bold`. A style that's only `bold` or `italic` keeps the colour beneath it.
