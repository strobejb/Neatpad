#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>

#include <stdio.h>
#include <string.h>

#include <string>

#include "../src/TextView/sequence.h"
#include "../src/TextView/TextDocument.h"
#include "../src/TextView/TextView.h"

// Test access to a document's sequence, for line-number lookups (such as where
// line N starts) that TextDocument itself doesn't offer
class TextDocumentLineIndexProbe
{
public:
    static sequence &seq(TextDocument &doc)
    {
        return doc.m_seq;
    }

    static ULONG header(TextDocument &doc)
    {
        return static_cast<ULONG>(doc.m_nHeaderSize);
    }
};

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

bool write_large_pattern_file(const char *line, size_t line_length, size_w line_count, TCHAR path[MAX_PATH])
{
    TCHAR temp_path[MAX_PATH];
    HANDLE hFile;
    DWORD written = 0;
    size_w file_length = static_cast<size_w>(line_length) * line_count;
    size_w first_length = min(file_length, MEM_BLOCK_SIZE);
    size_w tail_length = min(file_length, MEM_BLOCK_SIZE);
    size_w tail_offset = file_length - tail_length;
    char *block = new char[MEM_BLOCK_SIZE];
    LARGE_INTEGER pos;
    bool ok = false;

    path[0] = 0;

    if(block == 0)
        return false;

    if(GetTempPath(MAX_PATH, temp_path) == 0)
        goto cleanup;

    if(GetTempFileName(temp_path, TEXT("seq"), 0, path) == 0)
        goto cleanup;

    hFile = CreateFile(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, 0);

    if(hFile == INVALID_HANDLE_VALUE)
    {
        DeleteFile(path);
        path[0] = 0;
        goto cleanup;
    }

    for(size_w i = 0; i < first_length; i++)
        block[i] = line[i % line_length];

    ok = WriteFile(hFile, block, static_cast<DWORD>(first_length), &written, 0) && written == first_length;

    if(ok && tail_offset > first_length)
    {
        pos.QuadPart = tail_offset;
        ok = SetFilePointerEx(hFile, pos, 0, FILE_BEGIN) != 0;
    }

    if(ok)
    {
        for(size_w i = 0; i < tail_length; i++)
            block[i] = line[(tail_offset + i) % line_length];

        ok = WriteFile(hFile, block, static_cast<DWORD>(tail_length), &written, 0) && written == tail_length;
    }

    CloseHandle(hFile);

    if(!ok)
    {
        DeleteFile(path);
        path[0] = 0;
    }

cleanup:
    delete[] block;
    return ok;
}

bool write_numbered_lines_file(size_t line_count, TCHAR path[MAX_PATH])
{
    TCHAR temp_path[MAX_PATH];
    HANDLE hFile;
    char line[128];
    bool ok = false;

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

    ok = true;

    for(size_t i = 0; i < line_count; i++)
    {
        DWORD written = 0;
        int len = sprintf_s(line, sizeof(line), "line %u abcdefghijklmnopqrstuvwxyz 0123456789\r\n", static_cast<unsigned>(i));

        if(!WriteFile(hFile, line, static_cast<DWORD>(len), &written, 0) || written != static_cast<DWORD>(len))
        {
            ok = false;
            break;
        }
    }

    CloseHandle(hFile);

    if(!ok)
    {
        DeleteFile(path);
        path[0] = 0;
    }

    return ok;
}

// A file just over 4GB: text at the start and the end, and zeros between them
const unsigned long long LARGE_FILE_SIZE = 0x100000000ULL + 0x100000;
const char LARGE_FILE_HEAD[] = "first line\r\n";
const char LARGE_FILE_TAIL[] = "\r\nsecond to last\r\nlast line";

// Write such a file sparsely, so only the text takes disk space. Returns false
// where the volume can't make sparse files; the tests that need one then skip.
bool write_sparse_file(const char *head, const char *tail, unsigned long long size, TCHAR path[MAX_PATH])
{
    TCHAR temp_path[MAX_PATH];
    LARGE_INTEGER pos;
    HANDLE hFile;
    DWORD bytes;
    bool ok;

    path[0] = 0;

    if(GetTempPath(MAX_PATH, temp_path) == 0 || GetTempFileName(temp_path, TEXT("seq"), 0, path) == 0)
        return false;

    hFile = CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);

    if(hFile == INVALID_HANDLE_VALUE)
    {
        DeleteFile(path);
        return false;
    }

    pos.QuadPart = static_cast<LONGLONG>(size - strlen(tail));

    ok = DeviceIoControl(hFile, FSCTL_SET_SPARSE, 0, 0, 0, 0, &bytes, 0) &&
         WriteFile(hFile, head, static_cast<DWORD>(strlen(head)), &bytes, 0) &&
         SetFilePointerEx(hFile, pos, 0, FILE_BEGIN) &&
         WriteFile(hFile, tail, static_cast<DWORD>(strlen(tail)), &bytes, 0);

    CloseHandle(hFile);

    if(!ok)
        DeleteFile(path);

    return ok;
}

enum DocTestEncoding
{
    DOC_ASCII,
    DOC_UTF8_BOM,
    DOC_UTF16LE_BOM,
    DOC_UTF16BE_BOM
};

