#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <string.h>

#include <string>

#include "../TextView/sequence.h"

namespace
{
int g_failures = 0;

void check(bool condition, const char *expr, const char *file, int line)
{
    if(!condition)
    {
        printf("%s(%d): check failed: %s\n", file, line, expr);
        g_failures++;
    }
}

#define CHECK(expr) check((expr), #expr, __FILE__, __LINE__)

void init(sequence &seq, const char *text)
{
    CHECK(seq.init(reinterpret_cast<const seqchar *>(text), strlen(text)));
}

bool insert_bytes(sequence &seq, size_w index, const char *text)
{
    return seq.insert(index, reinterpret_cast<const seqchar *>(text), strlen(text));
}

bool replace_bytes(sequence &seq, size_w index, const char *text, size_w erase_length)
{
    return seq.replace(index, reinterpret_cast<const seqchar *>(text), strlen(text), erase_length);
}

bool write_temp_file(const char *data, size_t length, TCHAR path[MAX_PATH])
{
    TCHAR temp_path[MAX_PATH];
    HANDLE hFile;
    DWORD written = 0;
    bool ok;

    path[0] = 0;

    if(GetTempPath(MAX_PATH, temp_path) == 0)
        return false;

    if(GetTempFileName(temp_path, TEXT("seq"), 0, path) == 0)
        return false;

    hFile = CreateFile(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, 0);

    if(hFile == INVALID_HANDLE_VALUE)
	{
		DeleteFile(path);
		path[0] = 0;
        return false;
	}

    ok = WriteFile(hFile, data, static_cast<DWORD>(length), &written, 0) && written == length;
    CloseHandle(hFile);

    if(!ok)
    {
        DeleteFile(path);
        path[0] = 0;
    }

    return ok;
}

std::string render_content(const sequence &seq)
{
    std::string actual;

    if(seq.size() == 0)
        return actual;

    actual.resize(static_cast<size_t>(seq.size()));

    size_w rendered = seq.render(0, reinterpret_cast<seqchar *>(&actual[0]), seq.size());
    CHECK(rendered == seq.size());

    return actual;
}

void expect_content(const sequence &seq, const char *expected)
{
    CHECK(seq.size() == strlen(expected));
    CHECK(render_content(seq) == expected);
}

void expect_line_offset(const sequence &seq, size_w line, size_w expected)
{
    size_w actual = static_cast<size_w>(-1);
    CHECK(seq.lineoffset(line, &actual));
    CHECK(actual == expected);
}

void expect_line_from_offset(const sequence &seq, size_w offset, size_w expected_line, size_w expected_line_offset)
{
    size_w actual_line = static_cast<size_w>(-1);
    size_w actual_line_offset = static_cast<size_w>(-1);

    CHECK(seq.linefromoffset(offset, &actual_line, &actual_line_offset));
    CHECK(actual_line == expected_line);
    CHECK(actual_line_offset == expected_line_offset);
}

void insert_at_beginning()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(insert_bytes(seq, 0, "XX"));
    expect_content(seq, "XXabcdef");
}

void insert_at_end()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(insert_bytes(seq, seq.size(), "XX"));
    expect_content(seq, "abcdefXX");
}

void insert_on_span_boundary()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(insert_bytes(seq, 3, "XX"));
    CHECK(insert_bytes(seq, 5, "YY"));
    expect_content(seq, "abcXXYYdef");
}

void insert_in_middle_of_span()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(insert_bytes(seq, 2, "XX"));
    expect_content(seq, "abXXcdef");
}

void delete_starts_on_span_boundary()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(insert_bytes(seq, 3, "XX"));
    CHECK(seq.erase(3, 3));
    expect_content(seq, "abcef");
}

void delete_ends_on_span_boundary()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(insert_bytes(seq, 3, "XX"));
    CHECK(seq.erase(1, 2));
    expect_content(seq, "aXXdef");
}

