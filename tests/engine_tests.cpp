// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

//
//	TextView engine tests: the editor without a window, through a fake host,
//	layout and canvas, so they run on every platform and in each build's text
//	units. Every character is 8 pixels wide and a line is 16 high.
//
#include "tests.h"
#include "../src/TextView/TextViewInternal.h"

#include <type_traits>

namespace
{
const int CELL_WIDTH  = 8;
const int LINE_HEIGHT = 16;

// Whether the unit at i starts a character, rather than continuing one
bool starts_character(const tstring &text, size_t i)
{
    uint32_t unit = (uint32_t)(std::make_unsigned<tchar_t>::type)text[i];

    if (TCHAR_UTF8)
        return (unit & 0xC0) != 0x80;

    return unit < 0xDC00 || unit > 0xDFFF;
}

// Each unit's background and text colour, as the last line laid out with
// colours had them
static std::vector<uint32_t> last_backgrounds;
static std::vector<uint32_t> last_foregrounds;

//
//	A line laid out a character to a cell, with tab stops every tabWidth cells.
//	Cursor and word stops are at character starts, never inside one.
//
class FakeLayout : public TextLayout
{
  public:
    bool Analyze(const tchar_t *text, int length, TextRun *runs, int tabWidth) override
    {
        int x = 0;

        if (runs)
        {
            last_backgrounds.clear();
            last_foregrounds.clear();

            for (int i = 0; i < length; i++)
            {
                last_backgrounds.push_back(runs[i].bg);
                last_foregrounds.push_back(runs[i].fg);
            }
        }

        m_text.assign(text, length);
        m_x.assign(length + 1, 0);

        for (int i = 0; i < length; i++)
        {
            m_x[i] = x;

            if (!starts_character(m_text, i))
                continue;

            if (m_text[i] == '\t' && tabWidth > 0)
                x += tabWidth * CELL_WIDTH - x % (tabWidth * CELL_WIDTH);
            else
                x += CELL_WIDTH;
        }

        m_x[length] = x;
        return true;
    }

    int Length() override
    {
        return (int)m_text.size();
    }

    int Width() override
    {
        return m_x.back();
    }

    int OffsetToX(int offset, bool trailing) override
    {
        if (offset >= Length())
            return Width();

        return trailing ? m_x[Next(offset)] : m_x[offset];
    }

    void XToOffset(int x, int *offset, bool *trailing) override
    {
        int i = 0;

        while (i < Length() && m_x[Next(i)] <= x)
            i = Next(i);

        *offset   = i;
        *trailing = i < Length() && x >= (m_x[i] + m_x[Next(i)]) / 2;
    }

    int SnapXToOffset(int x, int *snappedX) override
    {
        int  offset;
        bool trailing;

        XToOffset(x, &offset, &trailing);

        if (trailing)
            offset = Next(offset);

        *snappedX = m_x[offset];
        return offset;
    }

    bool IsCursorStop(int offset) override
    {
        return offset >= Length() || starts_character(m_text, offset);
    }

    bool IsWordStop(int offset) override
    {
        if (offset >= Length())
            return true;

        return starts_character(m_text, offset) && IsWord(offset) && (offset == 0 || !IsWord(Prev(offset)));
    }

    bool IsWhiteSpace(int offset) override
    {
        return offset < Length() && (m_text[offset] == ' ' || m_text[offset] == '\t');
    }

    void SetSelection(int, int) override
    {
    }

    void SetSelectionColours(uint32_t, uint32_t) override
    {
    }

    void Draw(TextCanvas &, int, int, int, int, int, int, int, int) override
    {
    }

  private:
    int Next(int offset)
    {
        do
            offset++;
        while (offset < Length() && !starts_character(m_text, offset));

        return offset;
    }

    int Prev(int offset)
    {
        do
            offset--;
        while (offset > 0 && !starts_character(m_text, offset));

        return offset;
    }

    bool IsWord(int offset)
    {
        tchar_t ch = m_text[offset];

        return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_' ||
               (uint32_t)(std::make_unsigned<tchar_t>::type)ch >= 0x80;
    }

    tstring          m_text;
    std::vector<int> m_x;
};

class FakeCanvas : public TextCanvas
{
  public:
    void BeginRow(int, int, int) override
    {
    }

    void EndRow() override
    {
    }

    void FillRect(int, int, int, int, uint32_t) override
    {
    }