bool write_textdocument_file(const char *text, DocTestEncoding encoding, TCHAR path[MAX_PATH])
{
    std::string bytes;
    size_t len = strlen(text);

    switch(encoding)
    {
    case DOC_UTF8_BOM:
        bytes.push_back(static_cast<char>(0xef));
        bytes.push_back(static_cast<char>(0xbb));
        bytes.push_back(static_cast<char>(0xbf));
        break;

    case DOC_UTF16LE_BOM:
        bytes.push_back(static_cast<char>(0xff));
        bytes.push_back(static_cast<char>(0xfe));
        break;

    case DOC_UTF16BE_BOM:
        bytes.push_back(static_cast<char>(0xfe));
        bytes.push_back(static_cast<char>(0xff));
        break;

    case DOC_ASCII:
    default:
        break;
    }

    for(size_t i = 0; i < len; i++)
    {
        unsigned char ch = static_cast<unsigned char>(text[i]);

        if(encoding == DOC_UTF16LE_BOM)
        {
            bytes.push_back(static_cast<char>(ch));
            bytes.push_back(0);
        }
        else if(encoding == DOC_UTF16BE_BOM)
        {
            bytes.push_back(0);
            bytes.push_back(static_cast<char>(ch));
        }
        else
        {
            bytes.push_back(static_cast<char>(ch));
        }
    }

    return write_temp_file(bytes.data(), bytes.size(), path);
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

void expect_offset_from_lineno(const sequence &seq, size_w line, size_w expected)
{
    size_w actual = static_cast<size_w>(-1);
    CHECK(seq.offset_from_lineno(line, &actual));
    CHECK(actual == expected);
}

void expect_lineno_from_offset(const sequence &seq, size_w offset, size_w expected_line, size_w expected_line_offset)
{
    size_w actual_line = static_cast<size_w>(-1);
    size_w actual_line_offset = static_cast<size_w>(-1);

    CHECK(seq.lineno_from_offset(offset, &actual_line, &actual_line_offset));
    CHECK(actual_line == expected_line);
    CHECK(actual_line_offset == expected_line_offset);
}

void expect_line_bounds_from_offset(sequence &seq, size_w offset, size_w expected_line_offset, size_w expected_next_offset)
{
	size_w actual_line_offset = static_cast<size_w>(-1);
	size_w actual_next_offset = static_cast<size_w>(-1);

	CHECK(seq.line_bounds_from_offset(offset, &actual_line_offset, &actual_next_offset));
	CHECK(actual_line_offset == expected_line_offset);
	CHECK(actual_next_offset == expected_next_offset);
}

// Bytes per character of the plain test text in each encoding
ULONG unit_size(DocTestEncoding encoding)
{
    return encoding == DOC_UTF16LE_BOM || encoding == DOC_UTF16BE_BOM ? 2 : 1;
}

// The start of line 'line', reached by walking down from the top of the document
TextCoord doc_line(TextDocument &doc, ULONG line)
{
    TextCoord top;
    TextCoord coord;

    CHECK(doc.coord_from_byte_anchor(0, &top));
    CHECK(doc.next_line_from_coord(&top, line, &coord));
    return coord;
}

void expect_doc_line_begin(TextDocument &doc, ULONG line, ULONG expected)
{
    TextCoord coord = doc_line(doc, line);
    bool exact = false;

    CHECK(coord.line_begin == expected);
    CHECK(doc.lineno_from_coord(&coord, &exact) == line);
}

// Walking down past the last line stays on it
void expect_doc_no_line(TextDocument &doc, ULONG line)
{
    CHECK(doc_line(doc, line).line_begin == doc_line(doc, line - 1).line_begin);
}

void expect_doc_line_at(TextDocument &doc, ULONG offset, ULONG expected_line, ULONG expected_line_begin)
{
    TextCoord coord;
    bool exact = false;

    CHECK(doc.coord_from_byte_anchor(offset, &coord));
    CHECK(coord.line_begin == expected_line_begin);
    CHECK(doc.lineno_from_coord(&coord, &exact) == expected_line);
}

// Edits at document byte offsets
size_w doc_insert(TextDocument &doc, size_w offset, TCHAR *text, ULONG length, TextChange *change = 0)
{
    TextCoord at;

    CHECK(doc.coord_from_byte_anchor(offset, &at));
    return doc.insert_text(&at, text, length, change);
}

size_w doc_erase(TextDocument &doc, size_w from, size_w to, TextChange *change = 0)
{
    TextCoord start;
    TextCoord end;

    CHECK(doc.coord_from_byte_anchor(from, &start));
    CHECK(doc.coord_from_byte_anchor(to, &end));
    return doc.erase_text(&start, &end, change);
}

size_w doc_replace(TextDocument &doc, size_w from, size_w to, TCHAR *text, ULONG length, TextChange *change = 0)
{
    TextCoord start;
    TextCoord end;

    CHECK(doc.coord_from_byte_anchor(from, &start));
    CHECK(doc.coord_from_byte_anchor(to, &end));
    return doc.replace_text(&start, &end, text, length, change);
}

// A sequence offset as a document byte offset. The sequence's first line
// starts at 0, before the BOM; the document's starts after it.
size_w doc_offset(TextDocument &doc, size_w sequence_offset)
{
    size_w header = TextDocumentLineIndexProbe::header(doc);

    return sequence_offset <= header ? 0 : sequence_offset - header;
}

// The line found by walking the document agrees with the sequence's line index
void expect_sequence_line_matches_textdocument(TextDocument &doc, ULONG line)
{
    sequence &seq = TextDocumentLineIndexProbe::seq(doc);
    TextCoord coord = doc_line(doc, line);
    size_w line_offset = 0;
    size_w next_offset = 0;

    CHECK(seq.offset_from_lineno(line, &line_offset));

    if(!seq.next_line_from_offset(line_offset, &next_offset))
        next_offset = seq.size();

    CHECK(coord.line_begin == doc_offset(doc, line_offset));
    CHECK(coord.line_next == doc_offset(doc, next_offset));
}

// The line found around an offset agrees with the sequence's line index
void expect_sequence_offset_matches_textdocument(TextDocument &doc, ULONG offset)
{
    sequence &seq = TextDocumentLineIndexProbe::seq(doc);
    ULONG header = TextDocumentLineIndexProbe::header(doc);
    TextCoord coord;
    size_w line = 0;
    size_w line_offset = 0;
    bool exact = false;

    CHECK(doc.coord_from_byte_anchor(offset, &coord));
    CHECK(seq.lineno_from_offset(offset + header, &line, &line_offset));
    CHECK(doc.lineno_from_coord(&coord, &exact) == line);
    CHECK(coord.line_begin == doc_offset(doc, line_offset));
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

void undo_redo_reports_changed_range()
{
    sequence seq;
    size_w offset = 0;
    size_w erased = 0;
    size_w inserted = 0;

    init(seq, "abcdef");

    CHECK(insert_bytes(seq, 3, "XX"));
    CHECK(seq.undo());
    seq.event_change(&offset, &erased, &inserted);
    CHECK(offset == 3 && erased == 2 && inserted == 0);

    CHECK(seq.redo());
    seq.event_change(&offset, &erased, &inserted);
    CHECK(offset == 3 && erased == 0 && inserted == 2);

    // A replace is an erase and an insert grouped together; undo reports one range covering both.
    seq.breakopt();
    CHECK(replace_bytes(seq, 2, "Q", 4));
    expect_content(seq, "abQef");
    CHECK(seq.undo());
    expect_content(seq, "abcXXdef");
    seq.event_change(&offset, &erased, &inserted);
    CHECK(offset == 2 && erased == 1 && inserted == 4);
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
    expect_offset_from_lineno(seq, 0, 0);
    expect_offset_from_lineno(seq, 1, 4);
    CHECK(!seq.offset_from_lineno(2, 0));
}

void linecount_handles_crlf()
{
    sequence seq;
    init(seq, "abc\r\nxyz\nlast");

    CHECK(seq.linecount() == 3);
    expect_offset_from_lineno(seq, 0, 0);
    expect_offset_from_lineno(seq, 1, 5);
    expect_offset_from_lineno(seq, 2, 9);
}

void linecount_handles_crlf_split_across_spans()
{
    sequence seq;
    init(seq, "abc\rxyz");

    CHECK(insert_bytes(seq, 4, "\n"));
    expect_content(seq, "abc\r\nxyz");
    CHECK(seq.linecount() == 2);
    expect_offset_from_lineno(seq, 1, 5);
}

void linecount_updates_after_insert_delete_replace()
{
    sequence seq;
    init(seq, "abc");

    CHECK(seq.linecount() == 1);
    CHECK(insert_bytes(seq, 1, "\nX"));
    expect_content(seq, "a\nXbc");
    CHECK(seq.linecount() == 2);
    expect_offset_from_lineno(seq, 1, 2);

    CHECK(seq.erase(1, 2));
    expect_content(seq, "abc");
    CHECK(seq.linecount() == 1);

    CHECK(replace_bytes(seq, 1, "\r\nZ\n", 1));
    expect_content(seq, "a\r\nZ\nc");
    CHECK(seq.linecount() == 3);
    expect_offset_from_lineno(seq, 1, 3);
    expect_offset_from_lineno(seq, 2, 5);
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
    expect_offset_from_lineno(seq, 1, 4);
}

void lineno_from_offset_handles_basic_newlines()
{
    sequence seq;
    init(seq, "abc\nxyz");

    expect_lineno_from_offset(seq, 0, 0, 0);
    expect_lineno_from_offset(seq, 3, 0, 0);
    expect_lineno_from_offset(seq, 4, 1, 4);
    expect_lineno_from_offset(seq, 7, 1, 4);
}

void lineno_from_offset_handles_crlf()
{
    sequence seq;
    init(seq, "abc\r\nxyz");

    expect_lineno_from_offset(seq, 4, 0, 0);
    expect_lineno_from_offset(seq, 5, 1, 5);
    expect_lineno_from_offset(seq, 8, 1, 5);
}

void lineno_from_offset_handles_crlf_split_across_spans()
{
    sequence seq;
    init(seq, "abc\rxyz");

    CHECK(insert_bytes(seq, 4, "\n"));
    expect_content(seq, "abc\r\nxyz");
    expect_lineno_from_offset(seq, 4, 0, 0);
    expect_lineno_from_offset(seq, 5, 1, 5);
    expect_lineno_from_offset(seq, 8, 1, 5);
}

void linecount_handles_crlf_split_by_insert()
{
    sequence seq;
    init(seq, "a\r\nb");

    CHECK(insert_bytes(seq, 2, "X"));
    expect_content(seq, "a\rX\nb");
    CHECK(seq.linecount() == 3);
    expect_offset_from_lineno(seq, 1, 2);
    expect_offset_from_lineno(seq, 2, 4);
    expect_lineno_from_offset(seq, 3, 1, 2);

    CHECK(seq.erase(2, 1));
    expect_content(seq, "a\r\nb");
    CHECK(seq.linecount() == 2);
    expect_offset_from_lineno(seq, 1, 3);
}

void linecount_handles_cr_lf_adjacent_in_modify_buffer()
{
    sequence seq;
    init(seq, "hello");

    // Two unrelated inserts sit next to each other in the modify buffer as "\r\n".
    CHECK(insert_bytes(seq, 0, "foo\r"));
    CHECK(insert_bytes(seq, seq.size(), "\nbar"));
    CHECK(insert_bytes(seq, 1, "Q"));
    expect_content(seq, "fQoo\rhello\nbar");
    CHECK(seq.linecount() == 3);
    expect_offset_from_lineno(seq, 1, 5);
    expect_offset_from_lineno(seq, 2, 11);
}

void line_bounds_from_offset_handles_crlf_boundaries()
{
	sequence seq;
	init(seq, "abc\r\nxyz\nlast");

	expect_line_bounds_from_offset(seq, 0, 0, 5);
	expect_line_bounds_from_offset(seq, 3, 0, 5);
	expect_line_bounds_from_offset(seq, 4, 0, 5);
	expect_line_bounds_from_offset(seq, 5, 5, 9);
	expect_line_bounds_from_offset(seq, 8, 5, 9);
	expect_line_bounds_from_offset(seq, 9, 9, 13);
	expect_line_bounds_from_offset(seq, 13, 9, 13);
}

void line_bounds_from_offset_handles_long_lazy_file_line()
{
	sequence seq;
	TCHAR path[MAX_PATH];
	const size_w file_length = MEM_BLOCK_SIZE * 3;
	const size_w first_break = MEM_BLOCK_SIZE + 17;
	const size_w second_break = MEM_BLOCK_SIZE * 2 + 91;
	char *data = new char[file_length];

	memset(data, 'A', file_length);
	data[first_break] = '\n';
	data[second_break] = '\n';

	CHECK(write_temp_file(data, file_length, path));
	CHECK(seq.open(path, false));

	CHECK(!seq.linecount_known());
	expect_line_bounds_from_offset(seq, first_break + 100, first_break + 1, second_break + 1);
	expect_line_bounds_from_offset(seq, second_break, first_break + 1, second_break + 1);
	expect_line_bounds_from_offset(seq, second_break + 100, second_break + 1, file_length);

	seq.clear();
	DeleteFile(path);
	delete[] data;
}

void line_scan_mode_handles_utf16le_crlf()
{
    const seqchar text[] = { 'a', 0, '\r', 0, '\n', 0, 'b', 0 };
    sequence seq;

    CHECK(seq.init(text, sizeof(text)));
    seq.set_line_scan_mode(sequence::line_scan_utf16le);

    CHECK(seq.linecount() == 2);
    expect_offset_from_lineno(seq, 0, 0);
    expect_offset_from_lineno(seq, 1, 6);
    expect_lineno_from_offset(seq, 4, 0, 0);
    expect_lineno_from_offset(seq, 6, 1, 6);
}

void line_scan_mode_handles_utf16be_crlf()
{
    const seqchar text[] = { 0, 'a', 0, '\r', 0, '\n', 0, 'b' };
    sequence seq;

    CHECK(seq.init(text, sizeof(text)));
    seq.set_line_scan_mode(sequence::line_scan_utf16be);

    CHECK(seq.linecount() == 2);
    expect_offset_from_lineno(seq, 1, 6);
    expect_lineno_from_offset(seq, 4, 0, 0);
    expect_lineno_from_offset(seq, 6, 1, 6);
}

void line_scan_mode_handles_utf32le_crlf()
{
    const seqchar text[] =
    {
        'a', 0, 0, 0,
        '\r', 0, 0, 0,
        '\n', 0, 0, 0,
        'b', 0, 0, 0
    };
    sequence seq;

    CHECK(seq.init(text, sizeof(text)));
    seq.set_line_scan_mode(sequence::line_scan_utf32le);

    CHECK(seq.linecount() == 2);
    expect_offset_from_lineno(seq, 1, 12);
    expect_lineno_from_offset(seq, 8, 0, 0);
    expect_lineno_from_offset(seq, 12, 1, 12);
}

void line_scan_mode_handles_utf32be_crlf()
{
    const seqchar text[] =
    {
        0, 0, 0, 'a',
        0, 0, 0, '\r',
        0, 0, 0, '\n',
        0, 0, 0, 'b'
    };
    sequence seq;

    CHECK(seq.init(text, sizeof(text)));
    seq.set_line_scan_mode(sequence::line_scan_utf32be);

    CHECK(seq.linecount() == 2);
    expect_offset_from_lineno(seq, 1, 12);
    expect_lineno_from_offset(seq, 8, 0, 0);
    expect_lineno_from_offset(seq, 12, 1, 12);
}

void line_scan_mode_handles_utf16le_crlf_split_across_spans()
{
    const seqchar text[] = { 'a', 0, '\r', 0, 'b', 0 };
    const seqchar lf[] = { '\n', 0 };
    sequence seq;

    CHECK(seq.init(text, sizeof(text)));
    seq.set_line_scan_mode(sequence::line_scan_utf16le);
    CHECK(seq.insert(4, lf, sizeof(lf)));

    CHECK(seq.linecount() == 2);
    expect_offset_from_lineno(seq, 1, 6);
    expect_lineno_from_offset(seq, 4, 0, 0);
    expect_lineno_from_offset(seq, 6, 1, 6);
}

void textdocument_linecount_handles_basic_newlines()
{
    const DocTestEncoding encodings[] = { DOC_ASCII, DOC_UTF8_BOM, DOC_UTF16LE_BOM, DOC_UTF16BE_BOM };

    for(size_t i = 0; i < sizeof(encodings) / sizeof(encodings[0]); i++)
    {
        TextDocument doc;
        TCHAR path[MAX_PATH];
        ULONG unit = unit_size(encodings[i]);

        CHECK(write_textdocument_file("abc\n", encodings[i], path));
        CHECK(doc.init(path));

        CHECK(doc.linecount() == 2);
        expect_doc_line_begin(doc, 0, 0);
        expect_doc_line_begin(doc, 1, 4 * unit);
        expect_doc_no_line(doc, 2);

        doc.clear();
        DeleteFile(path);
    }
}

void textdocument_linecount_handles_crlf()
{
    const DocTestEncoding encodings[] = { DOC_ASCII, DOC_UTF8_BOM, DOC_UTF16LE_BOM, DOC_UTF16BE_BOM };

    for(size_t i = 0; i < sizeof(encodings) / sizeof(encodings[0]); i++)
    {
        TextDocument doc;
        TCHAR path[MAX_PATH];
        ULONG unit = unit_size(encodings[i]);

        CHECK(write_textdocument_file("abc\r\nxyz\nlast", encodings[i], path));
        CHECK(doc.init(path));

        CHECK(doc.linecount() == 3);
        expect_doc_line_begin(doc, 0, 0);
        expect_doc_line_begin(doc, 1, 5 * unit);
        expect_doc_line_begin(doc, 2, 9 * unit);

        expect_doc_line_at(doc, 4 * unit, 0, 0);
        expect_doc_line_at(doc, 5 * unit, 1, 5 * unit);
        expect_doc_line_at(doc, 8 * unit, 1, 5 * unit);
        expect_doc_line_at(doc, 9 * unit, 2, 9 * unit);

        doc.clear();
        DeleteFile(path);
    }
}

void textdocument_linecount_handles_crlf_split_across_lazy_page()
{
    const DocTestEncoding encodings[] = { DOC_ASCII, DOC_UTF8_BOM, DOC_UTF16LE_BOM, DOC_UTF16BE_BOM };

    for(size_t i = 0; i < sizeof(encodings) / sizeof(encodings[0]); i++)
    {
        TextDocument doc;
        TCHAR path[MAX_PATH];
        ULONG cr_offset = encodings[i] == DOC_UTF16LE_BOM || encodings[i] == DOC_UTF16BE_BOM
                            ? MEM_BLOCK_SIZE / 2 - 1
                            : MEM_BLOCK_SIZE - 1;
        ULONG unit = unit_size(encodings[i]);
        ULONG next_line = (cr_offset + 2) * unit;
        char *text = new char[cr_offset + 4];

        memset(text, 'a', cr_offset);
        text[cr_offset] = '\r';
        text[cr_offset + 1] = '\n';
        text[cr_offset + 2] = 'z';
        text[cr_offset + 3] = 0;

        CHECK(write_textdocument_file(text, encodings[i], path));
        CHECK(doc.init(path));

        printf("split %u line_offset\n", static_cast<unsigned>(i)); fflush(stdout);
        expect_doc_line_begin(doc, 1, next_line);
        printf("split %u before_next\n", static_cast<unsigned>(i)); fflush(stdout);
        expect_doc_line_at(doc, next_line - unit, 0, 0);
        printf("split %u at_next\n", static_cast<unsigned>(i)); fflush(stdout);
        expect_doc_line_at(doc, next_line, 1, next_line);
        printf("split %u done\n", static_cast<unsigned>(i)); fflush(stdout);

        doc.clear();
        DeleteFile(path);
        delete[] text;
    }
}

void textdocument_linecount_updates_after_insert_delete_replace()
{
    const DocTestEncoding encodings[] = { DOC_ASCII, DOC_UTF8_BOM, DOC_UTF16LE_BOM, DOC_UTF16BE_BOM };
    TCHAR insert_text[] = TEXT("\nX");
    TCHAR replace_text[] = TEXT("\r\nZ\n");

    for(size_t i = 0; i < sizeof(encodings) / sizeof(encodings[0]); i++)
    {
        TextDocument doc;
        TCHAR path[MAX_PATH];
        ULONG unit = unit_size(encodings[i]);

        CHECK(write_textdocument_file("abc", encodings[i], path));
        CHECK(doc.init(path));

        CHECK(doc.linecount() == 1);
        CHECK(doc_insert(doc, 1 * unit, insert_text, 2) != 0);
        CHECK(doc.linecount() == 2);
        expect_doc_line_begin(doc, 1, 2 * unit);

        CHECK(doc_erase(doc, 1 * unit, 3 * unit) == 2 * unit);
        CHECK(doc.linecount() == 1);

        CHECK(doc_replace(doc, 1 * unit, 2 * unit, replace_text, 4) != 0);
        CHECK(doc.linecount() == 3);
        expect_doc_line_begin(doc, 1, 3 * unit);
        expect_doc_line_begin(doc, 2, 5 * unit);

        doc.clear();
        DeleteFile(path);
    }
}

void textdocument_linecount_restores_on_undo_redo()
{
    const DocTestEncoding encodings[] = { DOC_ASCII, DOC_UTF8_BOM, DOC_UTF16LE_BOM, DOC_UTF16BE_BOM };
    TCHAR insert_text[] = TEXT("\nxyz");

    for(size_t i = 0; i < sizeof(encodings) / sizeof(encodings[0]); i++)
    {
        TextDocument doc;
        TCHAR path[MAX_PATH];
        ULONG unit = unit_size(encodings[i]);
        TextCoord end;
        TextChange change;

        CHECK(write_textdocument_file("abc", encodings[i], path));
        CHECK(doc.init(path));

        CHECK(doc.coord_from_document_end(&end));
        CHECK(doc.insert_text(&end, insert_text, 4, 0) != 0);
        CHECK(doc.linecount() == 2);

        CHECK(doc.undo(&change));
        CHECK(doc.linecount() == 1);

        CHECK(doc.redo(&change));
        CHECK(doc.linecount() == 2);
        expect_doc_line_begin(doc, 1, 4 * unit);

        doc.clear();
        DeleteFile(path);
    }
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
    expect_offset_from_lineno(seq, 1, 7);
    expect_offset_from_lineno(seq, 2, 14);

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
    expect_offset_from_lineno(seq, 1, scan_size + 1);

    seq.clear();
    DeleteFile(path);
    delete[] data;
}

void lazy_file_insert_crlf_updates_visible_line_offsets()
{
    sequence seq;
    TCHAR path[MAX_PATH];
    const char *line = "line abcdefghijklmnopqrstuvwxyz 0123456789\r\n";
    const size_t line_length = strlen(line);
    const size_t line_count = MEM_BLOCK_SIZE / line_length + 32;
    const size_t file_length = line_length * line_count;
    char *data = new char[file_length];
    size_w insert_offset = line_length * 9 + 37;
    size_w line_offset = 0;

    for(size_t i = 0; i < line_count; i++)
        memcpy(data + i * line_length, line, line_length);

    CHECK(write_temp_file(data, file_length, path));
    CHECK(seq.open(path, true));

    CHECK(seq.insert(insert_offset, reinterpret_cast<const seqchar *>("\r\n"), 2));
    CHECK(seq.offset_from_lineno(10, &line_offset));
    CHECK(line_offset == insert_offset + 2);
    CHECK(seq.offset_from_lineno(11, &line_offset));
    CHECK(line_offset == line_length * 10 + 2);

    seq.clear();
    DeleteFile(path);
    delete[] data;
}

void lazy_file_fully_indexed_keeps_exact_lines()
{
    sequence seq;
    TCHAR path[MAX_PATH];
    const char *line = "line abcdefghijklmnopqrstuvwxyz 0123456789\r\n";
    const size_t line_length = strlen(line);
    const size_t line_count = (MEM_BLOCK_SIZE * 3) / line_length;
    const size_t file_length = line_length * line_count;
    char *data = new char[file_length];
    size_w line_no = 0;
    size_w line_offset = 0;

    for(size_t i = 0; i < line_count; i++)
        memcpy(data + i * line_length, line, line_length);

    CHECK(write_temp_file(data, file_length, path));
    CHECK(seq.open(path, true));
    CHECK(!seq.linecount_known());

    seq.index_lines(0, seq.size());
    CHECK(seq.linecount_known());
    CHECK(seq.linecount() == line_count + 1);

    CHECK(seq.insert(line_length * 5000 + 3, reinterpret_cast<const seqchar *>("\r\n"), 2));
    CHECK(seq.linecount() == line_count + 2);
    CHECK(seq.lineno_from_offset(line_length * 6000 + 2, &line_no, &line_offset));
    CHECK(line_no == 6001);
    CHECK(line_offset == line_length * 6000 + 2);

    seq.clear();
    DeleteFile(path);
    delete[] data;
}

void lazy_line_numbers_follow_edits_above()
{
    sequence seq;
    TCHAR path[MAX_PATH];
    const char *line = "line abcdefghijklmnopqrstuvwxyz 0123456789\r\n";
    const size_t line_length = strlen(line);
    const size_t line_count = (MEM_BLOCK_SIZE * 3) / line_length;
    const size_t file_length = line_length * line_count;
    const size_w probe = 6 + line_length * 8000;
    char *data = new char[file_length];
    size_w line_no = 0;
    size_w line_offset = 0;

    for(size_t i = 0; i < line_count; i++)
        memcpy(data + i * line_length, line, line_length);

    CHECK(write_temp_file(data, file_length, path));
    CHECK(seq.open(path, true));
    CHECK(seq.insert(0, reinterpret_cast<const seqchar *>("\r\n\r\n\r\n"), 6));

    seq.index_lines(0, probe + line_length);
    CHECK(seq.lineno_known(8003));
    CHECK(seq.lineno_from_offset(probe, &line_no, &line_offset));
    CHECK(line_no == 8003);
    CHECK(line_offset == probe);
    CHECK(seq.offset_from_lineno(8003, &line_offset));
    CHECK(line_offset == probe);

    seq.clear();
    DeleteFile(path);
    delete[] data;
}

void textdocument_coord_handles_lazy_end_after_insert()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    const char *line = "line abcdefghijklmnopqrstuvwxyz 0123456789\r\n";
    const size_t line_length = strlen(line);
    const size_t line_count = MEM_BLOCK_SIZE / line_length + 32;
    const size_t file_length = line_length * line_count;
    char *data = new char[file_length];
    TextCoord eof;
    TCHAR crlf[] = TEXT("\r\n");

    for(size_t i = 0; i < line_count; i++)
        memcpy(data + i * line_length, line, line_length);

    CHECK(write_temp_file(data, file_length, path));
    CHECK(doc.init(path));

    CHECK(doc_insert(doc, static_cast<ULONG>(line_length * 9 + 37), crlf, 2) == 2);
    CHECK(doc.coord_from_document_end(&eof));
    CHECK(eof.line_begin == doc.byte_length());
    CHECK(eof.line_next == doc.byte_length());

    doc.clear();
    DeleteFile(path);
    delete[] data;
}

void textdocument_final_sparse_line_ignores_estimated_linecount()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    const size_t prefix_lines = MEM_BLOCK_SIZE / 3;
    const size_t prefix_length = prefix_lines * 3;
    const size_t file_length = MEM_BLOCK_SIZE * 4;
    const ULONG insert_offset = 9 * 3 + 1;
    const ULONG final_line_offset = static_cast<ULONG>(prefix_length + 2);
    char *data = new char[file_length];
    TextCoord coord;
    TCHAR crlf[] = TEXT("\r\n");

    memset(data, 'A', file_length);

    for(size_t i = 0; i < prefix_lines; i++)
    {
        data[i * 3] = 'x';
        data[i * 3 + 1] = '\r';
        data[i * 3 + 2] = '\n';
    }

    CHECK(write_temp_file(data, file_length, path));
    CHECK(doc.init(path));

    CHECK(doc_insert(doc, insert_offset, crlf, 2) == 2);
    CHECK(doc.coord_from_byte_anchor(final_line_offset, &coord));
    CHECK(coord.line_begin == final_line_offset);
    CHECK(coord.line_next == doc.byte_length());

    doc.clear();
    DeleteFile(path);
    delete[] data;
}