void delete_entire_file_with_multiple_spans()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(insert_bytes(seq, 0, "S"));
    CHECK(insert_bytes(seq, 4, "M"));
    CHECK(insert_bytes(seq, seq.size(), "E"));
    expect_content(seq, "SabcMdefE");

    CHECK(seq.erase(0, seq.size()));
    expect_content(seq, "");
}

void delete_starts_and_stops_within_single_span()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(seq.erase(2, 2));
    expect_content(seq, "abef");
}

void delete_across_multiple_spans()
{
    sequence seq;
    init(seq, "abcdefgh");

    CHECK(insert_bytes(seq, 3, "XX"));
    CHECK(seq.erase(2, 5));
    expect_content(seq, "abfgh");
}

void replace_at_beginning()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(replace_bytes(seq, 0, "XX", 2));
    expect_content(seq, "XXcdef");
}

void replace_at_end()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(replace_bytes(seq, seq.size(), "XX", 2));
    expect_content(seq, "abcdefXX");
}

void replace_on_span_boundary()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(insert_bytes(seq, 3, "XX"));
    CHECK(replace_bytes(seq, 3, "YY", 2));
    expect_content(seq, "abcYYdef");
}

void replace_in_middle_of_span()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(replace_bytes(seq, 2, "XYZ", 3));
    expect_content(seq, "abXYZf");
}

void replace_across_multiple_spans()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(insert_bytes(seq, 3, "XX"));
    CHECK(replace_bytes(seq, 2, "QQ", 5));
    expect_content(seq, "abQQf");
}

void consecutive_insert_undo_coalesces()
{
    sequence seq;
    init(seq, "abc");

    CHECK(insert_bytes(seq, seq.size(), "X"));
    CHECK(insert_bytes(seq, seq.size(), "Y"));
    expect_content(seq, "abcXY");

    CHECK(seq.undo());
    expect_content(seq, "abc");

    CHECK(seq.redo());
    expect_content(seq, "abcXY");
}

void breakopt_prevents_insert_coalescing()
{
    sequence seq;
    init(seq, "abc");

    CHECK(insert_bytes(seq, seq.size(), "X"));
    seq.breakopt();
    CHECK(insert_bytes(seq, seq.size(), "Y"));
    expect_content(seq, "abcXY");

    CHECK(seq.undo());
    expect_content(seq, "abcX");

    CHECK(seq.undo());
    expect_content(seq, "abc");

    CHECK(seq.redo());
    expect_content(seq, "abcX");

    CHECK(seq.redo());
    expect_content(seq, "abcXY");
}

void forward_delete_undo_coalesces()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(seq.erase(2, 1));
    CHECK(seq.erase(2, 1));
    expect_content(seq, "abef");

    CHECK(seq.undo());
    expect_content(seq, "abcdef");

    CHECK(seq.redo());
    expect_content(seq, "abef");
}

void backward_delete_undo_coalesces()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(seq.erase(3, 1));
    CHECK(seq.erase(2, 1));
    expect_content(seq, "abef");

    CHECK(seq.undo());
    expect_content(seq, "abcdef");

    CHECK(seq.redo());
    expect_content(seq, "abef");
}

void replace_overrun_clamps_to_end()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(replace_bytes(seq, 4, "XYZ", 99));
    expect_content(seq, "abcdXYZ");
}

void replace_shorter_than_erased()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(replace_bytes(seq, 1, "Z", 4));
    expect_content(seq, "aZf");
}

void replace_longer_than_erased()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(replace_bytes(seq, 2, "WXYZ", 1));
    expect_content(seq, "abWXYZdef");
}

void undo_redo_insert_middle_split()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(insert_bytes(seq, 3, "XX"));
    expect_content(seq, "abcXXdef");

    CHECK(seq.undo());
    expect_content(seq, "abcdef");

    CHECK(seq.redo());
    expect_content(seq, "abcXXdef");
}