    void FillChecked(int, int, int, int, uint32_t, uint32_t) override
    {
    }

    int MarginTextWidth(const tchar_t *, int length) override
    {
        return length * CELL_WIDTH;
    }

    void DrawMarginText(int, int, const tchar_t *, int, uint32_t, uint32_t, int, int, int, int) override
    {
    }

    void DrawMarginImage(int, int, int, int, int) override
    {
    }
};

//
//	A host with nothing behind it: a 640 by 480 view with the focus, one font,
//	a clipboard of its own, and the notifications it was sent
//
class FakeHost : public TextViewHost
{
  public:
    unsigned              modifiers = 0;
    tstring               clipboard;
    std::vector<unsigned> notified;

    void Invalidate() override
    {
    }

    void InvalidateRect(int, int, int, int) override
    {
    }

    void UpdateNow() override
    {
    }

    void ScrollPixels(int, int, int, int, int, int) override
    {
    }

    void SetVScroll(uint64_t, uint64_t, uint64_t) override
    {
    }

    uint64_t VScrollTrackPos(uint64_t) override
    {
        return 0;
    }

    void SetHScroll(int, int, int) override
    {
    }

    int HScrollTrackPos() override
    {
        return 0;
    }

    void CreateCaret(int, int) override
    {
    }

    void DestroyCaret() override
    {
    }

    void ShowCaret() override
    {
    }

    void HideCaret() override
    {
    }

    void SetCaretPos(int, int) override
    {
    }

    bool StartTimer(unsigned, unsigned) override
    {
        return true;
    }

    void StopTimer(unsigned) override
    {
    }

    void MousePosition(int *x, int *y) override
    {
        *x = 0;
        *y = 0;
    }

    void CaptureMouse() override
    {
    }

    void ReleaseMouse() override
    {
    }

    unsigned Modifiers() override
    {
        return modifiers;
    }

    void SetCursor(Cursor) override
    {
    }

    void ClientSize(int *width, int *height) override
    {
        *width  = 640;
        *height = 480;
    }

    bool HasFocus() override
    {
        return true;
    }

    bool IsActive() override
    {
        return true;
    }

    int CaretWidth() override
    {
        return 0;
    }

    int WheelScrollLines() override
    {
        return 3;
    }

    uint32_t SystemColour(int index) override
    {
        return (uint32_t)index;
    }

    int FontCount() override
    {
        return 1;
    }

    void GetFontMetrics(int, FontMetrics *metrics) override
    {
        metrics->height       = LINE_HEIGHT;
        metrics->ascent       = 12;
        metrics->averageWidth = CELL_WIDTH;
    }

    int TextWidth(const tchar_t *, int length) override
    {
        return length * CELL_WIDTH;
    }

    bool MarginImageSize(int *width, int *height) override
    {
        *width  = 0;
        *height = 0;
        return false;
    }

    TextLayout *CreateLayout() override
    {
        return new FakeLayout;
    }

    bool ReadClipboard(const std::function<void(const tchar_t *text, size_t length)> &use) override
    {
        if (clipboard.empty())
            return false;

        use(clipboard.data(), clipboard.size());
        return true;
    }

    bool WriteClipboard(size_t length, const std::function<size_t(tchar_t *buffer)> &fill) override
    {
        std::vector<tchar_t> buffer(length + 1);

        clipboard.assign(buffer.data(), fill(buffer.data()));
        return true;
    }

    intptr_t Notify(unsigned code, void *) override
    {
        notified.push_back(code);
        return 0;
    }
};

//
//	A view of a file holding text, in an encoding: the engine, its host, and
//	the file, which goes when the view does
//
struct EngineView
{
    FakeHost    host;
    TextView   *view;
    seqfilechar path[TEST_PATH_MAX];

    EngineView(const char32_t *text, TEXT_ENCODING encoding = NCP_UTF8)
    {
        std::string bytes = encode_text(text, encoding);

        view = new TextView(&host);
        view->OnSize(0, 640, 480);

        CHECK(write_temp_file(bytes.data(), bytes.size(), path));
        CHECK(view->OpenFile(path, -1, false));
    }

    ~EngineView()
    {
        delete view;
        delete_file(path);
    }

    void key(uint32_t key, unsigned modifiers = 0)
    {
        host.modifiers = modifiers;
        view->OnKeyDown(key, 0);
        host.modifiers = 0;
    }