void textdocument_ctrl_end_uses_final_page_when_lines_are_lazy()
{
    TextDocument doc;
    sequence &seq = TextDocumentLineIndexProbe::seq(doc);
    TCHAR path[MAX_PATH];
    const char *line = "line abcdefghijklmnopqrstuvwxyz 0123456789\r\n";
    const size_t line_length = strlen(line);
    const size_t line_count = (MEM_BLOCK_SIZE * 8) / line_length;
    const size_t file_length = line_length * line_count;
    char *data = new char[file_length];
    ULONG target_line = static_cast<ULONG>(MEM_BLOCK_SIZE / line_length + 20);
    size_w line_offset = 0;
    TextCoord eof;
    TextCoord prev;
    TextCoord next;
    bool exact = true;

    for(size_t i = 0; i < line_count; i++)
        memcpy(data + i * line_length, line, line_length);

    CHECK(write_temp_file(data, file_length, path));
    CHECK(doc.init(path));

    // looking up a line just past the indexed region scans ahead and makes it exact
    CHECK(!seq.lineno_known(target_line));
    CHECK(seq.offset_from_lineno(target_line, &line_offset));
    CHECK(seq.lineno_known(target_line));

    // Ctrl+End reaches the end without counting the lines before it
    CHECK(doc.coord_from_document_end(&eof));
    CHECK(eof.line_begin == doc.byte_length());
    CHECK(eof.line_next == doc.byte_length());
    CHECK(!doc.linecount_known());
    doc.lineno_from_coord(&eof, &exact);
    CHECK(!exact);

    CHECK(doc.previous_line_from_coord(&eof, 1, &prev));
    CHECK(prev.line_begin == doc.byte_length() - line_length);
    CHECK(prev.line_next == doc.byte_length());
    CHECK(doc.next_line_from_coord(&prev, 1, &next));
    CHECK(next.line_begin == doc.byte_length());

    doc.clear();
    DeleteFile(path);
    delete[] data;
}

