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

//
//	A line laid out a character to a cell, with tab stops every tabWidth cells.
//	Cursor and word stops are at character starts, never inside one.
//
class FakeLayout : public TextLayout
{
  public:
    bool Analyze(const tchar_t *text, int length, TextRun *, int tabWidth) override
    {
        int x = 0;

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
    tstring  text = tstr(U"café");

    // no file opened: the view starts with an empty document
    view.OnSize(0, 640, 480);
    view.OnText(text.c_str(), (uint32_t)text.size());
    CHECK(view.FileDoc()->byte_length() == 5);

    if (view.FileDoc()->byte_length() != 5)
        printf("::error::new view: %d bytes, %d lines, column %d, %d units typed\n",
               (int)view.FileDoc()->byte_length(), (int)view.FileDoc()->linecount(),
               (int)view.CursorColumn(), (int)text.size());
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

const test_case tests[] =
{
    { "engine_arrows_step_over_whole_characters", engine_arrows_step_over_whole_characters },
    { "engine_word_moves_stop_between_characters", engine_word_moves_stop_between_characters },
    { "engine_deletes_whole_characters", engine_deletes_whole_characters },
    { "engine_selects_and_copies_whole_characters", engine_selects_and_copies_whole_characters },
    { "engine_overwrites_a_character_with_a_character", engine_overwrites_a_character_with_a_character },
    { "engine_tab_spaces_count_characters", engine_tab_spaces_count_characters },
    { "engine_types_and_tells_its_host", engine_types_and_tells_its_host },
    { "engine_types_into_a_new_view", engine_types_into_a_new_view },
    { "engine_paints_every_line", engine_paints_every_line },
};
}

const test_group engine_tests = { "engine", tests, sizeof(tests) / sizeof(tests[0]) };