void undo_redo_delete_within_split_span()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(seq.erase(2, 2));
    expect_content(seq, "abef");

    CHECK(seq.undo());
    expect_content(seq, "abcdef");

    CHECK(seq.redo());
    expect_content(seq, "abef");
}

void undo_redo_replace_across_spans()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(insert_bytes(seq, 3, "XX"));
    seq.breakopt();
    CHECK(replace_bytes(seq, 2, "QQ", 5));
    expect_content(seq, "abQQf");

    CHECK(seq.undo());
    expect_content(seq, "abcXXdef");

    CHECK(seq.redo());
    expect_content(seq, "abQQf");
}

void grouped_operations_undo_redo_together()
{
    sequence seq;
    init(seq, "abcdef");

    seq.group();
    CHECK(insert_bytes(seq, 1, "X"));
    CHECK(seq.erase(4, 1));
    CHECK(replace_bytes(seq, 0, "Q", 1));
    seq.ungroup();
    expect_content(seq, "QXbcef");

    CHECK(seq.undo());
    expect_content(seq, "abcdef");

    CHECK(seq.redo());
    expect_content(seq, "QXbcef");
}

void nested_group_survives_inner_replace()
{
    sequence seq;
    init(seq, "abcdef");

    seq.group();
    CHECK(insert_bytes(seq, 1, "X"));
    CHECK(replace_bytes(seq, 3, "Y", 1));
    CHECK(seq.erase(5, 1));
    seq.ungroup();
	expect_content(seq, "aXbYdf");

    CHECK(seq.undo());
    expect_content(seq, "abcdef");

    CHECK(seq.redo());
	expect_content(seq, "aXbYdf");
}

void redo_invalidated_by_new_edit()
{
    sequence seq;
    init(seq, "abc");

    CHECK(insert_bytes(seq, seq.size(), "X"));
    CHECK(seq.undo());
    CHECK(seq.canredo());

    CHECK(insert_bytes(seq, seq.size(), "Y"));
    CHECK(!seq.canredo());
    expect_content(seq, "abcY");
}

void invalid_operations_do_not_modify_content()
{
    sequence seq;
    init(seq, "abc");

    CHECK(!insert_bytes(seq, 4, "X"));
    CHECK(!insert_bytes(seq, 1, ""));
    CHECK(!seq.erase(1, 0));
    CHECK(!seq.erase(2, 5));
    CHECK(!replace_bytes(seq, 4, "X", 1));
    expect_content(seq, "abc");
    CHECK(!seq.canundo());
}

void render_past_end_returns_available_bytes()
{
    sequence seq;
    seqchar actual[5] = {};
    init(seq, "abc");

    CHECK(seq.render(2, actual, 5) == 1);
    CHECK(actual[0] == 'c');

    CHECK(seq.render(3, actual, 5) == 0);
}

void linecount_handles_basic_newlines()
{
    sequence seq;
    init(seq, "abc\n");

    CHECK(seq.linecount() == 2);
    expect_line_offset(seq, 0, 0);
    expect_line_offset(seq, 1, 4);
    CHECK(!seq.lineoffset(2, 0));
}

void linecount_handles_crlf()
{
    sequence seq;
    init(seq, "abc\r\nxyz\nlast");

    CHECK(seq.linecount() == 3);
    expect_line_offset(seq, 0, 0);
    expect_line_offset(seq, 1, 5);
    expect_line_offset(seq, 2, 9);
}

void linecount_handles_crlf_split_across_spans()
{
    sequence seq;
    init(seq, "abc\rxyz");

    CHECK(insert_bytes(seq, 4, "\n"));
    expect_content(seq, "abc\r\nxyz");
    CHECK(seq.linecount() == 2);
    expect_line_offset(seq, 1, 5);
}