void textdocument_lazy_eof_line_number_maps_back_to_same_line()
{
    TextDocument doc;
    sequence &seq = TextDocumentLineIndexProbe::seq(doc);
    TCHAR path[MAX_PATH];
    const size_t line_count = 200000;
    TextCoord eof;
    size_w eof_line;
    size_w line_offset = 0;
    bool exact = true;

    CHECK(write_numbered_lines_file(line_count, path));
    CHECK(doc.init(path));

    // the estimated number of the last line leads back to that same line
    CHECK(doc.coord_from_document_end(&eof));
    eof_line = doc.lineno_from_coord(&eof, &exact);
    CHECK(seq.offset_from_lineno(eof_line, &line_offset));
    CHECK(doc_offset(doc, line_offset) == eof.line_begin);
    CHECK(!seq.offset_from_lineno(eof_line + 1, &line_offset));

    doc.clear();
    DeleteFile(path);
}

void textdocument_lazy_eof_without_final_crlf_maps_to_final_line()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    const char *line = "line abcdefghijklmnopqrstuvwxyz 0123456789\r\n";
    const char *last_line = "final abcdefghijklmnopqrstuvwxyz 0123456789";
    const size_t line_length = strlen(line);
    const size_t last_line_length = strlen(last_line);
    const size_t line_count = (MEM_BLOCK_SIZE * 8) / line_length;
    const size_t file_length = line_length * (line_count - 1) + last_line_length;
    char *data = new char[file_length];
    size_w last_begin;
    TextCoord eof;
    TextCoord coord;
    TextCoord prev;
    TextCoord next;

    for(size_t i = 0; i < line_count - 1; i++)
        memcpy(data + i * line_length, line, line_length);

    memcpy(data + line_length * (line_count - 1), last_line, last_line_length);

    CHECK(write_temp_file(data, file_length, path));
    CHECK(doc.init(path));

    CHECK(doc.coord_from_document_end(&eof));
    last_begin = eof.line_begin;
    CHECK(last_begin == file_length - last_line_length);
    CHECK(eof.line_next - eof.line_begin == last_line_length);

    // the CR and LF before the last line belong to the line above it
    CHECK(doc.coord_from_byte_anchor(last_begin - 1, &coord));
    CHECK(coord.line_begin == last_begin - line_length);
    CHECK(doc.coord_from_byte_anchor(last_begin - 2, &coord));
    CHECK(coord.line_begin == last_begin - line_length);

    CHECK(doc.previous_line_from_coord(&eof, 1, &prev));
    CHECK(prev.line_begin == last_begin - line_length);
    CHECK(prev.line_next == last_begin);
    CHECK(doc.next_line_from_coord(&prev, 1, &next));
    CHECK(next.line_begin == last_begin);
    CHECK(next.line_next == doc.byte_length());

    doc.clear();
    DeleteFile(path);
    delete[] data;
}

void textdocument_lazy_offset_lookup_uses_sequence_line_bounds()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    const size_t file_length = MEM_BLOCK_SIZE * 3;
    const ULONG first_break = MEM_BLOCK_SIZE + 17;
    const ULONG second_break = MEM_BLOCK_SIZE * 2 + 91;
    const ULONG target_offset = first_break + 100;
    char *data = new char[file_length];
    TextCoord coord;
    TextCoord prev;
    TextCoord next;
    bool exact = false;

    memset(data, 'A', file_length);
    data[first_break] = '\n';
    data[second_break] = '\n';

    CHECK(write_temp_file(data, file_length, path));
    CHECK(doc.init(path));
    CHECK(!doc.linecount_known());

    CHECK(doc.coord_from_byte_anchor(target_offset, &coord));
    CHECK(coord.line_begin == first_break + 1);
    CHECK(coord.line_next == second_break + 1);

    CHECK(doc.previous_line_from_coord(&coord, 1, &prev));
    CHECK(prev.line_begin == 0);
    CHECK(prev.line_next == first_break + 1);
    CHECK(doc.lineno_from_coord(&prev, &exact) == 0);
    CHECK(exact);

    CHECK(doc.next_line_from_coord(&coord, 1, &next));
    CHECK(next.line_begin == second_break + 1);
    CHECK(next.line_next == file_length);

    doc.clear();
    DeleteFile(path);
    delete[] data;
}

