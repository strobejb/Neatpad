//
//	GTK view tests: PangoTextLayout and NeatpadTextView, with real Pango
//	fonts. GTK needs a display, so in CI they run under Xvfb.
//
#include "../tests.h"
#include "../../src/TextView/TextViewInternal.h"
#include "../../src/TextView/gtk/NeatpadTextView.h"
#include "../../src/TextView/gtk/TextViewGtk.h"

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

const test_case tests[] = {
    {"pango_layout_stops_only_at_character_starts", pango_layout_stops_only_at_character_starts},
    {"widget_arrows_step_over_characters_with_pango", widget_arrows_step_over_characters_with_pango},
    {"widget_types_through_its_engine", widget_types_through_its_engine},
    {"widget_finds_saves_and_starts_again", widget_finds_saves_and_starts_again},
    {"widget_replaces_and_goes_to_lines", widget_replaces_and_goes_to_lines},
};
} // namespace

int main()
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