void linecount_updates_after_insert_delete_replace()
{
    sequence seq;
    init(seq, "abc");

    CHECK(seq.linecount() == 1);
    CHECK(insert_bytes(seq, 1, "\nX"));
    expect_content(seq, "a\nXbc");
    CHECK(seq.linecount() == 2);
    expect_line_offset(seq, 1, 2);

    CHECK(seq.erase(1, 2));
    expect_content(seq, "abc");
    CHECK(seq.linecount() == 1);

    CHECK(replace_bytes(seq, 1, "\r\nZ\n", 1));
    expect_content(seq, "a\r\nZ\nc");
    CHECK(seq.linecount() == 3);
    expect_line_offset(seq, 1, 3);
    expect_line_offset(seq, 2, 5);
}

void linecount_restores_on_undo_redo()
{
    sequence seq;
    init(seq, "abc");

    CHECK(insert_bytes(seq, seq.size(), "\nxyz"));
    CHECK(seq.linecount() == 2);

    CHECK(seq.undo());
    CHECK(seq.linecount() == 1);

    CHECK(seq.redo());
    CHECK(seq.linecount() == 2);
    expect_line_offset(seq, 1, 4);
}

void linefromoffset_handles_basic_newlines()
{
    sequence seq;
    init(seq, "abc\nxyz");

    expect_line_from_offset(seq, 0, 0, 0);
    expect_line_from_offset(seq, 3, 0, 0);
    expect_line_from_offset(seq, 4, 1, 4);
    expect_line_from_offset(seq, 7, 1, 4);
}

void linefromoffset_handles_crlf()
{
    sequence seq;
    init(seq, "abc\r\nxyz");

    expect_line_from_offset(seq, 4, 0, 0);
    expect_line_from_offset(seq, 5, 1, 5);
    expect_line_from_offset(seq, 8, 1, 5);
}

void linefromoffset_handles_crlf_split_across_spans()
{
    sequence seq;
    init(seq, "abc\rxyz");

    CHECK(insert_bytes(seq, 4, "\n"));
    expect_content(seq, "abc\r\nxyz");
    expect_line_from_offset(seq, 4, 0, 0);
    expect_line_from_offset(seq, 5, 1, 5);
    expect_line_from_offset(seq, 8, 1, 5);
}

void open_file_renders_file_backed_content()
{
    sequence seq;
    TCHAR path[MAX_PATH];
    const char *text = "first\r\nsecond\nthird";

    CHECK(write_temp_file(text, strlen(text), path));
    CHECK(seq.open(path, true));
    expect_content(seq, text);
    CHECK(seq.linecount() == 3);
    expect_line_offset(seq, 1, 7);
    expect_line_offset(seq, 2, 14);

	seq.clear();
    DeleteFile(path);
}

void open_file_renders_across_view_boundary()
{
    sequence seq;
    TCHAR path[MAX_PATH];
    const size_t file_length = MEM_BLOCK_SIZE + 128;
    const size_w render_offset = MEM_BLOCK_SIZE - 60;
    const size_w render_length = 140;
    char *data = new char[file_length];
    std::string actual;

    for(size_t i = 0; i < file_length; i++)
        data[i] = static_cast<char>('A' + (i % 26));

    CHECK(write_temp_file(data, file_length, path));
    CHECK(seq.open(path, true));
    CHECK(seq.size() == file_length);

    actual.resize(render_length);
    CHECK(seq.render(render_offset, reinterpret_cast<seqchar *>(&actual[0]), render_length) == render_length);
    CHECK(memcmp(actual.data(), data + render_offset, render_length) == 0);

	seq.clear();
    DeleteFile(path);
    delete[] data;
}

void open_file_handles_crlf_across_scan_boundary()
{
    sequence seq;
    TCHAR path[MAX_PATH];
    const size_w scan_size = MEM_BLOCK_SIZE;
    const size_t file_length = scan_size + 16;
    char *data = new char[file_length];

    memset(data, 'A', file_length);
    data[scan_size - 1] = '\r';
    data[scan_size] = '\n';
    data[scan_size + 1] = 'B';

    CHECK(write_temp_file(data, file_length, path));
    CHECK(seq.open(path, true));
    CHECK(seq.linecount() == 2);
    expect_line_offset(seq, 1, scan_size + 1);

    seq.clear();
    DeleteFile(path);
    delete[] data;
}