void textdocument_lazy_line_numbers_continue_after_first_page()
{
    TextDocument doc;
    sequence &seq = TextDocumentLineIndexProbe::seq(doc);
    TCHAR path[MAX_PATH];
    ULONG first_unknown = 0;

    CHECK(write_numbered_lines_file(20000, path));
    CHECK(doc.init(path));

    while(seq.lineno_known(first_unknown))
        first_unknown++;

    CHECK(first_unknown > 1000);

    for(ULONG line = first_unknown; line < first_unknown + 100; line++)
    {
        size_w line_offset = 0;
        TextCoord coord;
        bool exact = false;

        CHECK(seq.offset_from_lineno(line, &line_offset));
        CHECK(seq.lineno_known(line));

        CHECK(doc.coord_from_byte_anchor(doc_offset(doc, line_offset), &coord));
        CHECK(doc.lineno_from_coord(&coord, &exact) == line);
        CHECK(exact);
    }

    doc.clear();
    DeleteFile(path);
}

void textdocument_large_lazy_file_line_estimate_does_not_overflow()
{
    TextDocument doc;
    sequence &seq = TextDocumentLineIndexProbe::seq(doc);
    TCHAR path[MAX_PATH];
    const char *line = "line 0000000 abcdefghijklmnopqrstuvwxyz 0123456789\r\n";
    const size_t line_length = strlen(line);
    const size_w line_count = 5000000;
    size_w line_offset = 0;
    size_w next_offset = 0;
    TextCoord coord;
    bool exact = false;

    CHECK(write_large_pattern_file(line, line_length, line_count, path));
    CHECK(doc.init(path));

    CHECK(doc.linecount() > 4000000);

    CHECK(doc.coord_from_byte_anchor(0, &coord));
    CHECK(doc.lineno_from_coord(&coord, &exact) == 0);
    CHECK(exact);
    CHECK(doc.coord_from_byte_anchor(static_cast<ULONG>(line_length * 10), &coord));
    CHECK(doc.lineno_from_coord(&coord, &exact) == 10);
    CHECK(exact);

    CHECK(doc.coord_from_document_end(&coord));
    CHECK(doc.lineno_from_coord(&coord, &exact) > 4000000);
    CHECK(!exact);
    CHECK(coord.line_begin == doc.byte_length());
    CHECK(coord.line_next == doc.byte_length());

    CHECK(seq.offset_from_lineno(doc.linecount() - 2, &line_offset));
    CHECK(line_offset >= doc.byte_length() - MEM_BLOCK_SIZE);
    CHECK(seq.next_line_from_offset(line_offset, &next_offset));
    CHECK(next_offset - line_offset <= line_length);

    doc.clear();
    DeleteFile(path);
}

void open_file_larger_than_4gb()
{
    sequence seq;
    TCHAR path[MAX_PATH];
    const size_w size = LARGE_FILE_SIZE;
    const size_w tail_length = strlen(LARGE_FILE_TAIL);
    const size_w last_line = size - strlen("last line");
    char buf[64];
    size_w line_begin = 0;
    size_w line_next = 0;
    size_w lineno = 0;

    if(!write_sparse_file(LARGE_FILE_HEAD, LARGE_FILE_TAIL, LARGE_FILE_SIZE, path))
    {
        printf("open_file_larger_than_4gb: skipped, no sparse file support\n");
        return;
    }

    CHECK(size == LARGE_FILE_SIZE);
    CHECK(seq.open(path, true));
    CHECK(seq.size() == LARGE_FILE_SIZE);

    // reading beyond 4GB
    CHECK(seq.render(size - tail_length, reinterpret_cast<seqchar *>(buf), tail_length) == tail_length);
    CHECK(memcmp(buf, LARGE_FILE_TAIL, static_cast<size_t>(tail_length)) == 0);

    // lines beyond 4GB are found locally, and numbered by estimate
    CHECK(seq.line_bounds_from_offset(size - 3, &line_begin, &line_next));
    CHECK(line_begin == last_line);
    CHECK(line_next == size);
    CHECK(seq.lineno_from_offset(size, &lineno, &line_begin));
    CHECK(line_begin == last_line);
    CHECK(!seq.lineno_known_at(size));

    seq.clear();
    DeleteFile(path);
}

void textdocument_edits_beyond_4gb()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TCHAR bang[] = TEXT("!");
    const size_w size = LARGE_FILE_SIZE;
    const size_w last_line = size - strlen("last line");
    TextCoord eof;
    TextCoord prev;
    TextChange change;
    TCHAR buf[32];
    bool exact = true;

    if(!write_sparse_file(LARGE_FILE_HEAD, LARGE_FILE_TAIL, LARGE_FILE_SIZE, path))
    {
        printf("textdocument_edits_beyond_4gb: skipped, no sparse file support\n");
        return;
    }

    CHECK(size == LARGE_FILE_SIZE);
    CHECK(doc.init(path));
    CHECK(doc.byte_length() == LARGE_FILE_SIZE);

    CHECK(doc.coord_from_document_end(&eof));
    CHECK(eof.byte_anchor == size);
    CHECK(eof.line_begin == last_line);
    CHECK(eof.line_offset_chars == 9);
    doc.lineno_from_coord(&eof, &exact);
    CHECK(!exact);

    CHECK(doc.previous_line_from_coord(&eof, 1, &prev));
    CHECK(prev.line_next == last_line);
    CHECK(doc.getline(prev, buf, 32) == 16);
    CHECK(memcmp(buf, TEXT("second to last\r\n"), 16 * sizeof(TCHAR)) == 0);

    // an edit beyond 4GB, and a coordinate moved across it
    CHECK(doc.insert_text(&eof, bang, 1, &change) == 1);
    CHECK(change.offset == size && change.erased == 0 && change.inserted == 1);
    CHECK(doc.byte_length() == size + 1);
    CHECK(doc.coord_after_change(&eof, &change));
    CHECK(eof.byte_anchor == size);
    CHECK(eof.line_next == size + 1);

    CHECK(doc.undo(&change));
    CHECK(change.offset == size && change.erased == 1 && change.inserted == 0);
    CHECK(doc.byte_length() == size);

    doc.clear();
    DeleteFile(path);
}

void textdocument_utf16_coord_reads_line()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    const WCHAR text[] = { 0xfeff, 'a', '\r', '\n', 'b', 0xd83d, 0xde00, '\r', '\n', 'c' };
    TextCoord coord;
    TCHAR buf[16];
    bool exact = false;

    CHECK(write_temp_file(reinterpret_cast<const char *>(text), sizeof(text), path));
    CHECK(doc.init(path));

    CHECK(doc.getformat() == NCP_UTF16);
    CHECK(doc.byte_length() == 18);

    // the CR on the second line, after 'b' and a surrogate pair
    CHECK(doc.coord_from_byte_anchor(12, &coord));
    CHECK(coord.line_begin == 6);
    CHECK(coord.line_next == 16);
    CHECK(coord.line_offset_chars == 3);
    CHECK(doc.lineno_from_coord(&coord, &exact) == 1);

    CHECK(doc.getline(coord, buf, 16) == 5);
    CHECK(buf[0] == 'b');
    CHECK(buf[1] == 0xd83d);
    CHECK(buf[2] == 0xde00);
    CHECK(buf[3] == '\r');
    CHECK(buf[4] == '\n');

    doc.clear();
    DeleteFile(path);
}

void textdocument_utf16be_coord_reads_line()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    const unsigned char text[] =
    {
        0xfe, 0xff,
        0x00, 'a',
        0x00, '\r',
        0x00, '\n',
        0x00, 'b',
        0xd8, 0x3d,
        0xde, 0x00,
        0x00, '\r',
        0x00, '\n',
        0x00, 'c'
    };
    TextCoord coord;
    TCHAR buf[16];
    bool exact = false;

    CHECK(write_temp_file(reinterpret_cast<const char *>(text), sizeof(text), path));
    CHECK(doc.init(path));

    CHECK(doc.getformat() == NCP_UTF16BE);
    CHECK(doc.byte_length() == 18);

    // the CR on the second line, after 'b' and a surrogate pair
    CHECK(doc.coord_from_byte_anchor(12, &coord));
    CHECK(coord.line_begin == 6);
    CHECK(coord.line_next == 16);
    CHECK(coord.line_offset_chars == 3);
    CHECK(doc.lineno_from_coord(&coord, &exact) == 1);

    CHECK(doc.getline(coord, buf, 16) == 5);
    CHECK(buf[0] == 'b');
    CHECK(buf[1] == 0xd83d);
    CHECK(buf[2] == 0xde00);
    CHECK(buf[3] == '\r');
    CHECK(buf[4] == '\n');

    doc.clear();
    DeleteFile(path);
}

