//
//	GTK view tests: PangoTextLayout and NeatpadTextView, with real Pango
//	fonts. GTK needs a display, so in CI they run under Xvfb.
//
#include "../tests.h"
#include "../../src/TextView/TextViewInternal.h"
#include "../../src/TextView/gtk/NeatpadTextView.h"
#include "../../src/TextView/gtk/TextViewGtk.h"

extern "C"
{
#include "../../src/Neatpad/gtk/recovery.h"
}

int g_failures = 0;

void check(bool condition, const char *expr, const char *file, int line)
{
    if (!condition)
    {
        printf("%s(%d): check failed: %s\n", file, line, expr);

        if (getenv("GITHUB_ACTIONS"))
            printf("::error::%s(%d): check failed: %s\n", file, line, expr);

        g_failures++;
    }
}

namespace
{
// A UTF-8 lead byte, or ASCII: where a character starts
bool starts_character(const tstring &text, size_t i)
{
    return ((unsigned char)text[i] & 0xC0) != 0x80;
}

// A widget, sunk so it can be freed, and its host
struct Widget
{
    GtkWidget   *widget;
    TextViewGtk *host;

    Widget()
    {
        widget = neatpad_text_view_new();
        g_object_ref_sink(widget);
        host = NeatpadTextViewHost(widget);
        host->Resize(640, 480);
    }

