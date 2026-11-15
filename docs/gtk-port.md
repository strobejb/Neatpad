# Neatpad GTK4 port: structure and plan

This is the working plan for the GTK4 port. Keep "Current state" up to date as steps land.

## Decisions

Neatpad gets a GTK4 version by sharing one editor engine between thin platform front-ends, all in this repo. The work starts on Windows by refactoring TextView, with the working Win32 editor and its tests as the safety net.

- **One repo.** The core is still changing, and one test suite and CI should guard both editors.
- **Share the editor engine, not only TextDocument.** About 8k lines of TextView is mostly editor logic: cursor and selection, word and line moves, the scroll model, lazy indexing, find/replace, the filter, syntax colouring, the diff margin and recovery. Sharing only TextDocument would mean rewriting about 5k lines of subtle behavior for GTK, and the two copies would drift.
- **Windows first.** The core move and the TextView split must build cleanly and pass the tests on Windows before the Linux work starts. Split TextView in small steps, running the Win32 build and `SequenceTests.exe` after each one.
- **Each build uses its toolkit's own text unit:** `tchar_t` is UTF-16 on Win32 and UTF-8 on GTK (see Text units).
- **Balanced layout:** platform-specific code always goes in its own folder (see Repository layout).
- **Descriptive names:** `TextDocument`, `TextView`, `TextViewWin32`, `TextViewGtk`, `TextViewHost`, `TextLayout`, `TextCanvas`, `NeatpadTextView`. A brand name (NeatView, Neatext) was considered and turned down.
- **The two app shells share nothing to begin with.** Later candidates are the settings model and the recovery policy, plus the search logic if a Qt shell is added.
- **One native toolkit per desktop.** Win32 on Windows and GTK4 with libadwaita on GNOME, so neither adds a dependency there. A Qt front-end (KDE, and a route to macOS) can come later in `src/TextView/qt/` and `src/Neatpad/qt/`, so no interface is shaped by one toolkit. Qt is never shipped on Windows.
- **Commits go straight to `main`.**

## Repository layout

Each component is a folder under `src/`. Its root holds only portable code; platform code goes in `win32/` or `gtk/` (and `qt/` later).

```
src/TextDocument/            storage + text model, portable: sequence*, TextDocument*, SaveFile, Unicode, codepages
src/TextView/                portable editor engine, class TextView: cursor, selection, navigation,
                             scrolling, editing, find/replace, filter, syntax, diff margin, layout cache
src/TextView/win32/          TextViewWin32: HWND/WndProc, GDI painting, TXM_*/TVN_* messages,
                             UspTextLayout (wraps UspLib), Win32Canvas
src/TextView/win32/UspLib/   Uniscribe wrapper, moved from src/UspLib, otherwise unchanged
src/TextView/gtk/            TextViewGtk + the GObject type NeatpadTextView (neatpad_text_view_*,
                             signals), PangoTextLayout, GtkCanvas
src/Neatpad/                 shared app code (empty to begin with)
src/Neatpad/win32/           Win32 app (C), moved from src/Neatpad, otherwise unchanged
src/Neatpad/gtk/             GTK4 app (C); the installed binary is "neatpad"
tests/                       portable core and engine tests
tests/win32/                 textview tests that drive the Win32 window
```

- **TextDocument has no platform folders.** Its platform code is about 15 `#ifdef _WIN32` lines across a few files, and its split would be by OS (Windows/POSIX) rather than by toolkit.
- **No `src/lib`.** Shared code is named for what it is.
- Moving files doesn't break the catch22 tutorials, because the `tutorial/part-*` tags point at old commits.

## Class hierarchy and containment

Inheritance is limited to three small interfaces; everything else is composition. The platform object owns the engine, and the engine owns its documents and layouts.

```
Win32                                     GTK
HWND (window extra data)                  NeatpadTextView (GtkWidget, instance private data)
└─ TextViewWin32 : TextViewHost           └─ TextViewGtk : TextViewHost
   ├─ fonts                                  ├─ fonts
   └─ TextView (engine)                      └─ TextView (engine)
      ├─ TextDocument                           ├─ TextDocument
      └─ line-layout cache                      └─ line-layout cache
         └─ UspTextLayout : TextLayout             └─ PangoTextLayout : TextLayout
```