    size_w column()
    {
        TextViewCursorInfo info;

        view->FillCursorInfo(&info);
        return info.nColumnNo;
    }

    tstring text()
    {
        TextDocument        *doc = view->FileDoc();
        std::vector<tchar_t> buf(static_cast<size_t>(doc->byte_length()) * 3 + 1);
        TextReader           reader = doc->text_from_range(TextPos(0), TextPos(doc->byte_length()));

        return tstring(buf.data(), reader.read(buf.data(), (uint32_t)buf.size()));
    }
};

// The units the start of some text takes: the column after it
size_w columns(const char32_t *text)
{
    return tstr(text).size();
}

void engine_arrows_step_over_whole_characters()
{
    EngineView v(U"a\u00e9\U0001F600b");

    CHECK(v.column() == 0);

    v.key(TVK_RIGHT);
    CHECK(v.column() == columns(U"a"));
    v.key(TVK_RIGHT);
    CHECK(v.column() == columns(U"a\u00e9"));
    v.key(TVK_RIGHT);
    CHECK(v.column() == columns(U"a\u00e9\U0001F600"));
    v.key(TVK_RIGHT);
    CHECK(v.column() == columns(U"a\u00e9\U0001F600b"));

    v.key(TVK_LEFT);
    CHECK(v.column() == columns(U"a\u00e9\U0001F600"));
    v.key(TVK_LEFT);
    CHECK(v.column() == columns(U"a\u00e9"));

    v.key(TVK_HOME);
    CHECK(v.column() == 0);
    v.key(TVK_END);
    CHECK(v.column() == columns(U"a\u00e9\U0001F600b"));
}

void engine_word_moves_stop_between_characters()
{
    EngineView v(U"\u00e9t\u00e9 caf\u00e9 x");

    v.key(TVK_RIGHT, TextViewHost::TVMOD_CTRL);
    CHECK(v.column() == columns(U"\u00e9t\u00e9 "));
    v.key(TVK_RIGHT, TextViewHost::TVMOD_CTRL);
    CHECK(v.column() == columns(U"\u00e9t\u00e9 caf\u00e9 "));
    v.key(TVK_LEFT, TextViewHost::TVMOD_CTRL);
    CHECK(v.column() == columns(U"\u00e9t\u00e9 "));
    v.key(TVK_LEFT, TextViewHost::TVMOD_CTRL);
    CHECK(v.column() == 0);
}

void engine_deletes_whole_characters()
{
    EngineView v(U"a\u00e9\U0001F600b");

    // backspace from the end: b, then the face, as a whole
    v.key(TVK_END);
    v.key(TVK_BACK);
    v.key(TVK_BACK);
    CHECK(v.text() == tstr(U"a\u00e9"));

    // delete from the start: a, then the e
    v.key(TVK_HOME);
    v.key(TVK_DELETE);
    CHECK(v.text() == tstr(U"\u00e9"));
    v.key(TVK_DELETE);
    CHECK(v.text() == tstr(U""));
}

void engine_selects_and_copies_whole_characters()
{
    // UTF-16 text decodes to more UTF-8 units than its bytes: the copy has room
    EngineView v(U"\u4e2d\u4e2d\U0001F600", NCP_UTF16);

    v.key(TVK_RIGHT, TextViewHost::TVMOD_SHIFT);
    CHECK(v.view->SelectionSize() == 2);

    v.view->SelectAll();
    CHECK(v.view->OnCopy());
    CHECK(v.host.clipboard == tstr(U"\u4e2d\u4e2d\U0001F600"));

    // and pasted at the end, it's all there twice
    v.key(TVK_END);
    CHECK(v.view->OnPaste());
    CHECK(v.text() == tstr(U"\u4e2d\u4e2d\U0001F600\u4e2d\u4e2d\U0001F600"));
}

void engine_overwrites_a_character_with_a_character()
{
    EngineView v(U"abc");
    tstring    e_acute = tstr(U"\u00e9");
    tstring    face    = tstr(U"\U0001F600");

    v.view->SetEditMode(MODE_OVERWRITE);

    // typed in place of one character each, however many units they take
    v.view->OnText(e_acute.c_str(), (uint32_t)e_acute.size());
    CHECK(v.text() == tstr(U"\u00e9bc"));
    v.view->OnText(face.c_str(), (uint32_t)face.size());
    CHECK(v.text() == tstr(U"\u00e9\U0001F600c"));
    CHECK(v.column() == columns(U"\u00e9\U0001F600"));
}

void engine_tab_spaces_count_characters()
{
    EngineView v(U"\u00e9\u00e9");

    // two characters in, a tab stop every 4: two spaces reach it
    v.view->SetStyle(TXS_INSERTSPACES, TXS_INSERTSPACES);
    v.key(TVK_END);
    v.view->OnText(TSTR("\t"), 1);
    CHECK(v.text() == tstr(U"\u00e9\u00e9  "));
}

void engine_types_and_tells_its_host()
{
    EngineView v(U"");
    tstring    text = tstr(U"caf\u00e9");

    v.view->OnText(text.c_str(), (uint32_t)text.size());
    CHECK(v.text() == text);
    CHECK(v.column() == text.size());
    CHECK(std::find(v.host.notified.begin(), v.host.notified.end(), (unsigned)TVNOTIFY_CHANGED) !=
          v.host.notified.end());
}

void engine_types_into_a_new_view()
{
    FakeHost host;
    TextView view(&host);
    tstring  text = tstr(U"caf\u00e9");

    // no file opened: the view starts with an empty document
    view.OnSize(0, 640, 480);
    view.OnText(text.c_str(), (uint32_t)text.size());
    CHECK(view.FileDoc()->byte_length() == 5);
}

void engine_paints_every_line()
{
    EngineView v(U"one\r\n\u00e9\t\U0001F600\r\n\u4e2d\u4e2d");
    FakeCanvas canvas;
    TextRect   rect = {0, 0, 640, 480};

    v.view->SetStyle(TXS_LINENUMBERS | TXS_SELMARGIN, TXS_LINENUMBERS | TXS_SELMARGIN);
    v.view->SelectAll();
    v.view->Paint(canvas, &rect);
    CHECK(v.view->SelectionSize() == v.view->FileDoc()->byte_length());
}

// Every match highlighted, regardless of case, over whole characters: the
// UTF-8 build compares as UTF-16, as find does
void engine_highlights_matches_regardless_of_case()
{
    EngineView   v(U"un \u00c9clair, deux \u00e9clairs");
    FakeCanvas   canvas;
    TextRect     rect    = {0, 0, 640, 480};
    tstring      pattern = tstr(U"\u00e9CLAIR");
    TextViewFind find    = {};
    size_t       marked  = 0;

    find.text      = pattern.c_str();
    find.length    = (uint32_t)pattern.size();
    find.matchcase = false;

    v.view->ShowMatches(&find, false);
    v.view->Paint(canvas, &rect);

    for (uint32_t bg : last_backgrounds)
        marked += bg == TV_RGB(255, 230, 128);

    CHECK(marked == 2 * tstr(U"\u00e9clair").size());
}

//
//	Syntax colouring: a comment begun on one line colours the next, which is
//	the last laid out. The language is chosen by the file's name.
//
void engine_colours_syntax_across_lines()
{
    EngineView  v(U"int a; /* one\ntwo */ b");
    FakeCanvas  canvas;
    TextRect    rect    = {0, 0, 640, 480};
    uint32_t    comment = TV_RGB(0, 128, 0);
    const char *c       = "# neatpad-syntax-v1\n"
                          "language C\n"
                          "files *.c *.h\n"
                          "context main\n"
                          "    \"/*\"  comment  enter block-comment\n"
                          "context block-comment  comment\n"
                          "    \"*/\"  comment  leave\n";
    const char *log     = "# neatpad-syntax-v1\n"
                          "language Log\n"
                          "files *.log\n"
                          "context main\n";
    const char *const definitions[] = { c, log };

    CHECK(v.view->ChooseSyntax(definitions, 2, "/var/log/server.LOG") == 1);
    CHECK(v.view->ChooseSyntax(definitions, 2, "notes.txt") == -1);
    CHECK(v.view->ChooseSyntax(definitions, 2, "src/main.c") == 0);

    v.view->Paint(canvas, &rect);

    CHECK(last_foregrounds.size() >= 8);
    CHECK(last_foregrounds[0] == comment); // t
    CHECK(last_foregrounds[5] == comment); // the / of */
    CHECK(last_foregrounds[7] != comment); // b

    // a definition with a mistake isn't used
    CHECK(!v.view->SetSyntax("# neatpad-syntax-v1\nlanguage C\n"));
    CHECK(v.view->SetSyntax(0));
    v.view->Paint(canvas, &rect);
    CHECK(last_foregrounds[0] != comment);
}

// Unsaved edits saved as recovery state, and restored into a new view: the
// text, the file it's for, and the caret
//
//	A followed file is indexed as it opens, and its index takes in what's
//	appended, so the lines at its end have their numbers
//
void engine_indexes_a_followed_file()
{
    FakeHost          host;
    TextView         *view = new TextView(&host);
    seqfilechar       path[TEST_PATH_MAX];
    std::string       text;
    TextViewIndexInfo info;
    FILE             *fp;

    // longer than the part indexed as a file opens
    for (int i = 0; i < 600000; i++)
        text += "a log line\n";

    view->OnSize(0, 640, 480);
    CHECK(write_temp_file(text.data(), text.size(), path));
    CHECK(view->TailFile(path));

    view->FillIndexInfo(&info);
    CHECK(info.fIndexing);

    for (int i = 0; i < 10000 && info.nPercent < 100; i++)
    {
        view->OnTimer(INDEX_TIMER);
        view->FillIndexInfo(&info);
    }

    CHECK(info.nPercent == 100);
    CHECK(info.nLineCount == 600001);

    // appended lines are indexed too
    fp = open_file(path, "ab");
    CHECK(fp != 0);
    fputs("one more\nand another\n", fp);
    fclose(fp);

    CHECK(view->UpdateTail());
    view->FillIndexInfo(&info);
    CHECK(info.nPercent == 100);
    CHECK(info.nLineCount == 600003);

    delete view;
    delete_file(path);
}

//	A followed file, filtered: the caret stays at the end of the lines found,
//	while the filter is still finding them and as more are appended
void engine_follows_the_end_of_a_filtered_file()
{
    FakeHost     host;
    TextView    *view = new TextView(&host);
    seqfilechar  path[TEST_PATH_MAX];
    std::string  text;
    tstring      pattern = tstr(U"ERROR");
    TextViewFind find    = {};
    size_w       line    = 0;
    FILE        *fp;

    // long enough for the filter to take more than one slice
    for (int i = 0; i < 600000; i++)
        text += "2026-11-17 06:00:00 ERROR [exec-1] Pool: Timeout waiting for a connection\n";

    find.text      = pattern.c_str();
    find.length    = (uint32_t)pattern.size();
    find.matchcase = true;

    view->OnSize(0, 640, 480);
    CHECK(write_temp_file(text.data(), text.size(), path));
    view->ShowMatches(&find, true);
    CHECK(view->TailFile(path));

    for (int i = 0; i < 100000; i++)
        view->OnTimer(FILTER_TIMER);

    CHECK(view->CursorLine(&line) == TDR_OK);
    CHECK(line == 600000);
    CHECK(view->SelectionSize() == 0);

    fp = open_file(path, "ab");
    CHECK(fp != 0);
    fputs("2026-11-17 06:00:01 ERROR [exec-2] one more\n", fp);
    fclose(fp);

    CHECK(view->UpdateTail());

    for (int i = 0; i < 100; i++)
        view->OnTimer(FILTER_TIMER);

    CHECK(view->CursorLine(&line) == TDR_OK);
    CHECK(line == 600001);

    // the filter ended and started again, the caret still at the end
    for (int i = 0; i < 10000; i++)
        view->OnTimer(INDEX_TIMER);

    view->ShowMatches(0, false);
    CHECK(view->CursorLine(&line) == TDR_OK);
    CHECK(line == 600001);
    CHECK(view->SelectionSize() == 0);

    view->ShowMatches(&find, true);

    for (int i = 0; i < 100000; i++)
        view->OnTimer(FILTER_TIMER);

    CHECK(view->CursorLine(&line) == TDR_OK);
    CHECK(line == 600001);

    delete view;
    delete_file(path);
}

//	Filtering keeps the caret's place: at the first line shown from its line on,
//	once the filter has found it, rather than at the top
void engine_filter_keeps_the_caret_place()
{
    FakeHost     host;
    TextView    *view = new TextView(&host);
    seqfilechar  path[TEST_PATH_MAX];
    std::string  text;
    tstring      pattern = tstr(U"ERROR");
    TextViewFind find    = {};
    size_w       line    = 0;

    // every tenth line matches; long enough to take more than one slice
    for (int i = 0; i < 600000; i++)
        text += "a line of the log, number " + std::to_string(i) + (i % 10 == 0 ? " ERROR\n" : "\n");

    find.text      = pattern.c_str();
    find.length    = (uint32_t)pattern.size();
    find.matchcase = true;

    view->OnSize(0, 640, 480);
    CHECK(write_temp_file(text.data(), text.size(), path));
    CHECK(view->OpenFile(path, -1, false));
    view->SetFilterContext(0);

    for (int i = 0; i < 10000; i++)
        view->OnTimer(INDEX_TIMER);

    CHECK(view->GotoLine(300005, true) == TDR_OK);
    view->ShowMatches(&find, true);

    for (int i = 0; i < 100000; i++)
        view->OnTimer(FILTER_TIMER);

    // the match on line 300010 is the 30001st shown
    CHECK(view->CursorLine(&line) == TDR_OK);
    CHECK(line == 30001);

    // typed a character at a time, each filter starting before the last has
    // found the place: the place is still kept
    view->ShowMatches(0, false);
    CHECK(view->GotoLine(300005, true) == TDR_OK);

    for (size_t length = 1; length <= pattern.size(); length++)
    {
        find.length = (uint32_t)length;
        view->ShowMatches(&find, true);
    }

    for (int i = 0; i < 100000; i++)
        view->OnTimer(FILTER_TIMER);

    CHECK(view->CursorLine(&line) == TDR_OK);
    CHECK(line == 30001);

    delete view;
    delete_file(path);
}

void engine_restores_its_recovery_state()
{
    EngineView                     v(U"one two");
    std::filesystem::path          dir = std::filesystem::temp_directory_path() / "neatpad-recovery-test";
    std::basic_string<seqfilechar> store;
    TextViewRecovery               state = {};
    tstring                        edit  = tstr(U" \u00e9");

    std::filesystem::remove_all(dir);
    CHECK(std::filesystem::create_directory(dir));
    store = dir.native();

    v.key(TVK_END);
    v.view->OnText(edit.c_str(), (uint32_t)edit.size());

    state.directory = store.c_str();
    state.filename  = v.path;
    CHECK(v.view->SaveState(&state));

    {
        FakeHost         host;
        TextView         restored(&host);
        TextViewRecovery loaded = {};

        restored.OnSize(0, 640, 480);
        loaded.directory = store.c_str();
        CHECK(restored.LoadState(&loaded));
        CHECK(loaded.filename == v.path);
        CHECK(restored.FileDoc()->modified());
        CHECK(restored.CursorColumn() == tstr(U"one two \u00e9").size());

        std::vector<tchar_t> buf(64);
        TextDocument        *doc    = restored.FileDoc();
        TextReader           reader = doc->text_from_range(TextPos(0), TextPos(doc->byte_length()));

        CHECK(tstring(buf.data(), reader.read(buf.data(), (uint32_t)buf.size())) == tstr(U"one two \u00e9"));
    }

    std::filesystem::remove_all(dir);
}

const test_case tests[] =
{
    { "engine_arrows_step_over_whole_characters", engine_arrows_step_over_whole_characters },
    { "engine_word_moves_stop_between_characters", engine_word_moves_stop_between_characters },
    { "engine_deletes_whole_characters", engine_deletes_whole_characters },
    { "engine_selects_and_copies_whole_characters", engine_selects_and_copies_whole_characters },
    { "engine_overwrites_a_character_with_a_character", engine_overwrites_a_character_with_a_character },
    { "engine_indexes_a_followed_file", engine_indexes_a_followed_file },
    { "engine_follows_the_end_of_a_filtered_file", engine_follows_the_end_of_a_filtered_file },
    { "engine_filter_keeps_the_caret_place", engine_filter_keeps_the_caret_place },
    { "engine_tab_spaces_count_characters", engine_tab_spaces_count_characters },
    { "engine_types_and_tells_its_host", engine_types_and_tells_its_host },
    { "engine_types_into_a_new_view", engine_types_into_a_new_view },
    { "engine_paints_every_line", engine_paints_every_line },
    { "engine_restores_its_recovery_state", engine_restores_its_recovery_state },
    { "engine_highlights_matches_regardless_of_case", engine_highlights_matches_regardless_of_case },
    { "engine_colours_syntax_across_lines", engine_colours_syntax_across_lines },
};
}

const test_group engine_tests = { "engine", tests, sizeof(tests) / sizeof(tests[0]) };