    ~Widget()
    {
        g_object_unref(widget);
    }
};

void pango_layout_stops_only_at_character_starts()
{
    Widget                widget;
    TextLayout           *layout = widget.host->CreateLayout();
    tstring               text   = tstr(U"a\u00e9\U0001F600\u4e2d \tb ok");
    std::vector<TextRun>  runs(text.size());
    int                   previous = -1;

    for (TextRun &run : runs)
    {
        run     = TextRun();
        run.len = 1;
    }

    CHECK(layout->Analyze(text.c_str(), (int)text.size(), runs.data(), 4));
    CHECK(layout->Length() == (int)text.size());

    for (int i = 0; i <= (int)text.size(); i++)
    {
        bool start = i == (int)text.size() || starts_character(text, i);

        // the caret and words stop only where characters start
        CHECK(layout->IsCursorStop(i) == start);

        if (!start)
        {
            CHECK(!layout->IsWordStop(i));
            CHECK(!layout->IsWhiteSpace(i));
            continue;
        }

        // across the line, and x finds each stop again
        int  x = layout->OffsetToX(i, false);
        int  offset;
        bool trailing;

        CHECK(x >= previous);
        previous = x;

        if (i < (int)text.size())
        {
            layout->XToOffset(x, &offset, &trailing);
            CHECK(offset == i);
            CHECK(!trailing);
        }
    }

    CHECK(layout->IsWhiteSpace((int)tstr(U"a\u00e9\U0001F600\u4e2d").size()));
    CHECK(layout->IsWordStop((int)tstr(U"a\u00e9\U0001F600\u4e2d \tb ").size()));
    CHECK(layout->Width() == layout->OffsetToX((int)text.size(), false));

    delete layout;
}

// Control characters take the room of their hex code in a box, as on Win32,
// and a NUL doesn't end the line
void pango_layout_shows_control_characters()
{
    Widget               widget;
    TextLayout          *layout = widget.host->CreateLayout();
    std::string          text("a\x1b" "b\0c", 5);
    std::vector<TextRun> runs(text.size());
    int                  cell = widget.host->CharWidth();

    for (size_t i = 0; i < runs.size(); i++)
    {
        runs[i]      = TextRun();
        runs[i].len  = 1;
        runs[i].ctrl = text[i] == 0x1b || text[i] == 0;
    }

    CHECK(layout->Analyze(text.data(), (int)text.size(), runs.data(), 4));

    // wider than the two digits of its code, and every character laid out
    CHECK(layout->OffsetToX(2, false) - layout->OffsetToX(1, false) > 2 * cell);
    CHECK(layout->OffsetToX(4, false) - layout->OffsetToX(3, false) > 2 * cell);
    CHECK(layout->OffsetToX(5, false) > layout->OffsetToX(4, false));
    CHECK(layout->Width() == layout->OffsetToX(5, false));

    delete layout;
}

void widget_arrows_step_over_characters_with_pango()
{
    Widget      widget;
    TextView   *view = widget.host->View();
    std::string bytes = encode_text(U"a\u00e9\U0001F600b", NCP_UTF8);
    seqfilechar path[TEST_PATH_MAX];
    size_w      expected[] = {1, 2, 3, 4};

    CHECK(write_temp_file(bytes.data(), bytes.size(), path));
    CHECK(neatpad_text_view_open_file(NEATPAD_TEXT_VIEW(widget.widget), path));

    for (size_w column : expected)
    {
        guint64 line;
        guint64 at;

        view->OnKeyDown(TVK_RIGHT, 0);
        neatpad_text_view_get_cursor(NEATPAD_TEXT_VIEW(widget.widget), &line, &at, 0);
        CHECK(line == 0 && at == column);
    }

    delete_file(path);
}

void widget_types_through_its_engine()
{
    Widget      widget;
    TextView   *view = widget.host->View();
    std::string bytes = encode_text(U"", NCP_UTF8);
    seqfilechar path[TEST_PATH_MAX];
    int         changed = 0;
    guint64     column;

    CHECK(write_temp_file(bytes.data(), bytes.size(), path));
    CHECK(neatpad_text_view_open_file(NEATPAD_TEXT_VIEW(widget.widget), path));

    g_signal_connect(widget.widget, "changed", G_CALLBACK(+[](GtkWidget *, gpointer data) { ++*(int *)data; }),
                     &changed);

    view->OnText("caf\xc3\xa9", 5);
    neatpad_text_view_get_cursor(NEATPAD_TEXT_VIEW(widget.widget), 0, &column, 0);

    CHECK(column == 4);
    CHECK(changed == 1);
    CHECK(neatpad_text_view_can_undo(NEATPAD_TEXT_VIEW(widget.widget)));
    CHECK(neatpad_text_view_undo(NEATPAD_TEXT_VIEW(widget.widget)));
    CHECK(changed == 2);

    delete_file(path);
}

void widget_finds_saves_and_starts_again()
{
    Widget           widget;
    NeatpadTextView *view  = NEATPAD_TEXT_VIEW(widget.widget);
    std::string      bytes = "one two one";
    seqfilechar      path[TEST_PATH_MAX];
    seqfilechar      saved[TEST_PATH_MAX];
    guint64          column;
    char             text[32] = {0};
    FILE            *file;

    CHECK(write_temp_file(bytes.data(), bytes.size(), path));
    CHECK(temp_file(saved));
    CHECK(neatpad_text_view_open_file(view, path));
    CHECK(!neatpad_text_view_get_modified(view));
    CHECK(strcmp(neatpad_text_view_get_encoding(view), "UTF-8") == 0);

    // each match in turn, then round to the first again
    CHECK(neatpad_text_view_find(view, "one", FALSE, TRUE) == NEATPAD_FIND_FOUND);
    neatpad_text_view_get_cursor(view, 0, &column, 0);
    CHECK(column == 3);
    CHECK(neatpad_text_view_find(view, "one", FALSE, TRUE) == NEATPAD_FIND_FOUND);
    neatpad_text_view_get_cursor(view, 0, &column, 0);
    CHECK(column == 11);
    CHECK(neatpad_text_view_find(view, "one", FALSE, TRUE) == NEATPAD_FIND_WRAPPED);
    CHECK(neatpad_text_view_find(view, "ONE", FALSE, TRUE) == NEATPAD_FIND_NOT_FOUND);

    // typing replaces the match; saving keeps it
    widget.host->View()->OnText("1", 1);
    CHECK(neatpad_text_view_get_modified(view));
    CHECK(neatpad_text_view_save_file(view, saved));
    CHECK(!neatpad_text_view_get_modified(view));

    file = fopen(saved, "rb");
    CHECK(file != 0);

    if (file)
    {
        CHECK(fread(text, 1, sizeof(text) - 1, file) == 9);
        fclose(file);
    }

    CHECK(strcmp(text, "1 two one") == 0);

    neatpad_text_view_new_file(view);
    neatpad_text_view_get_cursor(view, 0, &column, 0);
    CHECK(column == 0);
    CHECK(!neatpad_text_view_get_modified(view));

    delete_file(path);
    delete_file(saved);
}

void widget_replaces_and_goes_to_lines()
{
    Widget           widget;
    NeatpadTextView *view  = NEATPAD_TEXT_VIEW(widget.widget);
    std::string      bytes = "cat dog\ncat\nbird";
    seqfilechar      path[TEST_PATH_MAX];
    guint64          line;
    guint64          column;

    CHECK(write_temp_file(bytes.data(), bytes.size(), path));
    CHECK(neatpad_text_view_open_file(view, path));

    // a selection that isn't a match isn't replaced; a found one is
    CHECK(neatpad_text_view_replace(view, "cat", "cow", TRUE, FALSE) == 0);
    CHECK(neatpad_text_view_find(view, "cat", FALSE, TRUE) == NEATPAD_FIND_FOUND);
    CHECK(neatpad_text_view_replace(view, "cat", "cow", TRUE, FALSE) == 1);
    CHECK(neatpad_text_view_replace(view, "cat", "horse", TRUE, TRUE) == 1);
    CHECK(neatpad_text_view_find(view, "horse", FALSE, TRUE) != NEATPAD_FIND_NOT_FOUND);

    // lines count from 0
    CHECK(neatpad_text_view_goto_line(view, 2));
    neatpad_text_view_get_cursor(view, &line, &column, 0);
    CHECK(line == 2 && column == 0);
    CHECK(!neatpad_text_view_goto_line(view, 7));

    delete_file(path);
}

// In a scrolled window the engine sets the adjustments' ranges, in bytes down
// (where the top line starts, so a file needn't be counted), and scrolls as
// they move
void widget_scrolls_with_its_adjustments()
{
    GtkWidget       *scroll = gtk_scrolled_window_new();
    Widget           widget;
    NeatpadTextView *view = NEATPAD_TEXT_VIEW(widget.widget);
    std::string      bytes;
    seqfilechar      path[TEST_PATH_MAX];
    GtkAdjustment   *vertical;
    guint64          line;

    for (int i = 0; i < 1000; i++)
        bytes += "line\n";

    g_object_ref_sink(scroll);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), widget.widget);
    vertical = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(widget.widget));
    CHECK(vertical == gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll)));

    CHECK(write_temp_file(bytes.data(), bytes.size(), path));
    CHECK(neatpad_text_view_open_file(view, path));
    widget.host->Resize(640, 480);

    CHECK(gtk_adjustment_get_upper(vertical) == bytes.size());
    CHECK(gtk_adjustment_get_page_size(vertical) > 0);

    // scrolled to line 200's first byte, the top row is line 200
    gtk_adjustment_set_value(vertical, 200 * 5);
    widget.host->View()->OnLButtonDown(0, 0, 0);
    widget.host->View()->OnLButtonUp(0, 0, 0);
    neatpad_text_view_get_cursor(view, &line, 0, 0);
    CHECK(line == 200);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), 0);
    g_object_unref(scroll);
    delete_file(path);
}