On Win32 the window's extra data holds `TextViewWin32`, as `SetWindowLongPtr` holds `TextView` today. On GTK the widget's instance private data holds `TextViewGtk`, which refers to the widget but doesn't own it.

**Inheritance**

```
TextViewHost   (abstract)  <- TextViewWin32, TextViewGtk
TextLayout     (abstract)  <- UspTextLayout, PangoTextLayout
TextCanvas     (abstract)  <- Win32Canvas (HDC), GtkCanvas (GtkSnapshot)
TextView       (concrete engine; not subclassed)
TextDocument   (concrete; unchanged)
```

**Ownership rules**

- The platform object owns the engine.
- The engine owns its documents and its line-layout cache.
- The host owns the fonts, so layouts refer to fonts by index, as `ATTR.font` does today.
- Nothing in the engine outlives the host.

**Why composition rather than `TextViewWin32 : public TextView`:** the engine can be tested on Linux with a fake host and layout, platform types never leak into it, and the interfaces are the whole contract between the two.

## Interfaces and call direction

The engine reaches the platform only through three small interfaces, so it includes no platform headers. They're shaped so that Uniscribe, Pango and Qt each fit without a special case in the engine.

| Interface | What it does | Win32 | GTK | Qt (later) |
| --- | --- | --- | --- | --- |
| `TextViewHost` | Holds the engine and does what it asks: invalidate rows or a rectangle (hints only: GTK redraws the whole widget), scroll pixels, set scrollbars, caret, timers, clipboard, notifications, fonts and metrics, creating `TextLayout`s | `TextViewWin32` | `TextViewGtk` | `TextViewQt` |
| `TextLayout` | One line laid out once, in the build's own text units: analyze, x to offset and back, snap, cursor and word stops, width. Font and colour runs are set at layout time; the selection is passed in at draw time, as `UspApplySelection` does today: `Draw(TextCanvas&, x, y, selection)` | `UspTextLayout` (wraps `USPDATA`) | `PangoTextLayout` (UTF-8, no offset map) | `QtTextLayout` (wraps `QTextLayout`) |
| `TextCanvas` | Created by the host for each paint: fill rect, draw a layout, margin text (line numbers), margin images (diff markers). Assumes nothing about partial repaints | `Win32Canvas` (HDC) | `GtkCanvas` (GtkSnapshot) | `QtCanvas` (QPainter) |

**Platform to engine.** The WndProc or the GTK event controllers translate input into engine calls: `OnKey(key, mods)`, `OnChar(text)`, `OnMouseDown(x, y, mods)`, `OnScroll`, `OnTimer(id)`, `OnResize` and `Paint(TextCanvas&, rect)`. They also forward the public API: `TXM_*` messages on Win32 and `neatpad_text_view_*()` functions on GTK.

**Engine to platform.** Only through `TextViewHost`, and through the layouts and canvases it creates.

**Public API.** Win32 keeps its `TXM_*`/`TVN_*` messages unchanged. GTK gets matching C functions and GObject signals, for example `neatpad_text_view_open_file()` and `::cursor-changed`.

## Text units

Each build uses its toolkit's own text unit, `tchar_t`: UTF-16 on Win32, UTF-8 on GTK, and UTF-16 again if a Qt build comes. No front-end converts offsets.