void textdocument_utf8_coord_reads_line()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    const unsigned char text[] =
    {
        0xef, 0xbb, 0xbf,
        'a', '\r', '\n',
        'b',
        0xf0, 0x9f, 0x98, 0x80,
        '\r', '\n',
        'c'
    };
    TextCoord coord;
    TCHAR buf[16];
    bool exact = false;

    CHECK(write_temp_file(reinterpret_cast<const char *>(text), sizeof(text), path));
    CHECK(doc.init(path));

    CHECK(doc.getformat() == NCP_UTF8);
    CHECK(doc.byte_length() == 11);

    // the CR on the second line, after 'b' and a surrogate pair
    CHECK(doc.coord_from_byte_anchor(8, &coord));
    CHECK(coord.line_begin == 3);
    CHECK(coord.line_next == 10);
    CHECK(coord.line_offset_chars == 3);
    CHECK(doc.lineno_from_coord(&coord, &exact) == 1);

    CHECK(doc.getline(coord, buf, 16) == 5);
    CHECK(buf[0] == 'b');
    CHECK(buf[1] == 0xd83d);
    CHECK(buf[2] == 0xde00);
    CHECK(buf[3] == '\r');
    CHECK(buf[4] == '\n');

    doc.clear();
    DeleteFile(path);
}

void textdocument_coord_uses_byte_anchor_for_ascii()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TextCoord coord;

    CHECK(write_textdocument_file("abc\r\nxyz", DOC_ASCII, path));
    CHECK(doc.init(path));

    CHECK(doc.coord_from_byte_anchor(6, &coord));
    CHECK(coord.byte_anchor == 6);
    CHECK(coord.line_begin == 5);
    CHECK(coord.line_next == 8);
    CHECK(coord.line_offset_chars == 1);

    doc.clear();
    DeleteFile(path);
}

void textdocument_coord_uses_local_utf8_byte_anchor()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    const unsigned char text[] =
    {
        0xef, 0xbb, 0xbf,
        'a', '\r', '\n',
        'b',
        0xf0, 0x9f, 0x98, 0x80,
        '\r', '\n',
        'c'
    };
    TextCoord coord;

    CHECK(write_temp_file(reinterpret_cast<const char *>(text), sizeof(text), path));
    CHECK(doc.init(path));

    CHECK(doc.coord_from_byte_anchor(4, &coord));
    CHECK(coord.byte_anchor == 4);
    CHECK(coord.line_begin == 3);
    CHECK(coord.line_next == 10);
    CHECK(coord.line_offset_chars == 1);

    // after the emoji: its four bytes are two UTF-16 units
    CHECK(doc.coord_from_byte_anchor(8, &coord));
    CHECK(coord.line_offset_chars == 3);

    doc.clear();
    DeleteFile(path);
}

void textdocument_coord_resolves_document_end()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    const unsigned char text[] =
    {
        0xef, 0xbb, 0xbf,
        'a', '\r', '\n',
        'b',
        0xf0, 0x9f, 0x98, 0x80,
        '\r', '\n',
        'c'
    };
    TextCoord coord;

    CHECK(write_temp_file(reinterpret_cast<const char *>(text), sizeof(text), path));
    CHECK(doc.init(path));

    CHECK(doc.coord_from_document_end(&coord));
    CHECK(coord.byte_anchor == 11);
    CHECK(coord.line_begin == 10);
    CHECK(coord.line_next == 11);
    CHECK(coord.line_offset_chars == 1);

    doc.clear();
    DeleteFile(path);
}

// Walking down the document finds the lines it counts, and stops where Ctrl+End does
void expect_doc_lines_consistent(const void *data, size_t length, ULONG expected_lines)
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TextCoord line;
    TextCoord eof;
    ULONG walked = 1;

    CHECK(write_temp_file(static_cast<const char *>(data), length, path));
    CHECK(doc.init(path));
    CHECK(doc.coord_from_byte_anchor(0, &line));

    for(;;)
    {
        TextCoord next;

        CHECK(doc.next_line_from_coord(&line, 1, &next));

        if(next.line_begin == line.line_begin)
            break;

        line = next;
        walked++;
    }

    CHECK(doc.coord_from_document_end(&eof));
    CHECK(doc.linecount() == expected_lines);
    CHECK(walked == expected_lines);
    CHECK(eof.line_begin == line.line_begin);

    doc.clear();
    DeleteFile(path);
}

void textdocument_only_cr_lf_end_lines()
{
    const char crlf[] = "abc\r\n";
    const char ff[] = "abc\x0c";
    const char vt[] = "abc\x0b";
    const char ansi_ellipsis[] = "abc\x85";
    const unsigned char utf8_nel[] = { 0xef, 0xbb, 0xbf, 'a', 'b', 'c', 0xc2, 0x85 };
    const unsigned char utf8_ls[] = { 0xef, 0xbb, 0xbf, 'a', 'b', 'c', 0xe2, 0x80, 0xa8 };
    const unsigned char utf16_nel[] = { 0xff, 0xfe, 'a', 0, 'b', 0, 'c', 0, 0x85, 0x00 };
    const unsigned char utf16_ls[] = { 0xff, 0xfe, 'a', 0, 'b', 0, 'c', 0, 0x28, 0x20 };
    const unsigned char utf16_ps[] = { 0xff, 0xfe, 'a', 0, 'b', 0, 'c', 0, 0x29, 0x20 };

    // a final CRLF is followed by one more, empty, line
    expect_doc_lines_consistent(crlf, sizeof(crlf) - 1, 2);

    // VT, FF, NEL, LS and PS are ordinary characters in every encoding
    // (and byte 0x85 in an ANSI file is an ellipsis)
    expect_doc_lines_consistent(ff, sizeof(ff) - 1, 1);
    expect_doc_lines_consistent(vt, sizeof(vt) - 1, 1);
    expect_doc_lines_consistent(ansi_ellipsis, sizeof(ansi_ellipsis) - 1, 1);
    expect_doc_lines_consistent(utf8_nel, sizeof(utf8_nel), 1);
    expect_doc_lines_consistent(utf8_ls, sizeof(utf8_ls), 1);
    expect_doc_lines_consistent(utf16_nel, sizeof(utf16_nel), 1);
    expect_doc_lines_consistent(utf16_ls, sizeof(utf16_ls), 1);
    expect_doc_lines_consistent(utf16_ps, sizeof(utf16_ps), 1);
}

void textdocument_linebreak_from_coord()
{
    const DocTestEncoding encodings[] = { DOC_ASCII, DOC_UTF8_BOM, DOC_UTF16LE_BOM, DOC_UTF16BE_BOM };

    for(size_t i = 0; i < sizeof(encodings) / sizeof(encodings[0]); i++)
    {
        TextDocument doc;
        TCHAR path[MAX_PATH];
        TextCoord line;

        CHECK(write_textdocument_file("crlf\r\nlf\ncr\rnone", encodings[i], path));
        CHECK(doc.init(path));

        CHECK(doc.coord_from_byte_anchor(0, &line));
        CHECK(doc.linebreak_from_coord(&line) == TXL_CRLF);

        CHECK(doc.next_line_from_coord(&line, 1, &line));
        CHECK(doc.linebreak_from_coord(&line) == TXL_LF);

        CHECK(doc.next_line_from_coord(&line, 1, &line));
        CHECK(doc.linebreak_from_coord(&line) == TXL_CR);

        CHECK(doc.next_line_from_coord(&line, 1, &line));
        CHECK(doc.linebreak_from_coord(&line) == 0);

        doc.clear();
        DeleteFile(path);
    }
}

void textdocument_charoffset_only_for_fixed_width()
{
    const DocTestEncoding encodings[] = { DOC_ASCII, DOC_UTF8_BOM, DOC_UTF16LE_BOM, DOC_UTF16BE_BOM };

    for(size_t i = 0; i < sizeof(encodings) / sizeof(encodings[0]); i++)
    {
        TextDocument doc;
        TCHAR path[MAX_PATH];
        TextCoord coord;
        size_w offset_chars = 0;
        bool utf16 = encodings[i] == DOC_UTF16LE_BOM || encodings[i] == DOC_UTF16BE_BOM;

        CHECK(write_textdocument_file("abc\r\nxyz", encodings[i], path));
        CHECK(doc.init(path));

        // the 'y', six characters in
        CHECK(doc.coord_from_byte_anchor(utf16 ? 12 : 6, &coord));

        if(encodings[i] == DOC_UTF8_BOM)
        {
            CHECK(!doc.charoffset_from_coord(&coord, &offset_chars));
            CHECK(!doc.charcount(&offset_chars));
        }
        else
        {
            CHECK(doc.charoffset_from_coord(&coord, &offset_chars));
            CHECK(offset_chars == 6);

            // the whole document, without the BOM
            CHECK(doc.charcount(&offset_chars));
            CHECK(offset_chars == 8);
        }

        doc.clear();
        DeleteFile(path);
    }
}

void textdocument_coord_moves_by_local_lines()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TextCoord coord;
    TextCoord line;
    TextCoord at_pos;

    CHECK(write_textdocument_file("abc\r\nxyz\nlast", DOC_ASCII, path));
    CHECK(doc.init(path));

    CHECK(doc.coord_from_byte_anchor(6, &coord));

    CHECK(doc.previous_line_from_coord(&coord, 1, &line));
    CHECK(line.byte_anchor == 0);
    CHECK(line.line_begin == 0);
    CHECK(line.line_next == 5);
    CHECK(line.line_offset_chars == 0);

    CHECK(doc.next_line_from_coord(&coord, 1, &line));
    CHECK(line.byte_anchor == 9);
    CHECK(line.line_begin == 9);
    CHECK(line.line_next == 13);
    CHECK(line.line_offset_chars == 0);

    CHECK(doc.coord_from_line_pos(&line, 2, &at_pos));
    CHECK(at_pos.byte_anchor == 11);
    CHECK(at_pos.line_offset_chars == 2);

    doc.clear();
    DeleteFile(path);
}

