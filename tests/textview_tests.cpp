//
//	TextView tests: the EDIT control messages, compared with the answers of a
//	real edit control, and the commands that change the text. The windows are
//	never shown.
//
#include "tests.h"
#include "../src/TextView/sequence_state.h"
#include <tchar.h>
#include <shellapi.h>
#include "../src/Neatpad/Neatpad.h"

// Minimal host for the real C recovery controller. Controls remain hidden,
// and RECOVERY_TESTS routes all store I/O to a disposable test directory.
HWND         g_hwndTextView, g_hwndMain;
TCHAR        g_szFileName[MAX_PATH], g_szFileTitle[MAX_PATH];
static TCHAR recovery_test_root[MAX_PATH];
static UINT  recovery_notice;

extern "C" BOOL RecoveryTestRoot(TCHAR *directory)
{
    if (!recovery_test_root[0])
        return FALSE;
    lstrcpy(directory, recovery_test_root);
    return TRUE;
}

void ShowNotifyBar(UINT notice)
{
    recovery_notice = notice;
}

BOOL NeatpadReloadFile(HWND)
{
    return FALSE;
}

void ShowRecoveredFile(HWND, const TCHAR *filename)
{
    const TCHAR *title = _tcsrchr(filename, _T('\\'));
    lstrcpy(g_szFileName, filename);
    lstrcpy(g_szFileTitle, title ? title + 1 : filename);
    ShowNotifyBar(filename[0] ? NOTIFY_RECOVERED : NOTIFY_RECOVERED_UNTITLED);
}

namespace
{
// The last index notification a TextView sent its parent, and how many times
// it has said its text changed
TVNINDEXINFO g_index_notified;
int          g_changes_notified;

LRESULT CALLBACK HiddenParentProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if(msg == WM_NOTIFY && reinterpret_cast<NMHDR *>(lParam)->code == TVN_INDEX_CHANGE)
        g_index_notified = *reinterpret_cast<TVNINDEXINFO *>(lParam);