- **Document positions don't depend on it.** `TextPos` is a byte offset in the file's own encoding, so undo, recovery and the line index are the same in every build.
- **Why not one unit everywhere:** UTF-16 everywhere needs an offset map in `PangoTextLayout`. UTF-8 everywhere needs maps for Uniscribe, Qt and the Win32 messages. A unit per build needs none.
- **UTF-8 is not the old 8-bit `TCHAR` build.** That was single-byte ANSI. A UTF-8 character is 1–4 bytes, so the surrogate-pair handling becomes one character-boundary helper that works for both widths.
- **Where the width matters today:** 17 casts to `UTF16 *` (12 in TextDocument's two conversion functions, 4 in find, 1 in paint) and 7 surrogate-pair spots, mostly in `TextDocumentCoord.cpp`. With `tchar_t = char` the core already compiles, but the casts hide the width, so it would be wrong.
- **Columns:** `TextCoord::column` counts `tchar_t` units, as the view lays a line out: UTF-16 units on Win32, as the
  EDIT control does, and UTF-8 bytes on GTK, as Pango does. The GTK status bar shows characters, not bytes, so it
  converts. Character offsets (`charoffset_from_pos`) are UTF-16 units in every build, as the sequence indexes them.
- **Pango** positions are UTF-8 byte offsets, so they match. Cursor and word stops come one per character, so `PangoTextLayout` spreads them out to bytes.
- **`tchar_t` and `TCHAR` never mix in the core or the engine.** `TCHAR` stays in Win32 code and the C API. Both are `wchar_t` on Windows, so only the Linux build catches a mix-up.
- **`wchar_t` is 32-bit on Linux,** so a UTF-16 build there (Qt) uses `char16_t`.
- **Linux filenames** are bytes, UTF-8 by convention. `seqfilechar` is already the platform's own form.

## Build and CI

Visual Studio stays in charge of the Win32 build, and CMake handles everything portable plus GTK.

- **`Neatpad.sln` and the `.vcxproj` files** build the Win32 app. Tutorial readers use Visual Studio.
- **CMake** builds TextDocument, the TextView engine and the portable tests on Linux and Windows, plus the GTK view and app on Linux.
- **The source lists are duplicated** between the vcxproj files and CMake. That's accepted for now; generating the VS projects from CMake is an option later.
- **CI:** the existing windows-2022 job, plus a new Ubuntu job for core, engine, tests and GTK.
- **Warnings:** the projects build at warning level 3 without warnings-as-errors, so CI passes with new warnings. Compare the MSBuild "Warning(s)" count before and after each change.
- **Tests** split into portable core tests (`tests/`) and Win32 textview tests (`tests/win32/`). Engine tests run with a fake `TextViewHost` and `TextLayout`, on Linux too.
- **`tests/tests.h`** includes `windows.h` and `winioctl.h`, and the document and sequence tests contain about 150 Win32-isms (`TCHAR`, `_T`, `DWORD`). Split out a portable test header, and put `crtdbg` in `main.cpp` under `#ifdef`.

## Order of work

Each step is its own commit on `main`, and moves and formatting changes stay separate from behavior changes (per AGENTS.md). Build Debug and Release x64 and run `SequenceTests.exe` after every commit.

1. **Move the core into `src/TextDocument`**. Then, as separate commits, move the app to `src/Neatpad/win32` and UspLib to `src/TextView/win32/UspLib`.
2. **Add tests for what the split touches.** Today's `textview_tests` cover the `EM_*` messages, `WM_CHAR`, the filter and recovery, but no key navigation, mouse or painting. Add arrow, word and Home/End moves over surrogate pairs and combining marks, click-to-caret, and a few paint bitmap hashes (`WM_PRINTCLIENT` into a memory DC).
3. **On Windows, split TextView into the engine plus `TextViewWin32`,** in small steps:
   1. Introduce `TextViewHost`, and route invalidation, scrolling, caret, timers, clipboard and notifications through it.
   2. Introduce `TextLayout` with `UspTextLayout` behind `GetLineLayout` and the line cache.
   3. Introduce `TextCanvas`, so painting decides what to draw and the canvas draws it.
   4. Move the HWND, WndProc and message code into `src/TextView/win32/`.
   5. Replace Win32 typedefs inside the engine with portable types, as a separate mechanical commit: `TCHAR` → `tchar_t`, `ULONG` → `uint32_t` (not `unsigned long`, which is 64-bit on Linux), and likewise `UINT`, `LONG`, `DWORD`, `BOOL`, `COLORREF` and `RECT`.
   6. After each step: the Win32 build, `SequenceTests.exe` and a manual check in Neatpad.
4. **CMake, portable core tests and Ubuntu CI.**
5. **Make the core width-agnostic and prove UTF-8.** The `textchar` → `tchar_t` rename is done. Replace the surrogate-pair spots with one character-boundary helper, and add UTF-8 versions of the two conversion functions and of case-insensitive find. Ubuntu CI runs the core tests with `tchar_t = char` on UTF-8 samples (accents, CJK, emoji); Windows CI keeps UTF-16. If UTF-8 doesn't hold up, fall back to UTF-16 everywhere with an offset map in `PangoTextLayout`.
6. **The engine on Linux.** Audit the engine for ±1 offset steps that bypass the layout's cursor stops. Then build it with `tchar_t = char` and a fake host, and run its tests in Ubuntu CI.
7. **Optional throwaway GTK spike:** TextDocument + Pango, scrolling a multi-GB file. It tests Pango's speed, selection painting and control characters before `PangoTextLayout` is written. If you want it to shape `TextLayout` itself, do it before step 3.
8. **Build `TextViewGtk` and `NeatpadTextView`** on the engine.
9. **Build the GTK app in `src/Neatpad/gtk`:** first open, edit, save, find, undo and the status bar. Then options, recovery and theming one at a time. Printing was dropped: the Win32 app doesn't print either (`Printing.c` isn't used).