void textdocument_coord_moves_by_local_utf8_lines()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    const unsigned char text[] =
    {
        0xef, 0xbb, 0xbf,
        'a', '\r', '\n',
        'b',
        0xf0, 0x9f, 0x98, 0x80,
        '\r', '\n',
        'c'
    };
    TextCoord coord;
    TextCoord line;
    TextCoord at_pos;

    CHECK(write_temp_file(reinterpret_cast<const char *>(text), sizeof(text), path));
    CHECK(doc.init(path));

    CHECK(doc.coord_from_byte_anchor(4, &coord));

    CHECK(doc.previous_line_from_coord(&coord, 1, &line));
    CHECK(line.byte_anchor == 0);
    CHECK(line.line_begin == 0);
    CHECK(line.line_next == 3);
    CHECK(line.line_offset_chars == 0);

    CHECK(doc.next_line_from_coord(&coord, 1, &line));
    CHECK(line.byte_anchor == 10);
    CHECK(line.line_begin == 10);
    CHECK(line.line_next == 11);
    CHECK(line.line_offset_chars == 0);

    CHECK(doc.coord_from_byte_anchor(3, &line));
    CHECK(doc.coord_from_line_pos(&line, 3, &at_pos));
    CHECK(at_pos.byte_anchor == 8);
    CHECK(at_pos.line_offset_chars == 3);

    doc.clear();
    DeleteFile(path);
}

void textdocument_coord_tracks_lazy_eof_after_newline_edits()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    const char line[] = "abcdefghijklmnopqrstuvwxyz 0123456789\r\n";
    const size_t line_length = strlen(line);
    const size_w line_count = (MEM_BLOCK_SIZE * 4) / line_length;
    TextCoord before;
    TextCoord after;
    TextCoord top;
    TCHAR newlines[] = TEXT("\r\n\r\n\r\n");
    bool exact;

    CHECK(write_large_pattern_file(line, line_length, line_count, path));
    CHECK(doc.init(path));
    CHECK(!doc.linecount_known());

    CHECK(doc.coord_from_document_end(&before));
    CHECK(doc.insert_text(&before, newlines, 6, 0) == 6);
    CHECK(doc.coord_from_document_end(&after));

    CHECK(after.byte_anchor == before.byte_anchor + 6);
    CHECK(after.line_begin == before.line_begin + 6);
    CHECK(doc.previous_line_from_coord(&after, 3, &top));
    CHECK(top.line_begin == before.line_begin);
    CHECK(doc.lineno_from_coord(&after, &exact) >= doc.lineno_from_coord(&top, &exact));

    doc.clear();
    DeleteFile(path);
}

void textdocument_coord_moves_across_changes()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TCHAR xyz[] = TEXT("XYZ");
    TextChange change;
    TextCoord top;
    TextCoord second;
    TextCoord next;

    CHECK(write_textdocument_file("hello\r\nworld\r\nthird\r\n", DOC_ASCII, path));
    CHECK(doc.init(path));

    CHECK(doc.coord_from_byte_anchor(0, &top));
    CHECK(doc.coord_from_byte_anchor(9, &second));

    // Typing on the top line: its start stays put but its end moves.
    CHECK(doc_insert(doc, 2, xyz, 3, &change) == 3);
    CHECK(change.offset == 2 && change.erased == 0 && change.inserted == 3);
    CHECK(doc.coord_after_change(&top, &change));
    CHECK(top.byte_anchor == 0);
    CHECK(top.line_next == 10);
    CHECK(doc.next_line_from_coord(&top, 1, &next));
    CHECK(next.line_begin == 10);
    CHECK(next.line_next == 17);

    CHECK(doc.coord_after_change(&second, &change));
    CHECK(second.byte_anchor == 12);
    CHECK(second.line_begin == 10);

    // A coordinate exactly at an insertion stays before the inserted text.
    CHECK(doc_insert(doc, 10, xyz, 3, &change) == 3);
    CHECK(doc.coord_after_change(&next, &change));
    CHECK(next.byte_anchor == 10);
    CHECK(next.line_next == 20);

    // A coordinate inside erased text moves to the start of the erase.
    CHECK(doc.coord_from_byte_anchor(12, &second));
    CHECK(doc_erase(doc, 11, 15, &change) == 4);
    CHECK(change.offset == 11 && change.erased == 4 && change.inserted == 0);
    CHECK(doc.coord_after_change(&second, &change));
    CHECK(second.byte_anchor == 11);
    CHECK(second.line_begin == 10);

    CHECK(doc.undo(&change));
    CHECK(change.offset == 11 && change.erased == 0 && change.inserted == 4);

    doc.clear();
    DeleteFile(path);
}

std::wstring read_range(TextDocument &doc, ULONG from_byte, ULONG to_byte)
{
    TextCoord from;
    TextCoord to;
    TCHAR buf[64];
    ULONG len;

    CHECK(doc.coord_from_byte_anchor(from_byte, &from));
    CHECK(doc.coord_from_byte_anchor(to_byte, &to));

    TextReader reader = doc.text_from_range(&from, &to);
    len = reader.read(buf, 64);

    return std::wstring(buf, len);
}

void textdocument_edits_by_coord()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TCHAR xyz[] = TEXT("XYZ");
    TextChange change;
    TextCoord from;
    TextCoord to;

    CHECK(write_textdocument_file("hello\r\nworld\r\n", DOC_ASCII, path));
    CHECK(doc.init(path));

    CHECK(doc.coord_from_byte_anchor(1, &from));
    CHECK(doc.insert_text(&from, xyz, 3, &change) == 3);
    CHECK(change.offset == 1 && change.erased == 0 && change.inserted == 3);
    CHECK(read_range(doc, 0, 17) == L"hXYZello\r\nworld\r\n");

    // A range may be given in either order.
    CHECK(doc.coord_from_byte_anchor(8, &from));
    CHECK(doc.coord_from_byte_anchor(4, &to));
    CHECK(doc.erase_text(&from, &to, &change) == 4);
    CHECK(change.offset == 4 && change.erased == 4 && change.inserted == 0);
    CHECK(read_range(doc, 0, 13) == L"hXYZ\r\nworld\r\n");

    CHECK(doc.coord_from_byte_anchor(6, &from));
    CHECK(doc.coord_from_byte_anchor(11, &to));
    CHECK(doc.replace_text(&from, &to, xyz, 3, &change) == 3);
    CHECK(change.offset == 6 && change.erased == 5 && change.inserted == 3);
    CHECK(read_range(doc, 0, 11) == L"hXYZ\r\nXYZ\r\n");
    CHECK(read_range(doc, 6, 9) == L"XYZ");

    // Undoing the replace brings back the erased word as one change.
    CHECK(doc.undo(&change));
    CHECK(change.offset == 6 && change.erased == 3 && change.inserted == 5);
    CHECK(read_range(doc, 0, 13) == L"hXYZ\r\nworld\r\n");

    CHECK(doc.redo(&change));
    CHECK(change.offset == 6 && change.erased == 5 && change.inserted == 3);

    doc.clear();
    DeleteFile(path);
}

void textdocument_change_excludes_header()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TCHAR x[] = TEXT("X");
    TextChange change;

    CHECK(write_textdocument_file("ab\r\ncd", DOC_UTF16LE_BOM, path));
    CHECK(doc.init(path));

    CHECK(doc_insert(doc, 2, x, 1, &change) == 2);
    CHECK(change.offset == 2 && change.erased == 0 && change.inserted == 2);

    CHECK(doc.undo(&change));
    CHECK(change.offset == 2 && change.erased == 2 && change.inserted == 0);

    doc.clear();
    DeleteFile(path);
}

void textdocument_sequence_index_matches_ascii_after_edits()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TCHAR crlf[] = TEXT("\r\n");

    CHECK(write_textdocument_file("abc\r\nxyz", DOC_ASCII, path));
    CHECK(doc.init(path));

    CHECK(doc_insert(doc, 1, crlf, 2) == 2);

    expect_sequence_line_matches_textdocument(doc, 0);
    expect_sequence_line_matches_textdocument(doc, 1);
    expect_sequence_line_matches_textdocument(doc, 2);
    expect_sequence_offset_matches_textdocument(doc, 0);
    expect_sequence_offset_matches_textdocument(doc, 3);
    expect_sequence_offset_matches_textdocument(doc, 6);

    doc.clear();
    DeleteFile(path);
}

void textdocument_sequence_index_matches_utf16le()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    const WCHAR text[] = { 0xfeff, 'a', '\r', '\n', 'b', 0xd83d, 0xde00, '\r', '\n', 'c' };

    CHECK(write_temp_file(reinterpret_cast<const char *>(text), sizeof(text), path));
    CHECK(doc.init(path));
    CHECK(doc.getformat() == NCP_UTF16);

    expect_sequence_line_matches_textdocument(doc, 0);
    expect_sequence_line_matches_textdocument(doc, 1);
    expect_sequence_line_matches_textdocument(doc, 2);
    expect_sequence_offset_matches_textdocument(doc, 0);
    expect_sequence_offset_matches_textdocument(doc, 10);
    expect_sequence_offset_matches_textdocument(doc, 16);

    doc.clear();
    DeleteFile(path);
}

void textdocument_sequence_index_matches_utf16be()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    const unsigned char text[] =
    {
        0xfe, 0xff,
        0x00, 'a',
        0x00, '\r',
        0x00, '\n',
        0x00, 'b',
        0xd8, 0x3d,
        0xde, 0x00,
        0x00, '\r',
        0x00, '\n',
        0x00, 'c'
    };

    CHECK(write_temp_file(reinterpret_cast<const char *>(text), sizeof(text), path));
    CHECK(doc.init(path));
    CHECK(doc.getformat() == NCP_UTF16BE);

    expect_sequence_line_matches_textdocument(doc, 0);
    expect_sequence_line_matches_textdocument(doc, 1);
    expect_sequence_line_matches_textdocument(doc, 2);
    expect_sequence_offset_matches_textdocument(doc, 0);
    expect_sequence_offset_matches_textdocument(doc, 10);
    expect_sequence_offset_matches_textdocument(doc, 16);

    doc.clear();
    DeleteFile(path);
}