    if(msg == WM_NOTIFY && reinterpret_cast<NMHDR *>(lParam)->code == TVN_CHANGED)
    {
        g_changes_notified++;
        if (reinterpret_cast<NMHDR *>(lParam)->hwndFrom == g_hwndTextView)
            RecoveryChanged();
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// A window to hold controls under test. It's never shown, so neither are they.
HWND create_hidden_parent()
{
    static ATOM registered = 0;

    if(registered == 0)
    {
        WNDCLASS wc = { 0 };

        wc.lpfnWndProc   = HiddenParentProc;
        wc.hInstance     = GetModuleHandle(0);
        wc.lpszClassName = TEXT("SequenceTestsParent");
        registered = RegisterClass(&wc);
    }

    return CreateWindowEx(0, TEXT("SequenceTestsParent"), TEXT(""), WS_POPUP, 0, 0, 640, 480, 0, 0, GetModuleHandle(0), 0);
}

HWND create_textview(HWND parent)
{
    static bool registered = InitTextView() != FALSE;
    HWND view = registered ? CreateTextView(parent) : 0;

    if(view)
        MoveWindow(view, 0, 0, 640, 480, FALSE);

    return view;
}

// A TextView and a standard EDIT control holding the same text, to compare
// their answers to the edit messages
struct EditPair
{
    HWND   parent;
    HWND   view;
    HWND   edit;
    TCHAR  path[MAX_PATH];
};

bool open_edit_pair(EditPair *pair, const wchar_t *text, TEXT_ENCODING encoding)
{
    std::string bytes = encode_text(text, encoding);

    pair->parent = create_hidden_parent();
    pair->view   = create_textview(pair->parent);
    pair->edit   = CreateWindowEx(0, TEXT("EDIT"), TEXT(""), WS_CHILD | WS_HSCROLL | WS_VSCROLL | ES_MULTILINE | ES_AUTOHSCROLL | ES_AUTOVSCROLL,
                                  0, 0, 640, 480, pair->parent, 0, GetModuleHandle(0), 0);

    if(pair->view == 0 || pair->edit == 0 || !write_temp_file(bytes.data(), bytes.size(), pair->path))
        return false;

    SetWindowTextW(pair->edit, text);
    return TextView_OpenFile(pair->view, pair->path) != 0;
}

void close_edit_pair(EditPair *pair)
{
    DestroyWindow(pair->parent);
    DeleteFile(pair->path);
}

// One message sent to both controls, and their answers compared
void expect_same_answer(const EditPair &pair, UINT msg, WPARAM wParam, const char *name)
{
    LRESULT view = SendMessage(pair.view, msg, wParam, 0);
    LRESULT edit = SendMessage(pair.edit, msg, wParam, 0);

    if(view != edit)
    {
        printf("%s(%d): TextView %lld, EDIT %lld\n", name, static_cast<int>(wParam), static_cast<long long>(view), static_cast<long long>(edit));
        g_failures++;
    }
}

// The text one message copies into a buffer of size characters, from both
// controls: their counts and what they copy
void expect_same_text(const EditPair &pair, UINT msg, WPARAM wParam, size_t size, const char *name)
{
    std::vector<wchar_t> view(size + 2, L'#');
    std::vector<wchar_t> edit(size + 2, L'#');
    LRESULT view_len;
    LRESULT edit_len;

    // EM_GETLINE takes the buffer's size in its first WORD
    if(msg == EM_GETLINE)
    {
        view[0] = static_cast<wchar_t>(size);
        edit[0] = static_cast<wchar_t>(size);
    }

    view_len = SendMessage(pair.view, msg, wParam, (LPARAM)&view[0]);
    edit_len = SendMessage(pair.edit, msg, wParam, (LPARAM)&edit[0]);

    // the EDIT control can stop between the halves of a surrogate pair, where
    // a TextView stops before the pair
    if(edit_len > 0 && edit[edit_len - 1] >= 0xd800 && edit[edit_len - 1] <= 0xdbff)
        edit_len--;

    if(view_len != edit_len || wmemcmp(&view[0], &edit[0], static_cast<size_t>(edit_len)) != 0)
    {
        printf("%s(%d, size %d): TextView %lld, EDIT %lld\n", name, static_cast<int>(wParam), static_cast<int>(size),
               static_cast<long long>(view_len), static_cast<long long>(edit_len));
        g_failures++;
    }

    // WM_GETTEXT ends what it copies with a null
    if(msg == WM_GETTEXT && size > 0)
        CHECK(view[static_cast<size_t>(view_len)] == 0);
}

// The selection set on both, and read back from both
void expect_same_selection(const EditPair &pair, int start, int end)
{
    DWORD view_start = 0, view_end = 0;
    DWORD edit_start = 0, edit_end = 0;

    SendMessage(pair.view, EM_SETSEL, start, end);
    SendMessage(pair.edit, EM_SETSEL, start, end);

    CHECK(SendMessage(pair.view, EM_GETSEL, (WPARAM)&view_start, (LPARAM)&view_end) ==
          SendMessage(pair.edit, EM_GETSEL, (WPARAM)&edit_start, (LPARAM)&edit_end));
    CHECK(view_start == edit_start && view_end == edit_end);

    // and the messages that look at the caret or the selection
    expect_same_answer(pair, EM_LINEFROMCHAR, (WPARAM)-1, "EM_LINEFROMCHAR");
    expect_same_answer(pair, EM_LINEINDEX, (WPARAM)-1, "EM_LINEINDEX");
    expect_same_answer(pair, EM_LINELENGTH, (WPARAM)-1, "EM_LINELENGTH");
}

void textview_edit_messages_match_an_edit_control()
{
    // lines with characters of one, two and three UTF-8 bytes, a surrogate
    // pair (at 23 and 24), and an empty line
    const wchar_t text[] = L"Hello\r\ncaf\x00e9 \x20ac\r\n\r\nemoji \xd83d\xde00 end\r\nlast";
    const wchar_t *texts[] = { text, L"one\r\ntwo\r\n", L"" };
    const TEXT_ENCODING encodings[] = { NCP_UTF8, NCP_UTF16, NCP_UTF32 };
    const int selections[][2] =
    {
        { 0, 0 }, { 2, 5 }, { 5, 2 }, { 0, -1 }, { 3, 3 }, { 7, 22 }, { 22, 7 }, { -1, 4 },
        { 12, 23 }, { 25, 17 }, { 31, 100 }, { 100, 31 },
    };

    for(size_t t = 0; t < sizeof(texts) / sizeof(texts[0]); t++)
    {
        for(size_t e = 0; e < sizeof(encodings) / sizeof(encodings[0]); e++)
        {
            EditPair pair;
            size_t length = wcslen(texts[t]);
            LRESULT lines;

            CHECK(open_edit_pair(&pair, texts[t], encodings[e]));

            expect_same_answer(pair, WM_GETTEXTLENGTH, 0, "WM_GETTEXTLENGTH");
            expect_same_answer(pair, EM_GETLINECOUNT, 0, "EM_GETLINECOUNT");
            expect_same_answer(pair, EM_GETFIRSTVISIBLELINE, 0, "EM_GETFIRSTVISIBLELINE");

            // every character, and past the end: the screen positions only
            // where there's one, as fonts differ
            for(size_t i = 0; i <= length + 2; i++)
            {
                expect_same_answer(pair, EM_LINEFROMCHAR, i, "EM_LINEFROMCHAR");
                expect_same_answer(pair, EM_LINELENGTH, i, "EM_LINELENGTH");

                CHECK((SendMessage(pair.view, EM_POSFROMCHAR, i, 0) == -1) == (SendMessage(pair.edit, EM_POSFROMCHAR, i, 0) == -1));
            }

            // every line, and past the last
            lines = SendMessage(pair.edit, EM_GETLINECOUNT, 0, 0);

            for(LRESULT line = 0; line <= lines + 1; line++)
                expect_same_answer(pair, EM_LINEINDEX, (WPARAM)line, "EM_LINEINDEX");

            // the text, and every line, at every buffer size
            for(size_t size = 0; size <= length + 2; size++)
            {
                expect_same_text(pair, WM_GETTEXT, size, size, "WM_GETTEXT");

                for(LRESULT line = 0; line <= lines; line++)
                    expect_same_text(pair, EM_GETLINE, (WPARAM)line, size, "EM_GETLINE");
            }

            for(size_t s = 0; s < sizeof(selections) / sizeof(selections[0]); s++)
                expect_same_selection(pair, selections[s][0], selections[s][1]);

            close_edit_pair(&pair);
        }
    }
}

void textview_edit_selection_covers_whole_characters()
{
    // U+1F600 is the units at 7 and 8
    const TEXT_ENCODING encodings[] = { NCP_UTF8, NCP_UTF16, NCP_UTF32 };

    for(size_t e = 0; e < sizeof(encodings) / sizeof(encodings[0]); e++)
    {
        EditPair pair;
        DWORD start = 0, end = 0;

        CHECK(open_edit_pair(&pair, L"abc\r\nde\xd83d\xde00 f", encodings[e]));

        // the caret moves back to the start of the character
        SendMessage(pair.view, EM_SETSEL, 8, 8);
        SendMessage(pair.view, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
        CHECK(start == 7 && end == 7);

        // and a selection's far end forward past it, whichever way round
        SendMessage(pair.view, EM_SETSEL, 5, 8);
        SendMessage(pair.view, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
        CHECK(start == 5 && end == 9);

        SendMessage(pair.view, EM_SETSEL, 8, 5);
        SendMessage(pair.view, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
        CHECK(start == 5 && end == 9);

        CHECK(SendMessage(pair.view, EM_LINEFROMCHAR, 8, 0) == 1);

        close_edit_pair(&pair);
    }
}

// All of a control's text
std::wstring window_text(HWND hwnd)
{
    std::vector<wchar_t> buf(static_cast<size_t>(SendMessage(hwnd, WM_GETTEXTLENGTH, 0, 0)) + 1);

    SendMessage(hwnd, WM_GETTEXT, buf.size(), (LPARAM)&buf[0]);
    return std::wstring(&buf[0]);
}

void textview_edit_positions_round_trip()
{
    // 100 lines of 20 UTF-16 units: one, two, three and four-byte UTF-8
    // characters, with U+1F600's second half at 13 and the LF at 19
    const wchar_t line_text[] = L"line caf\x00e9 \x20ac \xd83d\xde00 end\r\n";
    const size_t  line_units = 20;
    std::wstring  text;
    EditPair      pair;
    RECT          client;

    for(int i = 0; i < 100; i++)
        text += line_text;

    CHECK(open_edit_pair(&pair, text.c_str(), NCP_UTF8));
    GetClientRect(pair.view, &client);

    // first at the top, then scrolled down ten lines, so the first lines are above the window
    for(int scroll = 0; scroll <= 10; scroll += 10)
    {
        size_t top;
        int    height;
        int    prev_x = 0;

        SendMessage(pair.view, EM_LINESCROLL, 0, scroll);
        top = static_cast<size_t>(SendMessage(pair.view, EM_GETFIRSTVISIBLELINE, 0, 0));
        CHECK(top == static_cast<size_t>(scroll));

        height = (short)HIWORD(SendMessage(pair.view, EM_POSFROMCHAR, (top + 1) * line_units, 0)) -
                 (short)HIWORD(SendMessage(pair.view, EM_POSFROMCHAR, top * line_units, 0));
        CHECK(height > 0);

        for(size_t i = 0; i < text.size(); i++)
        {
            size_t  line = i / line_units;
            size_t  column = i % line_units;
            LRESULT pos;
            int     x, y;

            // a character's second half, or the LF of a CRLF, starts nowhere of its own
            if(column == 13 || column == 19)
                continue;

            pos = SendMessage(pair.view, EM_POSFROMCHAR, i, 0);
            x = (short)LOWORD(pos);
            y = (short)HIWORD(pos);

            CHECK(y == ((int)line - (int)top) * height);
            CHECK(column == 0 || x > prev_x);
            prev_x = x;

            // where it's in the window, the point gives the character back, and its line
            if(y >= 0 && y + height <= client.bottom)
            {
                LRESULT at = SendMessage(pair.view, EM_CHARFROMPOS, 0, MAKELPARAM(x, y));

                CHECK(LOWORD(at) == i && HIWORD(at) == line);
            }
        }
    }

    // no character at or past the end
    CHECK(SendMessage(pair.view, EM_POSFROMCHAR, text.size(), 0) == -1);
    CHECK(SendMessage(pair.view, EM_POSFROMCHAR, text.size() + 1, 0) == -1);

    close_edit_pair(&pair);
}

void textview_edit_replacesel_matches_an_edit_control()
{
    const wchar_t text[] = L"Hello\r\ncaf\x00e9 \x20ac\r\n\r\nemoji \xd83d\xde00 end\r\nlast";
    const TEXT_ENCODING encodings[] = { NCP_UTF8, NCP_UTF16, NCP_UTF32 };
    const UINT modes[] = { MODE_INSERT, MODE_OVERWRITE, MODE_READONLY };

    // insert at the start, replace both ways round, insert at a caret,
    // delete, break a line, add after the caret alone, and replace everything
    struct { int start; int end; const wchar_t *with; } edits[] =
    {
        { 0, 0, L">" }, { 3, 6, L"XY\x00e9" }, { 12, 4, L"\xd83d\xde00" }, { 9, 9, L"z" },
        { 2, 8, L"" }, { 5, 5, L"A\r\nB" }, { -1, 0, L"!" }, { 0, -1, L"all\r\nnew" },
    };

    for(size_t e = 0; e < sizeof(encodings) / sizeof(encodings[0]); e++)
    {
        for(size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); m++)
        {
            EditPair pair;

            CHECK(open_edit_pair(&pair, text, encodings[e]));

            // a program replaces the text whatever the user could type
            TextView_SetEditMode(pair.view, modes[m]);
            SendMessage(pair.edit, EM_SETREADONLY, modes[m] == MODE_READONLY, 0);

            for(size_t i = 0; i < sizeof(edits) / sizeof(edits[0]); i++)
            {
                DWORD view_start = 0, view_end = 0;
                DWORD edit_start = 0, edit_end = 0;

                SendMessage(pair.view, EM_SETSEL, edits[i].start, edits[i].end);
                SendMessage(pair.edit, EM_SETSEL, edits[i].start, edits[i].end);
                SendMessage(pair.view, EM_REPLACESEL, TRUE, (LPARAM)edits[i].with);
                SendMessage(pair.edit, EM_REPLACESEL, TRUE, (LPARAM)edits[i].with);

                // the same text, and the caret after what went in
                CHECK(window_text(pair.view) == window_text(pair.edit));

                SendMessage(pair.view, EM_GETSEL, (WPARAM)&view_start, (LPARAM)&view_end);
                SendMessage(pair.edit, EM_GETSEL, (WPARAM)&edit_start, (LPARAM)&edit_end);
                CHECK(view_start == edit_start && view_end == edit_end);
            }

            // undo takes back the last replacement
            if(modes[m] != MODE_READONLY)
            {
                SendMessage(pair.view, EM_UNDO, 0, 0);
                SendMessage(pair.edit, EM_UNDO, 0, 0);
                CHECK(window_text(pair.view) == window_text(pair.edit));
            }

            close_edit_pair(&pair);
        }
    }

    // typed text then a replacement: undo takes back just the replacement
    {
        EditPair pair;

        CHECK(open_edit_pair(&pair, L"abc", NCP_UTF8));

        for(HWND hwnd : { pair.view, pair.edit })
        {
            SendMessage(hwnd, EM_SETSEL, 3, 3);
            SendMessage(hwnd, WM_CHAR, 'd', 0);
            SendMessage(hwnd, WM_CHAR, 'e', 0);
            SendMessage(hwnd, EM_REPLACESEL, TRUE, (LPARAM)L"X");
            SendMessage(hwnd, EM_UNDO, 0, 0);
        }

        CHECK(window_text(pair.view) == L"abcde");
        CHECK(window_text(pair.edit) == L"abcde");

        close_edit_pair(&pair);
    }
}

void textview_edit_scrolling_matches_an_edit_control()
{
    // 100 lines of six UTF-16 units
    const int    steps[] = { 5, -2, -1000, 30 };
    std::wstring text;
    EditPair     pair;

    for(int i = 0; i < 100; i++)
        text += L"line\r\n";

    CHECK(open_edit_pair(&pair, text.c_str(), NCP_UTF8));

    // the same lines scrolled through, up and down, and stopped at the top
    for(size_t s = 0; s < sizeof(steps) / sizeof(steps[0]); s++)
    {
        SendMessage(pair.view, EM_LINESCROLL, 0, steps[s]);
        SendMessage(pair.edit, EM_LINESCROLL, 0, steps[s]);
        expect_same_answer(pair, EM_GETFIRSTVISIBLELINE, 0, "EM_GETFIRSTVISIBLELINE");
    }

    // a caret on line 80, scrolled away from, comes back into view. A hidden
    // EDIT control doesn't scroll to its caret, so only the TextView shows it.
    {
        LRESULT first;

        SendMessage(pair.view, EM_SETSEL, 80 * 6, 80 * 6);
        SendMessage(pair.view, EM_LINESCROLL, 0, -1000);
        CHECK(SendMessage(pair.view, EM_GETFIRSTVISIBLELINE, 0, 0) == 0);

        SendMessage(pair.view, EM_SCROLLCARET, 0, 0);
        first = SendMessage(pair.view, EM_GETFIRSTVISIBLELINE, 0, 0);
        CHECK(first > 0 && first <= 80);
    }

    close_edit_pair(&pair);
}

void textview_edit_messages_index_as_they_need()
{
    // whole 12-byte patterns, so the text ends with a line break, and its
    // last line is empty
    std::string text = utf8_text((static_cast<size_t>(TextDocumentLineIndexProbe::open_index_length()) + 2 * 1024 * 1024) / 12 * 12);
    size_w chars = utf8_chars(text, text.size());
    size_w last_line_chars = utf8_chars(text, text.rfind('\n', text.size() - 2) + 1);
    size_t lines = std::count(text.begin(), text.end(), '\n') + 1;
    HWND parent = create_hidden_parent();
    HWND view = create_textview(parent);
    TVNINDEXINFO info = { 0 };
    TCHAR path[MAX_PATH];

    CHECK(write_temp_file(text.data(), text.size(), path));
    CHECK(view != 0 && TextView_OpenFile(view, path));

    // opening indexes only the start
    CHECK(TextView_GetIndexInfo(view, &info) && info.nPercent < 100);
    g_index_notified.nPercent = 0;

    // reading from the start needs no more of it: 1000 characters are 142
    // patterns of 7 UTF-16 units, and 6 more
    {
        std::wstring expected(1000, 0);
        std::vector<wchar_t> head(1001);
        UINT percent = info.nPercent;

        MultiByteToWideChar(CP_UTF8, 0, text.data(), 143 * 12, &expected[0], 1000);
        CHECK(SendMessage(view, WM_GETTEXT, head.size(), (LPARAM)&head[0]) == 1000);
        CHECK(std::wstring(&head[0]) == expected);
        CHECK(TextView_GetIndexInfo(view, &info) && info.nPercent == percent);
    }

    // a message about the last line with text on it indexes as far as it
    // needs, answers exactly, and says how far indexing has got, as the timer would
    CHECK(SendMessage(view, EM_LINEFROMCHAR, (WPARAM)(last_line_chars + 1), 0) == static_cast<LRESULT>(lines - 2));
    CHECK(TextView_GetIndexInfo(view, &info) && info.nPercent == 100);
    CHECK(g_index_notified.nPercent == 100);

    CHECK(SendMessage(view, EM_LINEINDEX, lines - 2, 0) == static_cast<LRESULT>(last_line_chars));
    CHECK(SendMessage(view, EM_LINEFROMCHAR, (WPARAM)chars, 0) == static_cast<LRESULT>(lines - 1));

    // its text, the pattern without the line break: a, e-acute, the euro sign and U+1F600
    {
        wchar_t line[16] = { 16 };

        CHECK(SendMessage(view, EM_GETLINE, lines - 2, (LPARAM)line) == 5);
        CHECK(wmemcmp(line, L"a\x00e9\x20ac\xd83d\xde00", 5) == 0);
    }
    CHECK(SendMessage(view, EM_GETLINECOUNT, 0, 0) == static_cast<LRESULT>(lines));
    CHECK(SendMessage(view, WM_GETTEXTLENGTH, 0, 0) == static_cast<LRESULT>(chars));

    DestroyWindow(parent);
    DeleteFile(path);
}

void textview_goto_line_reports_indexing()
{
    std::string text = utf8_text(static_cast<size_t>(TextDocumentLineIndexProbe::open_index_length()) + 2 * 1024 * 1024);
    size_t indexed_lines = static_cast<size_t>(TextDocumentLineIndexProbe::open_index_length()) / 12;
    HWND parent = create_hidden_parent();
    HWND view = create_textview(parent);
    TVNINDEXINFO before = { 0 };
    TVNINDEXINFO after = { 0 };
    TCHAR path[MAX_PATH];

    CHECK(write_temp_file(text.data(), text.size(), path));
    CHECK(view != 0 && TextView_OpenFile(view, path));
    CHECK(TextView_GetIndexInfo(view, &before) && before.nPercent < 100);
    g_index_notified.nPercent = 0;

    // a line a little past the indexed part is indexed there and then, and the
    // parent hears how far indexing has got
    CHECK(TextView_GotoLine(view, indexed_lines + 1000) == TDR_OK);
    CHECK(TextView_GetIndexInfo(view, &after) && after.nPercent > before.nPercent);
    CHECK(g_index_notified.nPercent == after.nPercent);

    DestroyWindow(parent);
    DeleteFile(path);
}

void textview_converts_line_breaks()
{
    // CRLF from its first line, with an LF and a CR mixed in
    const wchar_t mixed[] = L"one\r\ntwo\nthree\rfour\r\nfive";
    EditPair pair;
    DWORD    start = 0, end = 0;
    int      changes;

    CHECK(open_edit_pair(&pair, mixed, NCP_UTF8));
    CHECK(TextView_GetLineFormat(pair.view) == TXL_CRLF);

    // normalize the whole file with the caret on the line "four", at its 'u'
    SendMessage(pair.view, EM_SETSEL, 17, 17);
    changes = g_changes_notified;

    CHECK(TextView_ConvertLineBreaks(pair.view, TXL_CRLF));
    CHECK(window_text(pair.view) == L"one\r\ntwo\r\nthree\r\nfour\r\nfive");
    CHECK(g_changes_notified > changes);

    // the caret stays on its line and column, which are now two characters on
    CHECK(SendMessage(pair.view, EM_LINEFROMCHAR, (WPARAM)-1, 0) == 3);
    SendMessage(pair.view, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
    CHECK(start == 19 && end == 19);

    // one undo brings the mix back
    SendMessage(pair.view, EM_UNDO, 0, 0);
    CHECK(window_text(pair.view) == mixed);

    // the whole file converted to UNIX takes that line format
    SendMessage(pair.view, EM_SETSEL, 0, 0);
    CHECK(TextView_ConvertLineBreaks(pair.view, TXL_LF));
    CHECK(window_text(pair.view) == L"one\ntwo\nthree\nfour\nfive");
    CHECK(TextView_GetLineFormat(pair.view) == TXL_LF);

    // a selection, made right to left, converts only its own line breaks, stays
    // selected as it grows, and leaves the format alone: "two\nthree\n" here
    SendMessage(pair.view, EM_SETSEL, 14, 4);
    CHECK(TextView_ConvertLineBreaks(pair.view, TXL_CRLF));
    CHECK(window_text(pair.view) == L"one\ntwo\r\nthree\r\nfour\nfive");
    CHECK(TextView_GetLineFormat(pair.view) == TXL_LF);
    SendMessage(pair.view, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
    CHECK(start == 4 && end == 16);

    // a read-only view converts nothing
    TextView_SetEditMode(pair.view, MODE_READONLY);
    CHECK(!TextView_ConvertLineBreaks(pair.view, TXL_CR));
    CHECK(window_text(pair.view) == L"one\ntwo\r\nthree\r\nfour\nfive");

    // and a program's replacement says the text changed, as typing does
    changes = g_changes_notified;
    SendMessage(pair.view, EM_REPLACESEL, TRUE, (LPARAM)L"x");
    CHECK(g_changes_notified > changes);

    close_edit_pair(&pair);
}

void textview_filters_lines_and_goes_back_to_them()
{
    HWND         parent = create_hidden_parent();
    HWND         view   = create_textview(parent);
    TCHAR        path[MAX_PATH];
    TCHAR        error[] = TEXT("error");
    TEXTVIEWFIND find = { 0 };
    const char   more[] = "\r\nerror three\r\n";
    HANDLE       writer;
    DWORD        written = 0;

    // a log being followed, filtered for its errors
    CHECK(write_textdocument_file("start\r\nerror one\r\nfine\r\nerror two\r\nlast", DOC_ASCII, path));
    CHECK(TextView_TailFile(view, path));

    find.text   = error;
    find.length = 5;
    TextView_ShowMatches(view, &find, TRUE);
    CHECK(window_text(view) == L"error one\r\nerror two\r\n");

    // what's appended, and matches, is added
    writer = CreateFile(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, 0, 0);
    CHECK(writer != INVALID_HANDLE_VALUE);
    CHECK(WriteFile(writer, more, (DWORD)strlen(more), &written, 0));
    CloseHandle(writer);

    CHECK(TextView_UpdateTail(view));
    CHECK(window_text(view) == L"error one\r\nerror two\r\nerror three\r\n");

    // back to the whole file, on the line picked
    SendMessage(view, EM_SETSEL, 11, 11);
    TextView_ShowMatches(view, &find, FALSE);
    CHECK(window_text(view) == L"start\r\nerror one\r\nfine\r\nerror two\r\nlast\r\nerror three\r\n");
    CHECK(SendMessage(view, EM_LINEFROMCHAR, (WPARAM)-1, 0) == 3);

    DestroyWindow(parent);
    DeleteFile(path);
}

// Recovery tests use the same hidden controls as the other view tests.
struct RecoveryStore
{
    TCHAR path[MAX_PATH];

    RecoveryStore()
    {
        CHECK(write_temp_file("", 0, path));
        CHECK(DeleteFile(path));
        CHECK(CreateDirectory(path, 0));
    }

    ~RecoveryStore()
    {
        TCHAR           pattern[MAX_PATH], file[MAX_PATH];
        WIN32_FIND_DATA data;
        wsprintf(pattern, TEXT("%s\\*"), path);
        HANDLE find = FindFirstFile(pattern, &data);
        if (find != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                {
                    wsprintf(file, TEXT("%s\\%s"), path, data.cFileName);
                    DeleteFile(file);
                }
            } while (FindNextFile(find, &data));
            FindClose(find);
        }
        RemoveDirectory(path);
    }
};

void textview_recovers_encoding_selection_and_both_histories()
{
    for (TEXT_ENCODING encoding : {NCP_UTF8, NCP_UTF16, NCP_UTF16BE, NCP_UTF32, NCP_UTF32BE})
    {
        RecoveryStore store;
        EditPair      pair = {};
        CHECK(open_edit_pair(&pair, L"one\r\ntwo \x03bb\r\nthree", encoding));
        TextView_SetInMemoryLimit(pair.view, 1024 * 1024);
        CHECK(TextView_OpenFile(pair.view, pair.path));
        SendMessage(pair.view, EM_SETSEL, 0, 3);
        SendMessage(pair.view, EM_REPLACESEL, TRUE, (LPARAM)L"first");
        SendMessage(pair.view, EM_SETSEL, 0, 0);
        SendMessage(pair.view, EM_REPLACESEL, TRUE, (LPARAM)L"extra");
        TextView_Undo(pair.view);
        CHECK(TextView_CanUndo(pair.view) && TextView_CanRedo(pair.view));
        SendMessage(pair.view, EM_SETSEL, 9, 7);
        TEXTVIEWRECOVERY state = {};
        state.directory        = store.path;
        lstrcpy(state.filename, pair.path);
        CHECK(TextView_SaveState(pair.view, &state));
        const std::wstring expected = window_text(pair.view);
        DestroyWindow(pair.parent); // original sequence gone; original file remains the base

        HWND parent = create_hidden_parent(), view = create_textview(parent);
        state.filename[0] = 0;
        CHECK(TextView_LoadState(view, &state));
        CHECK(lstrcmp(state.filename, pair.path) == 0);
        CHECK(window_text(view) == expected && TextView_GetModify(view));
        CHECK(TextView_GetFormat(view) == encoding);
        CHECK(TextView_GetBom(view) == (encoding != NCP_UTF8));
        CHECK(TextView_CanUndo(view) && TextView_CanRedo(view));
        DWORD start = 0, end = 0;
        SendMessage(view, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
        CHECK(start == 7 && end == 9);
        TextView_Redo(view);
        CHECK(window_text(view) == L"extra" + expected);
        TextView_Undo(view);
        CHECK(window_text(view) == expected);
        TextView_Undo(view);
        CHECK(window_text(view) == L"one\r\ntwo \x03bb\r\nthree");
        CHECK(!TextView_GetModify(view));
        TextView_Redo(view);
        SendMessage(view, EM_SETSEL, 0, 0);
        SendMessage(view, EM_REPLACESEL, TRUE, (LPARAM)L"new ");
        CHECK(TextView_SaveState(view, &state)); // continuation seals a new buffer
        DestroyWindow(parent);
        parent = create_hidden_parent();
        view   = create_textview(parent);
        CHECK(TextView_LoadState(view, &state));
        CHECK(window_text(view) == L"new " + expected);
        if (encoding == NCP_UTF8)
        {
            // Save replaces the original, but its old bytes remain needed by undo.
            state.preserveSources = TRUE;
            CHECK(TextView_SaveState(view, &state));
            state.preserveSources = FALSE;
            CHECK(TextView_SaveFile(view, pair.path, encoding, FALSE, 0));
            CHECK(!TextView_GetModify(view));
            SendMessage(view, EM_SETSEL, 0, 0);
            SendMessage(view, EM_REPLACESEL, TRUE, (LPARAM)L"after save ");
            CHECK(TextView_SaveState(view, &state));
            DestroyWindow(parent);
            parent = create_hidden_parent();
            view   = create_textview(parent);
            CHECK(TextView_LoadState(view, &state));
            CHECK(window_text(view) == L"after save new " + expected);
            TextView_Undo(view);
            CHECK(!TextView_GetModify(view));
            TextView_Undo(view);
            CHECK(window_text(view) == expected && TextView_GetModify(view));
        }
        DestroyWindow(parent);
        CHECK(DeleteFile(pair.path));
    }
}

void textview_failed_recovery_or_revert_keeps_the_open_document()
{
    RecoveryStore store;
    HWND          parent = create_hidden_parent(), view = create_textview(parent);
    TextView_Clear(view); // the app sends File > New before accepting input
    SendMessage(view, EM_REPLACESEL, TRUE, (LPARAM)L"untitled changes");
    TEXTVIEWRECOVERY state = {};
    state.directory        = store.path;
    CHECK(TextView_SaveState(view, &state));
    TextView_Clear(view);
    CHECK(TextView_LoadState(view, &state));
    CHECK(!state.filename[0]);
    CHECK(window_text(view) == L"untitled changes");
    TCHAR missing[MAX_PATH];
    wsprintf(missing, TEXT("%s\\missing.txt"), store.path);
    CHECK(!TextView_OpenFile(view, missing));
    CHECK(window_text(view) == L"untitled changes" && TextView_GetModify(view));

    // A valid sequence with invalid document metadata must never replace it.
    RecoveryStore invalid;
    sequence      raw;
    CHECK(raw.init());
    CHECK(raw.insert(0, (const seqchar *)"bad", 3));
    state_writer writer(invalid.path);
    writer.set_context(std::vector<uint8_t>(56, 0));
    CHECK(raw.save_state(writer));
    state.directory = invalid.path;
    lstrcpy(state.filename, TEXT("unchanged"));
    CHECK(!TextView_LoadState(view, &state));
    CHECK(lstrcmp(state.filename, TEXT("unchanged")) == 0);
    CHECK(window_text(view) == L"untitled changes" && TextView_CanUndo(view));

    // Transactional opening must carry the user's small-file memory setting
    // into the new document, including after a recovery.
    TCHAR small[MAX_PATH];
    CHECK(write_temp_file("small", 5, small));
    TextView_SetInMemoryLimit(view, 1024);
    CHECK(TextView_OpenFile(view, small));
    CHECK(TextView_OpenFile(view, small));
    HANDLE writerHandle = CreateFile(small, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0,
                                     OPEN_EXISTING, 0, 0);
    CHECK(writerHandle != INVALID_HANDLE_VALUE);
    if (writerHandle != INVALID_HANDLE_VALUE)
        CloseHandle(writerHandle);
    DestroyWindow(parent);
    CHECK(DeleteFile(small));
}

struct RecoveryHost
{
    RecoveryStore root;
    HWND          parent, view;

    RecoveryHost()
    {
        lstrcpy(recovery_test_root, root.path);
        RecoveryReset();
        Open();
    }

    void Open()
    {
        parent = create_hidden_parent();
        view   = create_textview(parent);
        CHECK(parent && view);
        g_hwndMain      = parent;
        g_hwndTextView  = view;
        g_szFileName[0] = g_szFileTitle[0] = 0;
        recovery_notice                    = 0;
        TextView_Clear(view);
    }

    void Restart()
    {
        DestroyWindow(parent);
        RecoveryShutdown();
        g_hwndMain = g_hwndTextView = 0;
        Open();
    }

    bool MarkerPath(TCHAR path[MAX_PATH], const TCHAR *marker)
    {
        TCHAR           pattern[MAX_PATH];
        WIN32_FIND_DATA data;
        wsprintf(pattern, TEXT("%s\\{*"), root.path);
        HANDLE find = FindFirstFile(pattern, &data);
        if (find == INVALID_HANDLE_VALUE)
            return false;
        wsprintf(path, TEXT("%s\\%s\\%s"), root.path, data.cFileName, marker);
        FindClose(find);
        return true;
    }

    ~RecoveryHost()
    {
        TextView_Clear(view);
        RecoveryReset();
        DestroyWindow(parent);
        g_hwndMain = g_hwndTextView = 0;
        recovery_test_root[0]       = 0;
    }
};

void recovery_controller_keeps_the_final_edit_and_both_histories_on_exit()
{
    RecoveryHost host;
    SendMessage(host.view, EM_REPLACESEL, TRUE, (LPARAM)L"first");
    SendMessage(host.view, EM_SETSEL, 0, 0);
    SendMessage(host.view, EM_REPLACESEL, TRUE, (LPARAM)L"second");
    TextView_Undo(host.view);
    CHECK(TextView_CanUndo(host.view) && TextView_CanRedo(host.view));
    // No idle tick: exit must capture the most recent state immediately.
    CHECK(RecoveryClose());
    host.Restart();
    CHECK(RecoveryRestore(host.parent));
    CHECK(recovery_notice == NOTIFY_SESSION_RESTORED_UNTITLED);
    CHECK(window_text(host.view) == L"first");
    CHECK(TextView_GetModify(host.view));
    CHECK(TextView_CanUndo(host.view) && TextView_CanRedo(host.view));
    TextView_Redo(host.view);
    CHECK(window_text(host.view) == L"secondfirst");
    TCHAR marker[MAX_PATH];
    CHECK(host.MarkerPath(marker, TEXT("session")));
    CHECK(GetFileAttributes(marker) == INVALID_FILE_ATTRIBUTES); // claimed session is active now

    // The same abandoned checkpoint without a graceful-exit marker is recovery.
    CHECK(RecoveryClose());
    CHECK(DeleteFile(marker));
    host.Restart();
    CHECK(RecoveryRestore(host.parent));
    CHECK(recovery_notice == NOTIFY_RECOVERED_UNTITLED);
    CHECK(window_text(host.view) == L"secondfirst");
}

void recovery_controller_failed_checkpoint_keeps_the_document_open()
{
    RecoveryHost host;
    SendMessage(host.view, EM_REPLACESEL, TRUE, (LPARAM)L"keep these last edits");
    TCHAR file[MAX_PATH];
    CHECK(write_temp_file("not a directory", 15, file));
    lstrcpy(recovery_test_root, file); // deterministic I/O failure, no permission changes
    CHECK(!RecoveryClose());
    CHECK(recovery_notice == NOTIFY_RECOVERY_WRITE_FAILED);
    CHECK(IsWindow(host.view) && TextView_GetModify(host.view));
    CHECK(window_text(host.view) == L"keep these last edits");
    lstrcpy(recovery_test_root, host.root.path);
    CHECK(DeleteFile(file));
    CHECK(RecoveryClose());
    host.Restart();
    CHECK(RecoveryRestore(host.parent));
    CHECK(window_text(host.view) == L"keep these last edits");
}

void recovery_controller_failed_exit_marker_can_be_retried()
{
    RecoveryHost host;
    SendMessage(host.view, EM_REPLACESEL, TRUE, (LPARAM)L"last keystroke");
    CHECK(RecoveryBeforeSave());
    TCHAR marker[MAX_PATH];
    CHECK(host.MarkerPath(marker, TEXT("session")));
    HANDLE lock = CreateFile(marker, GENERIC_READ | GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN, 0);
    CHECK(lock != INVALID_HANDLE_VALUE);
    CHECK(!RecoveryClose());
    CHECK(IsWindow(host.view) && window_text(host.view) == L"last keystroke");
    CloseHandle(lock);
    CHECK(DeleteFile(marker));
    CHECK(RecoveryClose());
    host.Restart();
    CHECK(RecoveryRestore(host.parent));
    CHECK(recovery_notice == NOTIFY_SESSION_RESTORED_UNTITLED);
    CHECK(window_text(host.view) == L"last keystroke");
}

void recovery_controller_does_not_restore_saved_or_discarded_documents()
{
    RecoveryHost host;
    CHECK(RecoveryClose());
    host.Restart();
    CHECK(!RecoveryRestore(host.parent));
    SendMessage(host.view, EM_REPLACESEL, TRUE, (LPARAM)L"discard me");
    CHECK(RecoveryBeforeSave());
    TextView_Clear(host.view);
    RecoveryReset(); // New after explicit Discard
    CHECK(RecoveryClose());
    host.Restart();
    CHECK(!RecoveryRestore(host.parent));
    CHECK(window_text(host.view).empty());
}

void recovery_controller_restores_named_work_without_saving_the_original()
{
    RecoveryHost host;
    TCHAR        original[MAX_PATH];
    CHECK(write_temp_file("original", 8, original));
    CHECK(TextView_OpenFile(host.view, original));
    lstrcpy(g_szFileName, original);
    lstrcpy(g_szFileTitle, _tcsrchr(original, _T('\\')) + 1);
    SendMessage(host.view, EM_SETSEL, 0, 0);
    SendMessage(host.view, EM_REPLACESEL, TRUE, (LPARAM)L"unsaved ");
    CHECK(RecoveryClose());
    host.Restart();
    CHECK(RecoveryRestore(host.parent));
    CHECK(recovery_notice == NOTIFY_SESSION_RESTORED);
    CHECK(lstrcmp(g_szFileName, original) == 0);
    CHECK(window_text(host.view) == L"unsaved original");
    HANDLE file      = CreateFile(original, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0,
                                  OPEN_EXISTING, 0, 0);
    char   bytes[32] = {};
    DWORD  n         = 0;
    CHECK(file != INVALID_HANDLE_VALUE);
    CHECK(ReadFile(file, bytes, sizeof(bytes), &n, 0));
    CloseHandle(file);
    CHECK(n == 8 && memcmp(bytes, "original", 8) == 0);

    CHECK(RecoveryBeforeSave());
    CHECK(TextView_SaveFile(host.view, original, NCP_UTF8, FALSE, 0));
    RecoverySaved();
    CHECK(RecoveryClose());
    host.Restart();
    CHECK(!RecoveryRestore(host.parent));
    CHECK(DeleteFile(original));
}

void recovery_controller_restores_multiple_sessions_newest_first()
{
    RecoveryHost host;
    SendMessage(host.view, EM_REPLACESEL, TRUE, (LPARAM)L"older session");
    CHECK(RecoveryClose());
    host.Restart();
    SendMessage(host.view, EM_REPLACESEL, TRUE, (LPARAM)L"newer session");
    CHECK(RecoveryClose());
    host.Restart();
    CHECK(RecoveryRestore(host.parent));
    CHECK(window_text(host.view) == L"newer session");
    TextView_Clear(host.view);
    RecoveryReset();
    CHECK(RecoveryRestore(host.parent));
    CHECK(window_text(host.view) == L"older session");
}

const test_case tests[] = {
    {"recovery_controller_keeps_the_final_edit_and_both_histories_on_exit",
     recovery_controller_keeps_the_final_edit_and_both_histories_on_exit},
    {"recovery_controller_failed_checkpoint_keeps_the_document_open",
     recovery_controller_failed_checkpoint_keeps_the_document_open},
    {"recovery_controller_failed_exit_marker_can_be_retried", recovery_controller_failed_exit_marker_can_be_retried},
    {"recovery_controller_does_not_restore_saved_or_discarded_documents",
     recovery_controller_does_not_restore_saved_or_discarded_documents},
    {"recovery_controller_restores_named_work_without_saving_the_original",
     recovery_controller_restores_named_work_without_saving_the_original},
    {"recovery_controller_restores_multiple_sessions_newest_first",
     recovery_controller_restores_multiple_sessions_newest_first},
    {"textview_recovers_encoding_selection_and_both_histories",
     textview_recovers_encoding_selection_and_both_histories},
    {"textview_failed_recovery_or_revert_keeps_the_open_document",
     textview_failed_recovery_or_revert_keeps_the_open_document},
    {"textview_edit_messages_match_an_edit_control", textview_edit_messages_match_an_edit_control},
    {"textview_edit_selection_covers_whole_characters", textview_edit_selection_covers_whole_characters},
    {"textview_edit_positions_round_trip", textview_edit_positions_round_trip},
    {"textview_edit_replacesel_matches_an_edit_control", textview_edit_replacesel_matches_an_edit_control},
    {"textview_edit_scrolling_matches_an_edit_control", textview_edit_scrolling_matches_an_edit_control},
    {"textview_edit_messages_index_as_they_need", textview_edit_messages_index_as_they_need},
    {"textview_goto_line_reports_indexing", textview_goto_line_reports_indexing},
    {"textview_converts_line_breaks", textview_converts_line_breaks},
    {"textview_filters_lines_and_goes_back_to_them", textview_filters_lines_and_goes_back_to_them},
};
}

const test_group textview_tests = { "textview", tests, sizeof(tests) / sizeof(tests[0]) };