## Current state

- **Done:** `textchar` renamed to `tchar_t`, on `main`.
- **Done:** step 1, on Windows, as three commits on `main`:
  - The core is in `src/TextDocument`, built as the static library `TextDocument.vcxproj`. TextView and Neatpad
    reference it, and `SequenceTests` still compiles its sources directly.
  - The Win32 app is in `src/Neatpad/win32`. `release.yml` looks for `resource.rc` there first, then in
    `src/Neatpad`, so older tags still build.
  - UspLib is in `src/TextView/win32/UspLib`.
  - Debug and Release x64 build with no warnings, as before, and `SequenceTests.exe` passes.
- **Done:** step 2. `textview_tests` cover arrow, word, Home/End and up/down moves and Shift selection over
  surrogate pairs and combining marks, click-to-caret and margin clicks, and painting through `WM_PRINTCLIENT`, which
  TextView now handles. Paint hashes depend on the machine's fonts, so the tests compare paintings with each other
  rather than with stored hashes. Set `NEATPAD_PAINT_HASHES=1` to print a few, to compare builds on one machine.
- **Done:** step 3.1. `TextViewHost` (`src/TextView/TextViewHost.h`) carries redraws, scrolling and scrollbars, the caret,
  timers, the mouse position for drag-scrolling, the clipboard and notifications. `TextViewWin32` implements it in
  `src/TextView/win32`. Until step 3.4 the engine is still the window's object and owns its host. Client size, fonts
  and DCs, focus, mouse capture and cursors, the theme and the context menu still call Win32 directly.
- **Done:** step 3.2. `TextLayout` (`src/TextView/TextLayout.h`) is one line laid out: x to offset and back, snapping,
  cursor and word stops, white space, width and the selection. `UspTextLayout` in `src/TextView/win32` wraps `USPDATA`.
  The line cache (`LINECACHE`) holds layouts, and navigation, the mouse, the caret and painting use them. Still to
  move: drawing (`UspTextLayout::Draw` until `TextCanvas`), the `ATTR` run type, and the fonts, so for now the
  engine creates its layouts rather than the host.
- **Done:** step 3.3. `TextCanvas` (`src/TextView/TextCanvas.h`) is what painting draws on: rows, filled and checked
  rectangles, margin text and margin images. `Win32Canvas` in `src/TextView/win32` draws each row into a memory bitmap
  and copies it across, as before, and `TextLayout::Draw` takes a canvas. Painting draws whole rows rather than
  clipping to the update region. `OnPaint` and `OnPrintClient` make the canvas for now, and the line-number width
  is still measured outside painting with a window DC.
- **Step 3.4** is done in four parts:
  - **Done:** 3.4a. The window holds `TextViewWin32`, which owns the engine. Its WndProc, the message dispatch, the
    themed border, the context menu and focus-on-click are in `src/TextView/win32/TextViewWndProc.cpp`. The engine
    has public methods for what the messages ask.
  - **Done:** 3.4b. The host gives the view's size, focus and active state, mouse capture, the cursor shape, the caret
    width and wheel lines; the margin cursor (`racursor.h`) is in `win32/`. The index and filter slices are timed
    with `std::chrono`. Modifier keys still come from `GetKeyState`, and keys are still `VK_*` codes.
  - **Done:** 3.4c. `TextViewWin32` holds the fonts (`USPFONT`) and the image list, makes the layouts, and handles
    WM_PAINT and WM_PRINTCLIENT with a `Win32Canvas`. The engine reads font metrics, text width and image size from
    the host, and calls `FontsChanged()` to measure again. It holds no `HWND` now.
  - **Done:** 3.4d. The `EM_*` edit-control messages are in `win32/TextViewEdit.cpp`, still a `TextView` member,
    declared only on `_WIN32`. The index-as-needed lookups they use stay in the engine, in `TextViewFile.cpp`.