void textdocument_sequence_index_matches_utf8_bom()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    const unsigned char text[] =
    {
        0xef, 0xbb, 0xbf,
        'a', '\r', '\n',
        'b',
        0xf0, 0x9f, 0x98, 0x80,
        '\r', '\n',
        'c'
    };

    CHECK(write_temp_file(reinterpret_cast<const char *>(text), sizeof(text), path));
    CHECK(doc.init(path));
    CHECK(doc.getformat() == NCP_UTF8);

    expect_sequence_line_matches_textdocument(doc, 0);
    expect_sequence_line_matches_textdocument(doc, 1);
    expect_sequence_line_matches_textdocument(doc, 2);
    expect_sequence_offset_matches_textdocument(doc, 0);
    expect_sequence_offset_matches_textdocument(doc, 8);
    expect_sequence_offset_matches_textdocument(doc, 10);

    doc.clear();
    DeleteFile(path);
}

void textdocument_utf8_lf_lazy_lookup_returns_bounded_line()
{
    TextDocument doc;
    sequence &seq = TextDocumentLineIndexProbe::seq(doc);
    TCHAR path[MAX_PATH];
    const unsigned char bom[] = { 0xef, 0xbb, 0xbf };
    const char *line = "line UTF-8 LF cafe\xcc\x81 omega \xce\xa9 emoji \xf0\x9f\x98\x80 0123456789\n";
    const size_t line_length = strlen(line);
    const size_t line_count = (MEM_BLOCK_SIZE * 4) / line_length;
    const size_t file_length = sizeof(bom) + line_length * line_count;
    const ULONG target_line = static_cast<ULONG>(line_count / 2);
    char *data = new char[file_length];
    TCHAR buf[128];
    size_w line_offset = 0;
    size_w line_begin;
    ULONG chars;
    TextCoord coord;
    TextCoord prev;
    TextCoord next;
    bool exact = false;

    memcpy(data, bom, sizeof(bom));

    for(size_t i = 0; i < line_count; i++)
        memcpy(data + sizeof(bom) + i * line_length, line, line_length);

    CHECK(write_temp_file(data, file_length, path));
    CHECK(doc.init(path));
    CHECK(doc.getformat() == NCP_UTF8);
    CHECK(!seq.lineno_known(target_line));

    CHECK(seq.offset_from_lineno(target_line, &line_offset));
    line_begin = doc_offset(doc, line_offset);
    CHECK(line_begin == target_line * line_length);

    CHECK(doc.coord_from_byte_anchor(line_begin + 20, &coord));
    CHECK(coord.line_begin == line_begin);
    CHECK(doc.lineno_from_coord(&coord, &exact) == target_line);

    CHECK(doc.coord_from_byte_anchor(line_begin + 21, &coord));
    CHECK(coord.line_begin == line_begin);
    CHECK(doc.lineno_from_coord(&coord, &exact) == target_line);

    CHECK(doc.previous_line_from_coord(&coord, 1, &prev));
    CHECK(doc.lineno_from_coord(&prev, &exact) == target_line - 1);

    CHECK(doc.next_line_from_coord(&coord, 1, &next));
    CHECK(doc.lineno_from_coord(&next, &exact) == target_line + 1);

    chars = doc.getline(coord, buf, 128);
    CHECK(chars > 0);
    CHECK(chars < 128);
    CHECK(buf[chars - 1] == '\n');

    doc.clear();
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
    { "undo_redo_reports_changed_range", undo_redo_reports_changed_range },
    { "redo_invalidated_by_new_edit", redo_invalidated_by_new_edit },
    { "invalid_operations_do_not_modify_content", invalid_operations_do_not_modify_content },
    { "render_past_end_returns_available_bytes", render_past_end_returns_available_bytes },
    { "linecount_handles_basic_newlines", linecount_handles_basic_newlines },
    { "linecount_handles_crlf", linecount_handles_crlf },
    { "linecount_handles_crlf_split_across_spans", linecount_handles_crlf_split_across_spans },
    { "linecount_updates_after_insert_delete_replace", linecount_updates_after_insert_delete_replace },
    { "linecount_restores_on_undo_redo", linecount_restores_on_undo_redo },
    { "lineno_from_offset_handles_basic_newlines", lineno_from_offset_handles_basic_newlines },
    { "lineno_from_offset_handles_crlf", lineno_from_offset_handles_crlf },
    { "lineno_from_offset_handles_crlf_split_across_spans", lineno_from_offset_handles_crlf_split_across_spans },
    { "linecount_handles_crlf_split_by_insert", linecount_handles_crlf_split_by_insert },
    { "linecount_handles_cr_lf_adjacent_in_modify_buffer", linecount_handles_cr_lf_adjacent_in_modify_buffer },
	{ "line_bounds_from_offset_handles_crlf_boundaries", line_bounds_from_offset_handles_crlf_boundaries },
	{ "line_bounds_from_offset_handles_long_lazy_file_line", line_bounds_from_offset_handles_long_lazy_file_line },
    { "line_scan_mode_handles_utf16le_crlf", line_scan_mode_handles_utf16le_crlf },
    { "line_scan_mode_handles_utf16be_crlf", line_scan_mode_handles_utf16be_crlf },
    { "line_scan_mode_handles_utf32le_crlf", line_scan_mode_handles_utf32le_crlf },
    { "line_scan_mode_handles_utf32be_crlf", line_scan_mode_handles_utf32be_crlf },
    { "line_scan_mode_handles_utf16le_crlf_split_across_spans", line_scan_mode_handles_utf16le_crlf_split_across_spans },
    { "textdocument_linecount_handles_basic_newlines", textdocument_linecount_handles_basic_newlines },
    { "textdocument_linecount_handles_crlf", textdocument_linecount_handles_crlf },
    { "textdocument_linecount_handles_crlf_split_across_lazy_page", textdocument_linecount_handles_crlf_split_across_lazy_page },
    { "textdocument_linecount_updates_after_insert_delete_replace", textdocument_linecount_updates_after_insert_delete_replace },
    { "textdocument_linecount_restores_on_undo_redo", textdocument_linecount_restores_on_undo_redo },
    { "open_file_renders_file_backed_content", open_file_renders_file_backed_content },
    { "open_file_renders_across_view_boundary", open_file_renders_across_view_boundary },
    { "open_file_handles_crlf_across_scan_boundary", open_file_handles_crlf_across_scan_boundary },
    { "lazy_file_insert_crlf_updates_visible_line_offsets", lazy_file_insert_crlf_updates_visible_line_offsets },
    { "lazy_file_fully_indexed_keeps_exact_lines", lazy_file_fully_indexed_keeps_exact_lines },
    { "lazy_line_numbers_follow_edits_above", lazy_line_numbers_follow_edits_above },
    { "textdocument_coord_handles_lazy_end_after_insert", textdocument_coord_handles_lazy_end_after_insert },
    { "textdocument_final_sparse_line_ignores_estimated_linecount", textdocument_final_sparse_line_ignores_estimated_linecount },
    { "textdocument_ctrl_end_uses_final_page_when_lines_are_lazy", textdocument_ctrl_end_uses_final_page_when_lines_are_lazy },
    { "textdocument_lazy_eof_line_number_maps_back_to_same_line", textdocument_lazy_eof_line_number_maps_back_to_same_line },
    { "textdocument_lazy_eof_without_final_crlf_maps_to_final_line", textdocument_lazy_eof_without_final_crlf_maps_to_final_line },
	{ "textdocument_lazy_offset_lookup_uses_sequence_line_bounds", textdocument_lazy_offset_lookup_uses_sequence_line_bounds },
    { "textdocument_lazy_line_numbers_continue_after_first_page", textdocument_lazy_line_numbers_continue_after_first_page },
    { "textdocument_large_lazy_file_line_estimate_does_not_overflow", textdocument_large_lazy_file_line_estimate_does_not_overflow },
    { "open_file_larger_than_4gb", open_file_larger_than_4gb },
    { "textdocument_edits_beyond_4gb", textdocument_edits_beyond_4gb },
    { "textdocument_utf16_coord_reads_line", textdocument_utf16_coord_reads_line },
    { "textdocument_utf16be_coord_reads_line", textdocument_utf16be_coord_reads_line },
    { "textdocument_utf8_coord_reads_line", textdocument_utf8_coord_reads_line },
    { "textdocument_coord_uses_byte_anchor_for_ascii", textdocument_coord_uses_byte_anchor_for_ascii },
    { "textdocument_coord_uses_local_utf8_byte_anchor", textdocument_coord_uses_local_utf8_byte_anchor },
    { "textdocument_coord_resolves_document_end", textdocument_coord_resolves_document_end },
    { "textdocument_only_cr_lf_end_lines", textdocument_only_cr_lf_end_lines },
    { "textdocument_linebreak_from_coord", textdocument_linebreak_from_coord },
    { "textdocument_charoffset_only_for_fixed_width", textdocument_charoffset_only_for_fixed_width },
    { "textdocument_coord_moves_by_local_lines", textdocument_coord_moves_by_local_lines },
    { "textdocument_coord_moves_by_local_utf8_lines", textdocument_coord_moves_by_local_utf8_lines },
    { "textdocument_coord_tracks_lazy_eof_after_newline_edits", textdocument_coord_tracks_lazy_eof_after_newline_edits },
    { "textdocument_coord_moves_across_changes", textdocument_coord_moves_across_changes },
    { "textdocument_edits_by_coord", textdocument_edits_by_coord },
    { "textdocument_change_excludes_header", textdocument_change_excludes_header },
    { "textdocument_sequence_index_matches_ascii_after_edits", textdocument_sequence_index_matches_ascii_after_edits },
    { "textdocument_sequence_index_matches_utf16le", textdocument_sequence_index_matches_utf16le },
    { "textdocument_sequence_index_matches_utf16be", textdocument_sequence_index_matches_utf16be },
    { "textdocument_sequence_index_matches_utf8_bom", textdocument_sequence_index_matches_utf8_bom },
    { "textdocument_utf8_lf_lazy_lookup_returns_bounded_line", textdocument_utf8_lf_lazy_lookup_returns_bounded_line },
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