// What recovery asks of a test's window: its file, and failures counted
struct RecoveryWindow
{
    Widget         widget;
    std::string    path;
    int            failures = 0;
    RecoveryClient client;
    Recovery      *recovery;

    explicit RecoveryWindow(const std::string &file) : path(file)
    {
        client.view     = NEATPAD_TEXT_VIEW(widget.widget);
        client.filename = [](gpointer data) -> char * { return g_strdup(((RecoveryWindow *)data)->path.c_str()); };
        client.failed   = [](gpointer data) { ((RecoveryWindow *)data)->failures++; };
        client.data     = this;
        recovery        = recovery_new(&client);
    }
};

int store_count()
{
    GPtrArray *stores = recovery_list();
    int        count  = (int)stores->len;

    g_ptr_array_unref(stores);
    return count;
}

// Unsaved changes kept as a session are found by their file and come back,
// once; a store a window has isn't listed and can't be taken; a saved one is
// tidied away
void recovery_keeps_and_restores_unsaved_changes()
{
    std::string bytes = "hello";
    seqfilechar path[TEST_PATH_MAX];
    char       *filename;
    gboolean    session;
    char       *store;
    GPtrArray  *stores;

    CHECK(write_temp_file(bytes.data(), bytes.size(), path));
    CHECK(store_count() == 0);

    {
        RecoveryWindow first(path);

        CHECK(neatpad_text_view_open_file(first.client.view, path));
        first.widget.host->View()->OnKeyDown(TVK_END, 0);
        first.widget.host->View()->OnText(" world", 6);

        // a crash now would leave the store, but while the window has it,
        // it's not for anyone else
        CHECK(recovery_before_save(first.recovery));
        CHECK(store_count() == 0);
        CHECK(recovery_store_for(path) == 0);

        CHECK(recovery_keep_session(first.recovery));
        recovery_free(first.recovery);
        CHECK(first.failures == 0);
    }

    // listed, with its file
    stores = recovery_list();
    CHECK(stores->len == 1);
    CHECK(((RecoveryStore *)stores->pdata[0])->session);
    CHECK(strcmp(((RecoveryStore *)stores->pdata[0])->filename, path) == 0);
    g_ptr_array_unref(stores);

    store = recovery_store_for(path);
    CHECK(store != 0);

    {
        RecoveryWindow restored("");
        RecoveryWindow other("");
        guint64        column;

        CHECK(recovery_restore(restored.recovery, store, &filename, &session));
        CHECK(session);
        CHECK(filename && strcmp(filename, path) == 0);
        CHECK(neatpad_text_view_get_modified(restored.client.view));
        neatpad_text_view_get_cursor(restored.client.view, 0, &column, 0);
        CHECK(column == 11);
        g_free(filename);

        // another window can't take it now
        CHECK(!recovery_restore(other.recovery, store, &filename, &session));
        CHECK(store_count() == 0);
        recovery_free(other.recovery);

        // closed without keeping it, the store goes
        recovery_free(restored.recovery);
        CHECK(store_count() == 0);
    }

    g_free(store);

    {
        RecoveryWindow untitled("");

        // an untitled document's store has no file, and can be discarded
        untitled.path.clear();
        untitled.widget.host->View()->OnText("draft", 5);
        CHECK(recovery_keep_session(untitled.recovery));
        recovery_free(untitled.recovery);

        stores = recovery_list();
        CHECK(stores->len == 1);
        CHECK(((RecoveryStore *)stores->pdata[0])->filename == 0);
        CHECK(recovery_discard(((RecoveryStore *)stores->pdata[0])->store));
        g_ptr_array_unref(stores);
        CHECK(store_count() == 0);
    }

    {
        RecoveryWindow saved(path);

        CHECK(neatpad_text_view_open_file(saved.client.view, path));
        saved.widget.host->View()->OnText("!", 1);
        CHECK(recovery_before_save(saved.recovery));
        CHECK(neatpad_text_view_save_file(saved.client.view, path));
        recovery_saved(saved.recovery);
        recovery_free(saved.recovery);

        // saved, there's nothing to restore
        CHECK(store_count() == 0);
    }

    delete_file(path);
}