- **Done:** step 3.5. The engine uses `tchar_t`, `uint32_t`, `int32_t`, `bool`, `uintptr_t`, `void` and a `TextRect`
  instead of `TCHAR`, `ULONG`, `UINT`, `LONG`, `DWORD`, `BOOL`, `COLORREF`, `RECT`, `VOID` and `UINT_PTR`. Kept on
  purpose: `DWORD` for the filter's `WriteFile` counts, and `COLORREF` in `RealizeColour` and `MixRGB`, which `TextView.h`
  declares for the app. `TextView.h` and the `win32/` code are unchanged.
- **Step 4** is done in three parts. The Linux parts can't be built on the Windows machine, so they're first checked
  on Fedora or in CI.
  - **Done:** 4a. `CMakeLists.txt` builds the `TextDocument` library and `CoreTests` (the sequence, line, char and
    document groups), checked with MSVC. `SequenceTests.vcxproj` defines `NEATPAD_TEXTVIEW_TESTS` to add the textview
    group.
  - **Done:** 4b. The core tests are portable, and `CoreTests` passes on Fedora with GCC. Text is `tchar_t`, with
    `TSTR("...")` literals and `tstring`, and paths are `seqfilechar`; neither `TCHAR` nor `wchar_t` is left. Files
    are made, read and deleted with `std::filesystem` and stdio, so `SequenceTests.vcxproj` builds as C++17.
    Windows-only, behind `_WIN32`: the share-mode tests (locked files, a writer appending, growth), read-only
    attributes, the NTFS stream, the ANSI code page and the creation-time check. Windows CI builds it and
    `SequenceTests.exe` passes.
  - **Done:** 4c. `Unicode.c` has POSIX fallbacks: 8-bit text is taken as Latin-1, and case is compared with
    `towlower_l` in the C.UTF-8 locale, one UTF-16 unit at a time. `build.yml` has a `linux` job on ubuntu-24.04
    that builds with CMake and runs `ctest`.
- **Done:** step 5. Off Windows `tchar_t` is `char`, holding UTF-8, and `TCHAR_UTF8` says so. Each UTF-16 path in
  the core has a UTF-8 one beside it, compiled in both builds, so Windows CI builds the UTF-8 code too:
  - Reading decodes a character at a time to UTF-8 (`decode_utf8`), and edits and saves encode it back
    (`encode_utf8`). What isn't a character becomes U+FFFD; what ASCII (Latin-1) can't hold is saved as `?`.
  - A UTF-16 decode block never ends on a high surrogate, so a pair stays whole.
  - Case-insensitive find converts a UTF-8 pattern to UTF-16 and searches as before.
  - Column shortcuts for ASCII and UTF-16 files only apply to UTF-16 text.
  - The tests are the same in both builds: `tstr(U"...")` makes text in the build's units, `encode_text` takes code
    points, and UTF-16 file data is `char16_t`. Ubuntu CI runs them in UTF-8 and Windows CI in UTF-16.