struct test_case
{
    const char *name;
    void (*run)();
};

const test_case tests[] =
{
    { "insert_at_beginning", insert_at_beginning },
    { "insert_at_end", insert_at_end },
    { "insert_on_span_boundary", insert_on_span_boundary },
    { "insert_in_middle_of_span", insert_in_middle_of_span },
    { "delete_starts_on_span_boundary", delete_starts_on_span_boundary },
    { "delete_ends_on_span_boundary", delete_ends_on_span_boundary },
    { "delete_entire_file_with_multiple_spans", delete_entire_file_with_multiple_spans },
    { "delete_starts_and_stops_within_single_span", delete_starts_and_stops_within_single_span },
    { "delete_across_multiple_spans", delete_across_multiple_spans },
    { "replace_at_beginning", replace_at_beginning },
    { "replace_at_end", replace_at_end },
    { "replace_on_span_boundary", replace_on_span_boundary },
    { "replace_in_middle_of_span", replace_in_middle_of_span },
    { "replace_across_multiple_spans", replace_across_multiple_spans },
    { "consecutive_insert_undo_coalesces", consecutive_insert_undo_coalesces },
    { "breakopt_prevents_insert_coalescing", breakopt_prevents_insert_coalescing },
    { "forward_delete_undo_coalesces", forward_delete_undo_coalesces },
    { "backward_delete_undo_coalesces", backward_delete_undo_coalesces },
    { "replace_overrun_clamps_to_end", replace_overrun_clamps_to_end },
    { "replace_shorter_than_erased", replace_shorter_than_erased },
    { "replace_longer_than_erased", replace_longer_than_erased },
    { "undo_redo_insert_middle_split", undo_redo_insert_middle_split },
    { "undo_redo_delete_within_split_span", undo_redo_delete_within_split_span },
    { "undo_redo_replace_across_spans", undo_redo_replace_across_spans },
    { "grouped_operations_undo_redo_together", grouped_operations_undo_redo_together },
    { "nested_group_survives_inner_replace", nested_group_survives_inner_replace },
    { "redo_invalidated_by_new_edit", redo_invalidated_by_new_edit },
    { "invalid_operations_do_not_modify_content", invalid_operations_do_not_modify_content },
    { "render_past_end_returns_available_bytes", render_past_end_returns_available_bytes },
    { "linecount_handles_basic_newlines", linecount_handles_basic_newlines },
    { "linecount_handles_crlf", linecount_handles_crlf },
    { "linecount_handles_crlf_split_across_spans", linecount_handles_crlf_split_across_spans },
    { "linecount_updates_after_insert_delete_replace", linecount_updates_after_insert_delete_replace },
    { "linecount_restores_on_undo_redo", linecount_restores_on_undo_redo },
    { "linefromoffset_handles_basic_newlines", linefromoffset_handles_basic_newlines },
    { "linefromoffset_handles_crlf", linefromoffset_handles_crlf },
    { "linefromoffset_handles_crlf_split_across_spans", linefromoffset_handles_crlf_split_across_spans },
    { "open_file_renders_file_backed_content", open_file_renders_file_backed_content },
    { "open_file_renders_across_view_boundary", open_file_renders_across_view_boundary },
    { "open_file_handles_crlf_across_scan_boundary", open_file_handles_crlf_across_scan_boundary },
};
}

int main()
{
    for(size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
    {
        int before = g_failures;
        tests[i].run();
        printf("%s %s\n", before == g_failures ? "ok" : "FAIL", tests[i].name);
    }

    if(g_failures)
    {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }

    printf("All sequence tests passed\n");
    return 0;
}