// Following a file: what another program appends comes in at the end, with
// the caret, and a file rewritten shorter has to be opened again
void widget_follows_a_growing_file()
{
    Widget           widget;
    NeatpadTextView *view  = NEATPAD_TEXT_VIEW(widget.widget);
    std::string      bytes = "one\n";
    seqfilechar      path[TEST_PATH_MAX];
    guint64          line;
    FILE            *file;

    CHECK(write_temp_file(bytes.data(), bytes.size(), path));
    CHECK(neatpad_text_view_tail_file(view, path));
    CHECK(widget.host->View()->GetEditMode() == MODE_READONLY);

    file = fopen(path, "ab");
    CHECK(file != 0);
    fputs("two\nthree\n", file);
    fclose(file);

    CHECK(neatpad_text_view_update_tail(view));
    CHECK(widget.host->View()->FileDoc()->byte_length() == 14);
    neatpad_text_view_get_cursor(view, &line, 0, 0);
    CHECK(line == 3);

    file = fopen(path, "wb");
    CHECK(file != 0);
    fputs("x", file);
    fclose(file);

    CHECK(!neatpad_text_view_update_tail(view));
    delete_file(path);
}

const test_case tests[] = {
    {"pango_layout_stops_only_at_character_starts", pango_layout_stops_only_at_character_starts},
    {"pango_layout_shows_control_characters", pango_layout_shows_control_characters},
    {"widget_arrows_step_over_characters_with_pango", widget_arrows_step_over_characters_with_pango},
    {"widget_types_through_its_engine", widget_types_through_its_engine},
    {"widget_finds_saves_and_starts_again", widget_finds_saves_and_starts_again},
    {"widget_replaces_and_goes_to_lines", widget_replaces_and_goes_to_lines},
    {"widget_scrolls_with_its_adjustments", widget_scrolls_with_its_adjustments},
    {"widget_follows_a_growing_file", widget_follows_a_growing_file},
    {"recovery_keeps_and_restores_unsaved_changes", recovery_keeps_and_restores_unsaved_changes},
};
} // namespace

static int run_tests()
{
    // no display, nothing to test: ctest counts it skipped
    if (!gtk_init_check())
    {
        printf("No display: GTK tests skipped\n");
        return 77;
    }

    for (const test_case &test : tests)
    {
        int before = g_failures;

        printf("run %s\n", test.name);
        test.run();
        printf("%s %s\n", before == g_failures ? "ok" : "FAIL", test.name);
    }

    if (g_failures)
    {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }

    printf("All tests passed\n");
    return 0;
}

int main()
{
    char state[] = "/tmp/neatpad-gtk-tests-XXXXXX";
    int  result;

    // recovery keeps its stores here, not in the user's own state
    if (!mkdtemp(state))
        return 1;

    setenv("XDG_STATE_HOME", state, 1);
    result = run_tests();
    std::filesystem::remove_all(state);
    return result;
}