- **Done:** step 6. The engine builds on Linux as the CMake library `TextViewEngine`, and its tests run there:
  - The engine includes no platform headers. On `_WIN32` `TextViewInternal.h` still includes `windows.h`, for the
    types of `EditMessage`, which `win32/TextViewEdit.cpp` defines.
  - Its own types replace Win32's: `TextRun` (for UspLib's `ATTR`), `TVK_*` keys and `TVSB_*` scroll actions
    (Win32's values), `TextViewFind`, `TextViewDocStats`, `TextViewRecovery`, `TextViewCursorInfo` and
    `TextViewIndexInfo`, and `TVNOTIFY_*` codes. The Win32 window converts its `TXM_*` structures, and its host
    sends `TVN_*` notifications as before. `TextViewDefs.h` holds the definitions both share, for C too.
  - The host gives the modifier keys (`Modifiers`) and system colours (`SystemColour`, by `TVSC_*` index: Win32's
    `COLOR_*` numbers). `RealizeColour` stays exported for the application, in `win32/`.
  - The filter's temp file is `TempAppendFile` (`SaveFile.h`): the same file on Windows, `mkstemp` elsewhere, in the
    build's own text. `KeepLastError` keeps `errno` and `GetLastError` over cleanup.
  - `OnText` takes typed text as a string, as an input method gives it.
  - The audit for unit steps: the engine's character and word moves stop where the layout says, and
    `TextLayout.h` now says that no stop is ever inside a character. Fixed for UTF-8: the line cache's and the
    clipboard's buffers (a byte can be 3 UTF-8 units), overwrite replacing one character with one typed character
    of any length (also a surrogate pair on Windows), and Tab-to-spaces counting characters.
  - `tests/engine_tests.cpp` drives the engine through a fake host, layout and canvas, in both builds: Ubuntu CI in
    UTF-8, Windows CI in UTF-16.
- **Done:** step 8, the GTK view, in `src/TextView/gtk`. CMake builds it as `TextViewGtk` where GTK 4 is found:
  - `NeatpadTextView` is the widget, with a C API (`NeatpadTextView.h`): open a file, the font, `TXS_*` styles, the
    caret, undo and redo, the clipboard, and signals for the engine's notifications. It holds `TextViewGtk`. It's a
    `GtkScrollable`, so a `GtkScrolledWindow` round it has the scrollbars: GNOME's overlay scrollbars, or always
    shown as Accessibility says. Its adjustments are bytes down (where the top line starts) and characters across.
  - `TextViewGtk` is the host. Timers are GLib sources, the caret is drawn over the text, and redraws are the whole
    widget. Scroll ranges set while painting wait until the frame is drawn, as GTK can't resize the scrolled window
    part way through drawing it. Paste reads the clipboard first (GTK reads it asynchronously), then the engine pastes it.
    Glyphs are at whole pixels, as the engine works in them.
  - `PangoTextLayout` lays a line out with Pango. Its stops, per character in Pango, are on each character's first
    byte only. Run backgrounds and the selection are drawn under the text; foregrounds are Pango attributes.
  - `GtkCanvas` draws on a `GtkSnapshot`. `FillChecked` is a plain fill of the two colours mixed.
  - Keys go to the engine as `TVK_*`, text through a `GtkIMContext` to `OnText`, and Ctrl+A/C/X/V/Z/Y to the
    widget's own calls. The mouse, the wheel (a notch is 120, as on Win32) and focus go to the engine too.
  - `tests/gtk/textview_demo.c` shows the widget in a window; `--screenshot FILE.png` saves what it draws.
    `tests/gtk/gtk_tests.cpp` tests the Pango layout's stops and the widget, with real fonts: Ubuntu CI runs them
    under Xvfb.
  - Not yet: the theme's colours (a light palette for now), margin images, the input method's
    preedit text and cursor location, the context menu, more than one font, and saving.
- **Done:** step 9, the GTK app: `src/Neatpad/gtk/neatpad.c`, built as `neatpad` where libadwaita is found.
  Step 7, the Pango spike, wasn't needed.
  - **Done:** the first cut. A libadwaita window per document: open, save and save as (GTK's file dialogs), a
    question before closing or opening over unsaved changes, find (next, previous, match case, wrapping), undo
    and redo, and a status bar with the caret's line and column (in characters) and the encoding. Files given on
    the command line each open a window. The widget gained `new_file`, `save_file`, `get_modified`,
    `get_encoding` and `find`, and its column counts characters (`TextView::CursorCharColumn`).
  - Rows start and end on whole device pixels, so a fractional display scale draws no lines between them.
  - **Done:** the first UX pass. Padding round the text (12px across, 6px down; GNOME Text Editor's 12px margins
    scroll with the text, which the line-based scroll model can't do cheaply). Open is a split button with recent
    files (GNOME's list, those Neatpad opened or saved), each with its folder and a button to remove it, as Text
    Editor shows them. The main menu has Find, Replace and Go to Line in a
    section. The view has a context menu as on Win32, without its two edit-control items that do nothing.
    Replace is a second row in the find bar; Go to Line (Ctrl+I, as Text Editor) is a bar of its own.
  - **Done:** the style and preferences. The text takes the colours of GtkSourceView's Adwaita and Adwaita Dark
    schemes (`neatpad_text_view_set_scheme`), with the selection in the accent colour (libadwaita 1.6; Adwaita
    blue before), and follows libadwaita's style. The main menu starts with a style switcher (follow the system,
    light, dark), as Text Editor's. Preferences has the font (fixed-width only), line numbers, current line,
    highlighting changes, tab width, spaces for tabs and auto-indent. Settings are a `GKeyFile`,
    `~/.config/catch22/neatpad/settings.ini`, and apply to every window as they change.
  - **Done:** recovery and Restore Session, as the Win32 app's recovery (`src/Neatpad/gtk/recovery.c`). Each
    document's unsaved state goes to a store of its own in `~/.local/state/catch22/neatpad/recovery` once edits
    pause (1.5 seconds, or 10 at most), and before a save replaces what it refers to. A store is locked (`flock`)
    while a window has it, and marked `clean` when there's nothing to restore. With Restore Session (on by
    default) a window closes with its unsaved changes kept as a session, as Win32 Neatpad and GNOME Text Editor do,
    and the files open as Neatpad quits (or as its last window closes) open again next time
    (`session.ini`). Without it, closing asks to save or discard. Starting, stores a crash or a session left are
    restored, each in a window. Use System Font follows GNOME's monospace font. The engine's recovery state is
    now tested on Linux too.
  - **Done:** a banner (`AdwBanner`) for what Win32 Neatpad's notification bar says: recovered or restored changes
    (Revert to Saved, or Discard Document), recovery data that can't be written, another program changing the file
    (Reload) or deleting it, and following it.
  - **Done:** file watching and Follow File. A `GFileMonitor` says when to look at the file, and its size and time
    say whether another program changed it, as Win32's once-a-second check does, so Neatpad's own saves aren't
    news. Follow File (a check item in the main menu, as `AdwBanner` has one button) opens the file read-only at
    its end and takes in what's appended; Stop, or a file rewritten shorter, opens it as usual. Linux has no
    share-mode locks, so a file another program is writing doesn't open read-only, as it does on Windows. Growing
    a document is now tested on Linux too.
  - **Done:** the status bar has Win32's panes, in its order: the caret, the document's size or the selection's,
    the index link, the zoom, the line endings (", mixed" when there are others) and the encoding. Zoom is as
    Win32's: 10% steps from 10% to 500%, Ctrl and the wheel, plus, minus or 0. Preferences can hide the status bar.
  - Printing isn't planned: the Win32 app doesn't print either. If it's wanted, both apps can add it together,
    drawing pages with the engine's painting.
  - **Done:** the caret blinks as GNOME's settings say (`gtk-cursor-blink`, its time and timeout), shown again as
    it moves. `scripts/install-dev-desktop.sh` installs a desktop entry and icon for the build in this checkout,
    named for the app ID (`net.catch22.Neatpad`), so GNOME shows it in the app grid, the dock and Open With. The
    icon is `src/Neatpad/gtk/neatpad-gnome-512.png`.
- **Next:** smaller gaps, as wanted: the context menu from the keyboard, input-method preedit, line endings and
  encoding when saving, and a filter in the find bar.

## Open decisions

| Decision | Needed by | Suggestion |
| --- | --- | --- |
| C++ namespace | Any time; a separate mechanical commit | `neatpad`, with library targets named after their folders |
| Pango spike | Before step 3 if it should shape `TextLayout` | Do it, timeboxed |
| ANSI code pages on Linux (`Unicode.c`) | Step 5 | glibc `iconv`: part of the C library, so no new dependency |
| Oldest GTK4 and libadwaita versions | Step 8 | Whatever the oldest distro you support ships; CI builds against that |
| How the GTK app ships | Step 9 | Flatpak, distro packages or from source. Flatpak opens files through its document portal, so check multi-GB file performance |
| Where Linux settings are stored | Step 9 | Decided: `GKeyFile` in `~/.config/catch22/neatpad` (part of GLib, nothing to install) |

## Details to settle during the refactor

- **The filter's temp file** is a Win32 `HANDLE` (`m_hFilterFile`). Move it into TextDocument or `SaveFile` with portable code, rather than adding it to the host.
- **The context menu** (`CreateContextMenu`, an `HMENU`) belongs to the platform class. The engine should only report which commands are available.
- **Fonts:** `LOGFONT`-based settings need a `PangoFontDescription` equivalent. Layouts refer to fonts by index, as `ATTR.font` does today.
- **`Unicode.c`:** the ANSI code page and invariant case comparison are Windows-only and need a POSIX fallback.
- **Large files:** never lay out more than the visible lines, and keep the `LINE_LAYOUT_LIMIT` cap.
- **GTK host:** text input goes through `GtkIMContext`, including preedit (Win32 gets input methods for free through `WM_CHAR`). Host metrics are in logical pixels for HiDPI. Touchpad smooth scrolling has to fit the engine's line-based scroll model.
