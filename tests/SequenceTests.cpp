#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include "../src/TextView/sequence.h"
#include "../src/TextView/SaveFile.h"
#include "../src/TextView/TextDocument.h"
#include "../src/TextView/TextView.h"
#include "../src/TextView/Unicode.h"

// Test access to a document's insides: its sequence, for line-number lookups
// (such as where line N starts) that TextDocument itself doesn't offer, and
// functions only TextDocument uses
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

    static bool linecount_known(TextDocument &doc)
    {
        return doc.linecount_known();
    }

    static TextPos pos_after_change(TextDocument &doc, TextPos pos, const TextChange *change)
    {
        return doc.pos_after_change(pos, change);
    }

    // How much of a file opening it indexes: a file must be bigger to stay lazy
    static size_w open_index_length()
    {
        return TextDocument::OPEN_INDEX_LENGTH;
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

// A position's line number, and whether it's exact rather than pending
size_w doc_lineno(TextDocument &doc, TextPos pos, bool *exact)
{
    size_w lineno = 0;

    *exact = doc.lineno_from_pos(pos, &lineno) == TDR_OK;
    return lineno;
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
    size_w first_length = min(file_length, TextDocumentLineIndexProbe::open_index_length());	// all that opening indexes
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

    ok = true;

    for(size_w done = 0; ok && done < first_length; done += MEM_BLOCK_SIZE)
    {
        size_w chunk = min(first_length - done, MEM_BLOCK_SIZE);

        for(size_w i = 0; i < chunk; i++)
            block[i] = line[(done + i) % line_length];

        ok = WriteFile(hFile, block, static_cast<DWORD>(chunk), &written, 0) && written == chunk;
    }

    // the end of the file too, as much of it as the start didn't cover
    if(tail_offset < first_length)
    {
        tail_offset = first_length;
        tail_length = file_length - first_length;
    }

    if(ok && tail_length > 0)
    {
        pos.QuadPart = tail_offset;
        ok = SetFilePointerEx(hFile, pos, 0, FILE_BEGIN) != 0;

        for(size_w i = 0; ok && i < tail_length; i++)
            block[i] = line[(tail_offset + i) % line_length];

        ok = ok && WriteFile(hFile, block, static_cast<DWORD>(tail_length), &written, 0) && written == tail_length;
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

// Write code units of unit_size bytes in either byte order. Start the units
// with 0xFEFF to write the matching byte-order mark.
bool write_units_file(const ULONG *units, size_t count, size_t unit_size, bool big_endian, TCHAR path[MAX_PATH])
{
    std::string bytes;

    for(size_t i = 0; i < count; i++)
    {
        for(size_t b = 0; b < unit_size; b++)
        {
            size_t shift = 8 * (big_endian ? unit_size - 1 - b : b);
            bytes.push_back(static_cast<char>((units[i] >> shift) & 0xff));
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

// The kinds of line break the sequence reports, and whether it has counted all of it
void expect_linebreak_kinds(const sequence &seq, bool crlf, bool cr, bool lf, bool complete = true)
{
    sequence::break_kinds kinds = seq.linebreak_kinds();

    CHECK(kinds.crlf == crlf);
    CHECK(kinds.cr == cr);
    CHECK(kinds.lf == lf);
    CHECK(seq.linecount_known() == complete);
}

void expect_linebreak_kinds(const char *text, bool crlf, bool cr, bool lf)
{
    sequence seq;

    init(seq, text);
    expect_linebreak_kinds(seq, crlf, cr, lf);
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

    CHECK(doc.coord_from_pos(TextPos(0), &top));
    CHECK(doc.next_line_from_coord(&top, line, &coord));
    return coord;
}

void expect_doc_line_begin(TextDocument &doc, ULONG line, ULONG expected)
{
    TextCoord coord = doc_line(doc, line);
    bool exact = false;

    CHECK(coord.line_begin.byte_offset == expected);
    CHECK(doc_lineno(doc, coord, &exact) == line);
}

// Walking down past the last line stays on it
void expect_doc_no_line(TextDocument &doc, ULONG line)
{
    CHECK(doc_line(doc, line).line_begin.byte_offset == doc_line(doc, line - 1).line_begin.byte_offset);
}

void expect_doc_line_at(TextDocument &doc, ULONG offset, ULONG expected_line, ULONG expected_line_begin)
{
    TextCoord coord;
    bool exact = false;

    CHECK(doc.coord_from_pos(TextPos(offset), &coord));
    CHECK(coord.line_begin.byte_offset == expected_line_begin);
    CHECK(doc_lineno(doc, coord, &exact) == expected_line);
}

// Edits at document byte offsets
size_w doc_insert(TextDocument &doc, size_w offset, const TCHAR *text, ULONG length, TextChange *change = 0)
{
    return doc.insert_text(TextPos(offset), text, length, change);
}

size_w doc_erase(TextDocument &doc, size_w from, size_w to, TextChange *change = 0)
{
    return doc.erase_text(TextPos(from), TextPos(to), change);
}

size_w doc_replace(TextDocument &doc, size_w from, size_w to, const TCHAR *text, ULONG length, TextChange *change = 0)
{
    return doc.replace_text(TextPos(from), TextPos(to), text, length, change);
}

// find_text between document byte offsets, reporting the match as an offset and a length
bool doc_find(TextDocument &doc, const TCHAR *text, ULONG length, size_w from, size_w to, bool backwards, bool matchcase, size_w *match, size_w *match_length)
{
    TextPos begin;
    TextPos end;

    if(!doc.find_text(text, length, TextPos(from), TextPos(to), backwards, matchcase, &begin, &end))
        return false;

    *match = begin.byte_offset;
    *match_length = end - begin;
    return true;
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

    CHECK(coord.line_begin.byte_offset == doc_offset(doc, line_offset));
    CHECK(coord.line_next.byte_offset == doc_offset(doc, next_offset));
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

    CHECK(doc.coord_from_pos(TextPos(offset), &coord));
    CHECK(seq.lineno_from_offset(offset + header, &line, &line_offset));
    CHECK(doc_lineno(doc, coord, &exact) == line);
    CHECK(coord.line_begin.byte_offset == doc_offset(doc, line_offset));
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

void linebreak_kinds_finds_each_kind()
{
    //                     text            crlf   cr     lf
    expect_linebreak_kinds("abc",          false, false, false);
    expect_linebreak_kinds("a\r\nb\r\n",   true,  false, false);
    expect_linebreak_kinds("a\nb\n",       false, false, true);
    expect_linebreak_kinds("a\rb\r",       false, true,  false);
    expect_linebreak_kinds("a\r\nb\nc\rd", true,  true,  true);

    // a CR followed by another CR is a lone CR
    expect_linebreak_kinds("a\r\r\nb",     true,  true,  false);

    // nothing is before the first unit or after the last, so an LF at the very
    // start and a CR at the very end are lone breaks
    expect_linebreak_kinds("\nab",         false, false, true);
    expect_linebreak_kinds("ab\r",         false, true,  false);
    expect_linebreak_kinds("\r\n",         true,  false, false);
}

void linebreak_kinds_joins_crlf_across_spans_and_pages()
{
    // a CR ending one span and an LF starting the next make one CRLF
    {
        sequence seq;
        init(seq, "abc\rxyz");
        expect_linebreak_kinds(seq, false, true, false);

        CHECK(insert_bytes(seq, 4, "\n"));
        expect_content(seq, "abc\r\nxyz");
        expect_linebreak_kinds(seq, true, false, false);
    }

    // splitting a CRLF leaves a lone CR and a lone LF; joining it again undoes that
    {
        sequence seq;
        init(seq, "a\r\nb");

        CHECK(insert_bytes(seq, 2, "X"));
        expect_content(seq, "a\rX\nb");
        expect_linebreak_kinds(seq, false, true, true);

        CHECK(seq.erase(2, 1));
        expect_content(seq, "a\r\nb");
        expect_linebreak_kinds(seq, true, false, false);
    }

    // a CRLF straddling a line page: the page before ends with CR, the page after starts with LF
    {
        sequence seq;
        std::string text(LINE_PAGE_SIZE * 2, 'A');

        text[LINE_PAGE_SIZE - 1] = '\r';
        text[LINE_PAGE_SIZE] = '\n';

        CHECK(seq.init(reinterpret_cast<const seqchar *>(text.data()), text.size()));
        expect_linebreak_kinds(seq, true, false, false);
    }
}

void linebreak_kinds_follow_edits()
{
    sequence seq;
    init(seq, "a\r\nb\nc\r\n");
    expect_linebreak_kinds(seq, true, false, true);

    // turning the only lone LF into a CRLF leaves just CRLFs
    CHECK(insert_bytes(seq, 4, "\r"));
    expect_content(seq, "a\r\nb\r\nc\r\n");
    expect_linebreak_kinds(seq, true, false, false);

    CHECK(seq.undo());
    expect_linebreak_kinds(seq, true, false, true);

    CHECK(seq.redo());
    expect_linebreak_kinds(seq, true, false, false);
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
        CHECK(doc.insert_text(end, insert_text, 4, 0) != 0);
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

void lazy_file_linebreak_kinds_grow_with_counting()
{
    sequence seq;
    TCHAR path[MAX_PATH];
    const size_t file_length = MEM_BLOCK_SIZE * 2;
    char *data = new char[file_length];

    // CRLF lines, except the last, which ends with a lone LF past the block counted on open
    for(size_t i = 0; i < file_length; i += 4)
        memcpy(data + i, "ab\r\n", 4);

    data[file_length - 2] = 'x';

    CHECK(write_temp_file(data, file_length, path));
    CHECK(seq.open(path, true));

    // only the first block is counted, so the lone LF hasn't been seen yet
    expect_linebreak_kinds(seq, true, false, false, false);

    seq.index_lines(0, seq.size());
    expect_linebreak_kinds(seq, true, false, true, true);

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
    CHECK(eof.line_begin.byte_offset == doc.byte_length());
    CHECK(eof.line_next.byte_offset == doc.byte_length());

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
    CHECK(doc.coord_from_pos(TextPos(final_line_offset), &coord));
    CHECK(coord.line_begin.byte_offset == final_line_offset);
    CHECK(coord.line_next.byte_offset == doc.byte_length());

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
    const size_w open_index = TextDocumentLineIndexProbe::open_index_length();
    const size_t line_count = static_cast<size_t>((open_index + MEM_BLOCK_SIZE * 8) / line_length);
    const size_t file_length = line_length * line_count;
    char *data = new char[file_length];
    ULONG target_line = static_cast<ULONG>(open_index / line_length + 20);
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
    CHECK(eof.line_begin.byte_offset == doc.byte_length());
    CHECK(eof.line_next.byte_offset == doc.byte_length());
    CHECK(!TextDocumentLineIndexProbe::linecount_known(doc));
    doc_lineno(doc, eof, &exact);
    CHECK(!exact);

    CHECK(doc.previous_line_from_coord(&eof, 1, &prev));
    CHECK(prev.line_begin.byte_offset == doc.byte_length() - line_length);
    CHECK(prev.line_next.byte_offset == doc.byte_length());
    CHECK(doc.next_line_from_coord(&prev, 1, &next));
    CHECK(next.line_begin.byte_offset == doc.byte_length());

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
    eof_line = doc_lineno(doc, eof, &exact);
    CHECK(seq.offset_from_lineno(eof_line, &line_offset));
    CHECK(doc_offset(doc, line_offset) == eof.line_begin.byte_offset);
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
    last_begin = eof.line_begin.byte_offset;
    CHECK(last_begin == file_length - last_line_length);
    CHECK(eof.line_next.byte_offset - eof.line_begin.byte_offset == last_line_length);

    // the CR and LF before the last line belong to the line above it
    CHECK(doc.coord_from_pos(TextPos(last_begin - 1), &coord));
    CHECK(coord.line_begin.byte_offset == last_begin - line_length);
    CHECK(doc.coord_from_pos(TextPos(last_begin - 2), &coord));
    CHECK(coord.line_begin.byte_offset == last_begin - line_length);

    CHECK(doc.previous_line_from_coord(&eof, 1, &prev));
    CHECK(prev.line_begin.byte_offset == last_begin - line_length);
    CHECK(prev.line_next.byte_offset == last_begin);
    CHECK(doc.next_line_from_coord(&prev, 1, &next));
    CHECK(next.line_begin.byte_offset == last_begin);
    CHECK(next.line_next.byte_offset == doc.byte_length());

    doc.clear();
    DeleteFile(path);
    delete[] data;
}

void textdocument_lazy_offset_lookup_uses_sequence_line_bounds()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    const size_w open_index = TextDocumentLineIndexProbe::open_index_length();
    const size_t file_length = static_cast<size_t>(open_index + MEM_BLOCK_SIZE * 3);
    const ULONG first_break = static_cast<ULONG>(open_index + MEM_BLOCK_SIZE + 17);
    const ULONG second_break = static_cast<ULONG>(open_index + MEM_BLOCK_SIZE * 2 + 91);
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
    CHECK(!TextDocumentLineIndexProbe::linecount_known(doc));

    CHECK(doc.coord_from_pos(TextPos(target_offset), &coord));
    CHECK(coord.line_begin.byte_offset == first_break + 1);
    CHECK(coord.line_next.byte_offset == second_break + 1);

    CHECK(doc.previous_line_from_coord(&coord, 1, &prev));
    CHECK(prev.line_begin.byte_offset == 0);
    CHECK(prev.line_next.byte_offset == first_break + 1);
    CHECK(doc_lineno(doc, prev, &exact) == 0);
    CHECK(exact);

    CHECK(doc.next_line_from_coord(&coord, 1, &next));
    CHECK(next.line_begin.byte_offset == second_break + 1);
    CHECK(next.line_next.byte_offset == file_length);

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

    // about 6 MB: more than opening indexes
    CHECK(write_numbered_lines_file(120000, path));
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

        CHECK(doc.coord_from_pos(TextPos(doc_offset(doc, line_offset)), &coord));
        CHECK(doc_lineno(doc, coord, &exact) == line);
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

    CHECK(doc.coord_from_pos(TextPos(0), &coord));
    CHECK(doc_lineno(doc, coord, &exact) == 0);
    CHECK(exact);
    CHECK(doc.coord_from_pos(TextPos(static_cast<ULONG>(line_length * 10)), &coord));
    CHECK(doc_lineno(doc, coord, &exact) == 10);
    CHECK(exact);

    CHECK(doc.coord_from_document_end(&coord));
    CHECK(doc_lineno(doc, coord, &exact) > 4000000);
    CHECK(!exact);
    CHECK(coord.line_begin.byte_offset == doc.byte_length());
    CHECK(coord.line_next.byte_offset == doc.byte_length());

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
    CHECK(eof.byte_offset == size);
    CHECK(eof.line_begin.byte_offset == last_line);
    CHECK(eof.column == 9);
    doc_lineno(doc, eof, &exact);
    CHECK(!exact);

    CHECK(doc.previous_line_from_coord(&eof, 1, &prev));
    CHECK(prev.line_next.byte_offset == last_line);
    CHECK(doc.getline(prev, buf, 32) == 16);
    CHECK(memcmp(buf, TEXT("second to last\r\n"), 16 * sizeof(TCHAR)) == 0);

    // an edit beyond 4GB, and a coordinate moved across it
    CHECK(doc.insert_text(eof, bang, 1, &change) == 1);
    CHECK(change.at.byte_offset == size && change.erased == 0 && change.inserted == 1);
    CHECK(doc.byte_length() == size + 1);
    CHECK(doc.coord_after_change(&eof, &change));
    CHECK(eof.byte_offset == size);
    CHECK(eof.line_next.byte_offset == size + 1);

    CHECK(doc.undo(&change));
    CHECK(change.at.byte_offset == size && change.erased == 1 && change.inserted == 0);
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
    CHECK(doc.coord_from_pos(TextPos(12), &coord));
    CHECK(coord.line_begin.byte_offset == 6);
    CHECK(coord.line_next.byte_offset == 16);
    CHECK(coord.column == 3);
    CHECK(doc_lineno(doc, coord, &exact) == 1);

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
    CHECK(doc.coord_from_pos(TextPos(12), &coord));
    CHECK(coord.line_begin.byte_offset == 6);
    CHECK(coord.line_next.byte_offset == 16);
    CHECK(coord.column == 3);
    CHECK(doc_lineno(doc, coord, &exact) == 1);

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
    CHECK(doc.coord_from_pos(TextPos(8), &coord));
    CHECK(coord.line_begin.byte_offset == 3);
    CHECK(coord.line_next.byte_offset == 10);
    CHECK(coord.column == 3);
    CHECK(doc_lineno(doc, coord, &exact) == 1);

    CHECK(doc.getline(coord, buf, 16) == 5);
    CHECK(buf[0] == 'b');
    CHECK(buf[1] == 0xd83d);
    CHECK(buf[2] == 0xde00);
    CHECK(buf[3] == '\r');
    CHECK(buf[4] == '\n');

    doc.clear();
    DeleteFile(path);
}

void textdocument_coord_uses_byte_offset_for_ascii()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TextCoord coord;

    CHECK(write_textdocument_file("abc\r\nxyz", DOC_ASCII, path));
    CHECK(doc.init(path));

    CHECK(doc.coord_from_pos(TextPos(6), &coord));
    CHECK(coord.byte_offset == 6);
    CHECK(coord.line_begin.byte_offset == 5);
    CHECK(coord.line_next.byte_offset == 8);
    CHECK(coord.column == 1);

    doc.clear();
    DeleteFile(path);
}

void textdocument_coord_uses_local_utf8_byte_offset()
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

    CHECK(doc.coord_from_pos(TextPos(4), &coord));
    CHECK(coord.byte_offset == 4);
    CHECK(coord.line_begin.byte_offset == 3);
    CHECK(coord.line_next.byte_offset == 10);
    CHECK(coord.column == 1);

    // after the emoji: its four bytes are two UTF-16 units
    CHECK(doc.coord_from_pos(TextPos(8), &coord));
    CHECK(coord.column == 3);

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
    CHECK(coord.byte_offset == 11);
    CHECK(coord.line_begin.byte_offset == 10);
    CHECK(coord.line_next.byte_offset == 11);
    CHECK(coord.column == 1);

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
    CHECK(doc.coord_from_pos(TextPos(0), &line));

    for(;;)
    {
        TextCoord next;

        CHECK(doc.next_line_from_coord(&line, 1, &next));

        if(next.line_begin.byte_offset == line.line_begin.byte_offset)
            break;

        line = next;
        walked++;
    }

    CHECK(doc.coord_from_document_end(&eof));
    CHECK(doc.linecount() == expected_lines);
    CHECK(walked == expected_lines);
    CHECK(eof.line_begin.byte_offset == line.line_begin.byte_offset);

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

        CHECK(doc.coord_from_pos(TextPos(0), &line));
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

void textdocument_linebreaks_seen()
{
    const DocTestEncoding encodings[] = { DOC_ASCII, DOC_UTF8_BOM, DOC_UTF16LE_BOM, DOC_UTF16BE_BOM };

    for(size_t i = 0; i < sizeof(encodings) / sizeof(encodings[0]); i++)
    {
        TextDocument doc;
        TCHAR path[MAX_PATH];

        CHECK(write_textdocument_file("crlf\r\nlf\nmore\r\n", encodings[i], path));
        CHECK(doc.init(path));
        CHECK(doc.linebreaks_seen() == (TXL_CRLF | TXL_LF));

        doc.clear();
        DeleteFile(path);
    }
}

void textdocument_charoffset_in_each_encoding()
{
    // a, a character of two UTF-16 units (U+1F600, or two ANSI letters), CRLF
    // and b. starts are where a, the two-unit character, CR, LF and b start,
    // and the end: 0, 1, 3, 4, 5 and 6 characters in.
    struct { TEXT_ENCODING encoding; const char *bytes; size_t length; size_t starts[6]; } cases[] =
    {
        { NCP_ASCII,   "a\xe9\xe8\r\nb",                                                  6, { 0, 1, 3,  4,  5,  6 } },
        { NCP_UTF8,    "a\xf0\x9f\x98\x80\r\nb",                                          8, { 0, 1, 5,  6,  7,  8 } },
        { NCP_UTF8,    "\xef\xbb\xbf" "a\xf0\x9f\x98\x80\r\nb",                          11, { 0, 1, 5,  6,  7,  8 } },
        { NCP_UTF16,   "\xff\xfe" "a\0\x3d\xd8\x00\xde\r\0\n\0b\0",                      14, { 0, 2, 6,  8, 10, 12 } },
        { NCP_UTF16BE, "\xfe\xff" "\0a\xd8\x3d\xde\x00\0\r\0\n\0b",                      14, { 0, 2, 6,  8, 10, 12 } },
        { NCP_UTF32,   "\xff\xfe\0\0" "a\0\0\0\x00\xf6\x01\x00\r\0\0\0\n\0\0\0b\0\0\0",   24, { 0, 4, 8, 12, 16, 20 } },
        { NCP_UTF32BE, "\0\0\xfe\xff" "\0\0\0a\x00\x01\xf6\x00\0\0\0\r\0\0\0\n\0\0\0b",   24, { 0, 4, 8, 12, 16, 20 } },
    };
    const size_w chars[6] = { 0, 1, 3, 4, 5, 6 };

    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        TextDocument doc;
        TCHAR path[MAX_PATH];
        size_w offset_chars = 0;

        CHECK(write_temp_file(cases[i].bytes, cases[i].length, path));
        CHECK(doc.init(path, cases[i].encoding));

        for(size_t k = 0; k < 6; k++)
        {
            CHECK(doc.charoffset_from_pos(TextPos(cases[i].starts[k]), &offset_chars) == TDR_OK);
            CHECK(offset_chars == chars[k]);
        }

        // the whole document, without a byte-order mark
        CHECK(doc.charcount(&offset_chars) == TDR_OK);
        CHECK(offset_chars == 6);

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

    CHECK(doc.coord_from_pos(TextPos(6), &coord));

    CHECK(doc.previous_line_from_coord(&coord, 1, &line));
    CHECK(line.byte_offset == 0);
    CHECK(line.line_begin.byte_offset == 0);
    CHECK(line.line_next.byte_offset == 5);
    CHECK(line.column == 0);

    CHECK(doc.next_line_from_coord(&coord, 1, &line));
    CHECK(line.byte_offset == 9);
    CHECK(line.line_begin.byte_offset == 9);
    CHECK(line.line_next.byte_offset == 13);
    CHECK(line.column == 0);

    CHECK(doc.coord_from_line_pos(&line, 2, &at_pos));
    CHECK(at_pos.byte_offset == 11);
    CHECK(at_pos.column == 2);

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

    CHECK(doc.coord_from_pos(TextPos(4), &coord));

    CHECK(doc.previous_line_from_coord(&coord, 1, &line));
    CHECK(line.byte_offset == 0);
    CHECK(line.line_begin.byte_offset == 0);
    CHECK(line.line_next.byte_offset == 3);
    CHECK(line.column == 0);

    CHECK(doc.next_line_from_coord(&coord, 1, &line));
    CHECK(line.byte_offset == 10);
    CHECK(line.line_begin.byte_offset == 10);
    CHECK(line.line_next.byte_offset == 11);
    CHECK(line.column == 0);

    CHECK(doc.coord_from_pos(TextPos(3), &line));
    CHECK(doc.coord_from_line_pos(&line, 3, &at_pos));
    CHECK(at_pos.byte_offset == 8);
    CHECK(at_pos.column == 3);

    doc.clear();
    DeleteFile(path);
}

void textdocument_coord_tracks_lazy_eof_after_newline_edits()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    const char line[] = "abcdefghijklmnopqrstuvwxyz 0123456789\r\n";
    const size_t line_length = strlen(line);
    const size_w line_count = (TextDocumentLineIndexProbe::open_index_length() + MEM_BLOCK_SIZE * 4) / line_length;
    TextCoord before;
    TextCoord after;
    TextCoord top;
    TCHAR newlines[] = TEXT("\r\n\r\n\r\n");
    bool exact;

    CHECK(write_large_pattern_file(line, line_length, line_count, path));
    CHECK(doc.init(path));
    CHECK(!TextDocumentLineIndexProbe::linecount_known(doc));

    CHECK(doc.coord_from_document_end(&before));
    CHECK(doc.insert_text(before, newlines, 6, 0) == 6);
    CHECK(doc.coord_from_document_end(&after));

    CHECK(after.byte_offset == before.byte_offset + 6);
    CHECK(after.line_begin.byte_offset == before.line_begin.byte_offset + 6);
    CHECK(doc.previous_line_from_coord(&after, 3, &top));
    CHECK(top.line_begin.byte_offset == before.line_begin.byte_offset);
    CHECK(doc_lineno(doc, after, &exact) >= doc_lineno(doc, top, &exact));

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

    CHECK(doc.coord_from_pos(TextPos(0), &top));
    CHECK(doc.coord_from_pos(TextPos(9), &second));

    // Typing on the top line: its start stays put but its end moves.
    CHECK(doc_insert(doc, 2, xyz, 3, &change) == 3);
    CHECK(change.at.byte_offset == 2 && change.erased == 0 && change.inserted == 3);
    CHECK(doc.coord_after_change(&top, &change));
    CHECK(top.byte_offset == 0);
    CHECK(top.line_next.byte_offset == 10);
    CHECK(doc.next_line_from_coord(&top, 1, &next));
    CHECK(next.line_begin.byte_offset == 10);
    CHECK(next.line_next.byte_offset == 17);

    CHECK(doc.coord_after_change(&second, &change));
    CHECK(second.byte_offset == 12);
    CHECK(second.line_begin.byte_offset == 10);

    // A coordinate exactly at an insertion stays before the inserted text.
    CHECK(doc_insert(doc, 10, xyz, 3, &change) == 3);
    CHECK(doc.coord_after_change(&next, &change));
    CHECK(next.byte_offset == 10);
    CHECK(next.line_next.byte_offset == 20);

    // A coordinate inside erased text moves to the start of the erase.
    CHECK(doc.coord_from_pos(TextPos(12), &second));
    CHECK(doc_erase(doc, 11, 15, &change) == 4);
    CHECK(change.at.byte_offset == 11 && change.erased == 4 && change.inserted == 0);
    CHECK(doc.coord_after_change(&second, &change));
    CHECK(second.byte_offset == 11);
    CHECK(second.line_begin.byte_offset == 10);

    CHECK(doc.undo(&change));
    CHECK(change.at.byte_offset == 11 && change.erased == 0 && change.inserted == 4);

    doc.clear();
    DeleteFile(path);
}

std::wstring read_range(TextDocument &doc, ULONG from_byte, ULONG to_byte)
{
    TextCoord from;
    TextCoord to;
    TCHAR buf[64];
    ULONG len;

    CHECK(doc.coord_from_pos(TextPos(from_byte), &from));
    CHECK(doc.coord_from_pos(TextPos(to_byte), &to));

    TextReader reader = doc.text_from_range(from, to);
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

    CHECK(doc.coord_from_pos(TextPos(1), &from));
    CHECK(doc.insert_text(from, xyz, 3, &change) == 3);
    CHECK(change.at.byte_offset == 1 && change.erased == 0 && change.inserted == 3);
    CHECK(read_range(doc, 0, 17) == L"hXYZello\r\nworld\r\n");

    // A range may be given in either order.
    CHECK(doc.coord_from_pos(TextPos(8), &from));
    CHECK(doc.coord_from_pos(TextPos(4), &to));
    CHECK(doc.erase_text(from, to, &change) == 4);
    CHECK(change.at.byte_offset == 4 && change.erased == 4 && change.inserted == 0);
    CHECK(read_range(doc, 0, 13) == L"hXYZ\r\nworld\r\n");

    CHECK(doc.coord_from_pos(TextPos(6), &from));
    CHECK(doc.coord_from_pos(TextPos(11), &to));
    CHECK(doc.replace_text(from, to, xyz, 3, &change) == 3);
    CHECK(change.at.byte_offset == 6 && change.erased == 5 && change.inserted == 3);
    CHECK(read_range(doc, 0, 11) == L"hXYZ\r\nXYZ\r\n");
    CHECK(read_range(doc, 6, 9) == L"XYZ");

    // Undoing the replace brings back the erased word as one change.
    CHECK(doc.undo(&change));
    CHECK(change.at.byte_offset == 6 && change.erased == 3 && change.inserted == 5);
    CHECK(read_range(doc, 0, 13) == L"hXYZ\r\nworld\r\n");

    CHECK(doc.redo(&change));
    CHECK(change.at.byte_offset == 6 && change.erased == 5 && change.inserted == 3);

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
    CHECK(change.at.byte_offset == 2 && change.erased == 0 && change.inserted == 2);

    CHECK(doc.undo(&change));
    CHECK(change.at.byte_offset == 2 && change.erased == 2 && change.inserted == 0);

    doc.clear();
    DeleteFile(path);
}

void textdocument_find_text_searches_raw_sequence()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TCHAR alpha[] = TEXT("alpha");
    TCHAR missing[] = TEXT("omega");
    size_w match = static_cast<size_w>(-1);
    size_w match_length = 0;

    CHECK(write_textdocument_file("alpha beta alpha", DOC_ASCII, path));
    CHECK(doc.init(path));

    CHECK(doc_find(doc, alpha, 5, 0, doc.byte_length(), false, true, &match, &match_length));
    CHECK(match == 0);
    CHECK(match_length == 5);

    CHECK(doc_find(doc, alpha, 5, 1, doc.byte_length(), false, true, &match, &match_length));
    CHECK(match == 11);
    CHECK(match_length == 5);

    CHECK(doc_find(doc, alpha, 5, 0, 11, true, true, &match, &match_length));
    CHECK(match == 0);
    CHECK(match_length == 5);

    CHECK(doc_find(doc, alpha, 5, 0, doc.byte_length(), true, true, &match, &match_length));
    CHECK(match == 11);
    CHECK(match_length == 5);

    CHECK(!doc_find(doc, missing, 5, 0, doc.byte_length(), false, true, &match, &match_length));

    doc.clear();
    DeleteFile(path);
}

void textdocument_find_text_matches_ascii_case_insensitive()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TCHAR alpha[] = TEXT("alpha");
    size_w match = static_cast<size_w>(-1);
    size_w match_length = 0;

    CHECK(write_textdocument_file("ALPHA beta", DOC_UTF16LE_BOM, path));
    CHECK(doc.init(path));

    CHECK(!doc_find(doc, alpha, 5, 0, doc.byte_length(), false, true, &match, &match_length));
    CHECK(doc_find(doc, alpha, 5, 0, doc.byte_length(), false, false, &match, &match_length));
    CHECK(match == 0);
    CHECK(match_length == 10);

    doc.clear();
    DeleteFile(path);
}

void textdocument_find_text_matches_unicode_case_insensitive_across_encodings()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TCHAR cafe[] = { 'c', 'a', 'f', 0x00e9, 0 };
    size_w match = static_cast<size_w>(-1);
    size_w match_length = 0;

    const char utf8[] =
    {
        static_cast<char>(0xef), static_cast<char>(0xbb), static_cast<char>(0xbf),
        'C', 'A', 'F', static_cast<char>(0xc3), static_cast<char>(0x89)
    };
    CHECK(write_temp_file(utf8, sizeof(utf8), path));
    CHECK(doc.init(path));
    CHECK(doc_find(doc, cafe, 4, 0, doc.byte_length(), false, false, &match, &match_length));
    CHECK(match == 0);
    CHECK(match_length == 5);
    doc.clear();
    DeleteFile(path);

    const WCHAR utf16[] = { 0xfeff, 'C', 'A', 'F', 0x00c9 };
    CHECK(write_temp_file(reinterpret_cast<const char *>(utf16), sizeof(utf16), path));
    CHECK(doc.init(path));
    CHECK(doc_find(doc, cafe, 4, 0, doc.byte_length(), false, false, &match, &match_length));
    CHECK(match == 0);
    CHECK(match_length == 8);
    doc.clear();
    DeleteFile(path);
}

void textdocument_find_text_converts_query_to_document_encoding()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TCHAR beta[] = TEXT("beta");
    size_w match = static_cast<size_w>(-1);
    size_w match_length = 0;

    CHECK(write_textdocument_file("alpha beta gamma", DOC_UTF16BE_BOM, path));
    CHECK(doc.init(path));

    CHECK(doc_find(doc, beta, 4, 0, doc.byte_length(), false, true, &match, &match_length));
    CHECK(match == 12);
    CHECK(match_length == 8);

    doc.clear();
    DeleteFile(path);
}

void textdocument_find_text_nocase_slides_window()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TCHAR alpha[] = TEXT("alpha");
    TCHAR z[] = TEXT("Z");
    size_w match = static_cast<size_w>(-1);
    size_w match_length = 0;

    CHECK(write_textdocument_file("zz Alpha zz aLPHA zb", DOC_ASCII, path));
    CHECK(doc.init(path));

    // the window wraps round several times before each match
    CHECK(doc_find(doc, alpha, 5, 0, doc.byte_length(), false, false, &match, &match_length));
    CHECK(match == 3);
    CHECK(match_length == 5);

    CHECK(doc_find(doc, alpha, 5, 4, doc.byte_length(), false, false, &match, &match_length));
    CHECK(match == 12);
    CHECK(match_length == 5);

    CHECK(doc_find(doc, alpha, 5, 0, doc.byte_length(), true, false, &match, &match_length));
    CHECK(match == 12);
    CHECK(match_length == 5);

    CHECK(doc_find(doc, alpha, 5, 0, 16, true, false, &match, &match_length));
    CHECK(match == 3);
    CHECK(match_length == 5);

    // a window of a single unit
    CHECK(doc_find(doc, z, 1, 11, doc.byte_length(), false, false, &match, &match_length));
    CHECK(match == 18);
    CHECK(match_length == 1);

    CHECK(doc_find(doc, z, 1, 0, 18, true, false, &match, &match_length));
    CHECK(match == 10);
    CHECK(match_length == 1);

    doc.clear();
    DeleteFile(path);
}

void textdocument_find_text_nocase_backwards_over_utf8()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TCHAR cafe[] = { 'c', 'a', 'f', 0x00e9, 0 };
    size_w match = static_cast<size_w>(-1);
    size_w match_length = 0;

    // "cafe CAFE x", each final e accented as a 2-byte sequence
    const ULONG utf8[] =
    {
        0xef, 0xbb, 0xbf,
        'c', 'a', 'f', 0xc3, 0xa9, ' ', 'C', 'A', 'F', 0xc3, 0x89, ' ', 'x'
    };

    CHECK(write_units_file(utf8, sizeof(utf8) / sizeof(utf8[0]), 1, false, path));
    CHECK(doc.init(path));

    CHECK(doc_find(doc, cafe, 4, 0, doc.byte_length(), true, false, &match, &match_length));
    CHECK(match == 6);
    CHECK(match_length == 5);

    CHECK(doc_find(doc, cafe, 4, 0, 6, true, false, &match, &match_length));
    CHECK(match == 0);
    CHECK(match_length == 5);

    // ending the search between the bytes of the second accented E leaves only the first match
    CHECK(doc_find(doc, cafe, 4, 0, 10, true, false, &match, &match_length));
    CHECK(match == 0);
    CHECK(match_length == 5);

    doc.clear();
    DeleteFile(path);
}

void textdocument_find_text_nocase_matches_supplementary_chars_in_each_encoding()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TCHAR pattern[] = { 0xd83d, 0xde00, 'a', 0 };
    size_w match = static_cast<size_w>(-1);
    size_w match_length = 0;

    // U+1F600 then A, twice over with a space between
    const ULONG utf8[]  = { 0xef, 0xbb, 0xbf, 0xf0, 0x9f, 0x98, 0x80, 'A', ' ', 0xf0, 0x9f, 0x98, 0x80, 'A' };
    const ULONG utf16[] = { 0xfeff, 0xd83d, 0xde00, 'A', ' ', 0xd83d, 0xde00, 'A' };
    const ULONG utf32[] = { 0xfeff, 0x1f600, 'A', ' ', 0x1f600, 'A' };

    const struct
    {
        const ULONG *units;
        size_t count;
        size_t unit_size;
        bool big_endian;
        size_w second;
        size_w length;
    }
    files[] =
    {
        { utf8,  sizeof(utf8) / sizeof(utf8[0]),   1, false, 6,  5 },
        { utf16, sizeof(utf16) / sizeof(utf16[0]), 2, false, 8,  6 },
        { utf16, sizeof(utf16) / sizeof(utf16[0]), 2, true,  8,  6 },
        { utf32, sizeof(utf32) / sizeof(utf32[0]), 4, false, 12, 8 },
        { utf32, sizeof(utf32) / sizeof(utf32[0]), 4, true,  12, 8 },
    };

    for(size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++)
    {
        CHECK(write_units_file(files[i].units, files[i].count, files[i].unit_size, files[i].big_endian, path));
        CHECK(doc.init(path));

        CHECK(doc_find(doc, pattern, 3, 0, doc.byte_length(), false, false, &match, &match_length));
        CHECK(match == 0);
        CHECK(match_length == files[i].length);

        CHECK(doc_find(doc, pattern, 3, 0, doc.byte_length(), true, false, &match, &match_length));
        CHECK(match == files[i].second);
        CHECK(match_length == files[i].length);

        doc.clear();
        DeleteFile(path);
    }
}

void textdocument_find_text_nocase_steps_over_split_surrogate_pair()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TCHAR pattern[] = { 0xd83d, 0xde00, 'a', 0 };
    size_w match = static_cast<size_w>(-1);
    size_w match_length = 0;

    const ULONG utf16[] = { 0xfeff, 0xd83d, 0xde00, 'A', ' ', 0xd83d, 0xde00, 'A' };

    CHECK(write_units_file(utf16, sizeof(utf16) / sizeof(utf16[0]), 2, false, path));
    CHECK(doc.init(path));

    // starting after the first high surrogate, the search picks up from the lone low one
    CHECK(doc_find(doc, pattern, 3, 2, doc.byte_length(), false, false, &match, &match_length));
    CHECK(match == 8);
    CHECK(match_length == 6);

    // ending after the second high surrogate, the search steps back from the lone high one
    CHECK(doc_find(doc, pattern, 3, 0, 10, true, false, &match, &match_length));
    CHECK(match == 0);
    CHECK(match_length == 6);

    doc.clear();
    DeleteFile(path);
}

void unicode_utf8_prev_steps_back_to_lead_byte()
{
    UTF8 text[] = { 'a', 0xc3, 0xa9, 0xf0, 0x9f, 0x98, 0x80 };
    UTF8 stray[] = { 0xc3, 0xa9, 0xa9 };
    UTF8 headless[] = { 0x9f, 0x98, 0x80 };
    UTF32 ch32 = 0;

    // a 4-byte sequence, the 2-byte one before it, then plain ASCII
    CHECK(utf8_to_utf32_prev(text, 7, &ch32) == 4);
    CHECK(ch32 == 0x1f600);
    CHECK(utf8_to_utf32_prev(text, 3, &ch32) == 2);
    CHECK(ch32 == 0xe9);
    CHECK(utf8_to_utf32_prev(text, 1, &ch32) == 1);
    CHECK(ch32 == 'a');

    // one trailing byte too many, or trailing bytes with no lead, go one byte at a time
    CHECK(utf8_to_utf32_prev(stray, 3, &ch32) == 1);
    CHECK(ch32 == UNI_REPLACEMENT_CHAR);
    CHECK(utf8_to_utf32_prev(stray, 2, &ch32) == 2);
    CHECK(ch32 == 0xe9);
    CHECK(utf8_to_utf32_prev(headless, 3, &ch32) == 1);
    CHECK(ch32 == UNI_REPLACEMENT_CHAR);
}

void unicode_utf16_prev_joins_surrogate_pair()
{
    // 'A' then U+1F600 as a surrogate pair, in each byte order
    UTF8 le[] = { 'A', 0x00, 0x3d, 0xd8, 0x00, 0xde };
    UTF8 be[] = { 0x00, 'A', 0xd8, 0x3d, 0xde, 0x00 };
    UTF8 lone[] = { 'A', 0x00, 0x00, 0xde };
    UTF32 ch32 = 0;

    CHECK(utf16_to_utf32_prev(le, 6, &ch32) == 4);
    CHECK(ch32 == 0x1f600);
    CHECK(utf16_to_utf32_prev(le, 2, &ch32) == 2);
    CHECK(ch32 == 'A');

    CHECK(utf16be_to_utf32_prev(be, 6, &ch32) == 4);
    CHECK(ch32 == 0x1f600);
    CHECK(utf16be_to_utf32_prev(be, 2, &ch32) == 2);
    CHECK(ch32 == 'A');

    // a low surrogate with no high surrogate before it stands alone
    CHECK(utf16_to_utf32_prev(lone, 4, &ch32) == 2);
    CHECK(ch32 == 0xde00);
    CHECK(utf16_to_utf32_prev(le + 4, 2, &ch32) == 2);
    CHECK(ch32 == 0xde00);
    CHECK(utf16be_to_utf32_prev(be + 4, 2, &ch32) == 2);
    CHECK(ch32 == 0xde00);
}

void unicode_utf32_char_reads_both_byte_orders()
{
    UTF8 le[] = { 0x00, 0xf6, 0x01, 0x00 };
    UTF8 be[] = { 0x00, 0x01, 0xf6, 0x00 };
    UTF8 beyond[] = { 0x00, 0x00, 0x11, 0x00 };
    UTF32 ch32 = 0;

    CHECK(utf32_to_utf32_char(le, 4, &ch32) == 4);
    CHECK(ch32 == 0x1f600);
    CHECK(utf32be_to_utf32_char(be, 4, &ch32) == 4);
    CHECK(ch32 == 0x1f600);

    // 0x110000 is past the end of Unicode
    CHECK(utf32_to_utf32_char(beyond, 4, &ch32) == 4);
    CHECK(ch32 == UNI_REPLACEMENT_CHAR);

    // a partial unit is not a character
    CHECK(utf32_to_utf32_char(le, 3, &ch32) == 0);
    CHECK(utf32be_to_utf32_char(be, 3, &ch32) == 0);
}

void unicode_utf8_complete_length_leaves_out_cut_sequence()
{
    UTF8 text[] = { 'a', 'b', 0xe4, 0xb8, 0xad };
    UTF8 four[] = { 0xf0, 0x9f, 0x98, 0x80 };
    UTF8 stray[] = { 'a', 0x80, 0x80 };

    CHECK(utf8_complete_length(text, 5) == 5);
    CHECK(utf8_complete_length(text, 4) == 2);
    CHECK(utf8_complete_length(text, 3) == 2);
    CHECK(utf8_complete_length(text, 2) == 2);
    CHECK(utf8_complete_length(four, 4) == 4);
    CHECK(utf8_complete_length(four, 3) == 0);

    // trailing bytes with no lead byte waiting for them are already whole
    CHECK(utf8_complete_length(stray, 3) == 3);
}

void unicode_utf16_to_utf8_leaves_character_that_does_not_fit()
{
    // 'a', a 3-byte character, then a 4-byte one
    UTF16 text[] = { 'a', 0x4e2d, 0xd83d, 0xde00 };
    UTF8 out[8];
    size_t len;

    len = 3;
    CHECK(utf16_to_utf8(text, 4, out, &len) == 1);
    CHECK(len == 1);

    len = 4;
    CHECK(utf16_to_utf8(text, 4, out, &len) == 2);
    CHECK(len == 4);

    len = 7;
    CHECK(utf16_to_utf8(text, 4, out, &len) == 2);
    CHECK(len == 4);

    len = 8;
    CHECK(utf16_to_utf8(text, 4, out, &len) == 4);
    CHECK(len == 8);
}

void unicode_utf32_streams_convert_both_byte_orders()
{
    // 'A' then U+1F600, then half a unit
    UTF8 le[] = { 'A', 0, 0, 0, 0x00, 0xf6, 0x01, 0x00, 'B', 0 };
    UTF8 be[] = { 0, 0, 0, 'A', 0x00, 0x01, 0xf6, 0x00, 0, 'B' };

    // 'A', U+1F600, then a lone low surrogate
    UTF16 text[] = { 'A', 0xd83d, 0xde00, 0xdc00 };
    const UTF8 text_le[] = { 'A', 0, 0, 0, 0x00, 0xf6, 0x01, 0x00, 0xfd, 0xff, 0x00, 0x00 };
    const UTF8 text_be[] = { 0, 0, 0, 'A', 0x00, 0x01, 0xf6, 0x00, 0x00, 0x00, 0xff, 0xfd };

    UTF16 out16[4];
    UTF8 out32[16];
    size_t len;

    len = 4;
    CHECK(utf32le_to_utf16(le, sizeof(le), out16, &len) == 8);
    CHECK(len == 3);
    CHECK(out16[0] == 'A' && out16[1] == 0xd83d && out16[2] == 0xde00);

    len = 4;
    CHECK(utf32be_to_utf16(be, sizeof(be), out16, &len) == 8);
    CHECK(len == 3);
    CHECK(out16[0] == 'A' && out16[1] == 0xd83d && out16[2] == 0xde00);

    // a surrogate pair with no room for both halves is left for next time
    len = 2;
    CHECK(utf32le_to_utf16(le, sizeof(le), out16, &len) == 4);
    CHECK(len == 1);

    // a lone surrogate has no UTF-32 form
    len = sizeof(out32);
    CHECK(utf16_to_utf32le(text, 4, out32, &len) == 4);
    CHECK(len == 12);
    CHECK(memcmp(out32, text_le, 12) == 0);

    len = sizeof(out32);
    CHECK(utf16_to_utf32be(text, 4, out32, &len) == 4);
    CHECK(len == 12);
    CHECK(memcmp(out32, text_be, 12) == 0);

    // a character with no room is left for next time
    len = 7;
    CHECK(utf16_to_utf32le(text, 4, out32, &len) == 1);
    CHECK(len == 4);
}

// The whole of the line containing offset, as the view reads it to draw
std::wstring read_line(TextDocument &doc, size_w offset)
{
    TextCoord line;
    std::vector<TCHAR> buf(static_cast<size_t>(doc.byte_length()) + 1);

    CHECK(doc.coord_from_pos(TextPos(offset), &line));
    return std::wstring(&buf[0], doc.getline(line, &buf[0], static_cast<ULONG>(buf.size())));
}

// The whole document, as the view reads it to copy
std::wstring read_document(TextDocument &doc)
{
    TextCoord from;
    TextCoord to;
    std::vector<TCHAR> buf(static_cast<size_t>(doc.byte_length()) + 1);

    CHECK(doc.coord_from_pos(TextPos(0), &from));
    CHECK(doc.coord_from_document_end(&to));

    TextReader reader = doc.text_from_range(from, to);
    return std::wstring(&buf[0], reader.read(&buf[0], static_cast<ULONG>(buf.size())));
}

void textdocument_utf8_decodes_characters_across_blocks()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TextCoord line;
    TextCoord coord;
    std::vector<ULONG> bytes;
    std::wstring expected(255, 'a');

    // 255 bytes of 'a' put the two bytes of an accented e either side of byte 256
    bytes.push_back(0xef);
    bytes.push_back(0xbb);
    bytes.push_back(0xbf);
    bytes.insert(bytes.end(), 255, static_cast<ULONG>('a'));
    bytes.push_back(0xc3);
    bytes.push_back(0xa9);
    bytes.push_back('b');
    expected += static_cast<wchar_t>(0x00e9);
    expected += 'b';

    CHECK(write_units_file(&bytes[0], bytes.size(), 1, false, path));
    CHECK(doc.init(path));

    CHECK(read_line(doc, 0) == expected);
    CHECK(read_document(doc) == expected);

    CHECK(doc.coord_from_pos(TextPos(258), &coord));
    CHECK(coord.column == 257);

    CHECK(doc.coord_from_pos(TextPos(0), &line));
    CHECK(doc.coord_from_line_pos(&line, 256, &coord));
    CHECK(coord.byte_offset == 257);

    doc.clear();
    DeleteFile(path);

    // 3-byte characters cross two block boundaries in three
    bytes.resize(3);
    for(int i = 0; i < 100; i++)
    {
        bytes.push_back(0xe4);
        bytes.push_back(0xb8);
        bytes.push_back(0xad);
    }
    expected.assign(100, static_cast<wchar_t>(0x4e2d));

    CHECK(write_units_file(&bytes[0], bytes.size(), 1, false, path));
    CHECK(doc.init(path));

    CHECK(read_line(doc, 0) == expected);
    CHECK(read_document(doc) == expected);

    CHECK(doc.coord_from_pos(TextPos(300), &coord));
    CHECK(coord.column == 100);

    CHECK(doc.coord_from_pos(TextPos(0), &line));
    CHECK(doc.coord_from_line_pos(&line, 90, &coord));
    CHECK(coord.byte_offset == 270);

    doc.clear();
    DeleteFile(path);
}

void textdocument_utf8_edits_keep_characters_across_buffers()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    const ULONG bom[] = { 0xef, 0xbb, 0xbf };
    std::wstring text(255, 'a');
    std::wstring cjk(100, static_cast<wchar_t>(0x4e2d));
    TCHAR xy[] = TEXT("xy");

    // the accented e doesn't fit in what is left of the first 256-byte buffer
    text += static_cast<wchar_t>(0x00e9);
    text += 'b';

    CHECK(write_units_file(bom, 3, 1, false, path));
    CHECK(doc.init(path));
    CHECK(doc_insert(doc, 0, &text[0], static_cast<ULONG>(text.size())) == 258);
    CHECK(doc.byte_length() == 258);
    CHECK(read_document(doc) == text);
    doc.clear();
    DeleteFile(path);

    // nor do the 86th and later of these 3-byte characters
    CHECK(write_units_file(bom, 3, 1, false, path));
    CHECK(doc.init(path));
    CHECK(doc_insert(doc, 0, &cjk[0], static_cast<ULONG>(cjk.size())) == 300);
    CHECK(read_document(doc) == cjk);
    doc.clear();
    DeleteFile(path);

    // replacing goes through the same buffer
    CHECK(write_units_file(bom, 3, 1, false, path));
    CHECK(doc.init(path));
    CHECK(doc_insert(doc, 0, xy, 2) == 2);
    CHECK(doc_replace(doc, 0, 2, &cjk[0], static_cast<ULONG>(cjk.size())) == 300);
    CHECK(read_document(doc) == cjk);
    doc.clear();
    DeleteFile(path);
}

void textdocument_utf32_reads_and_edits_text()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TCHAR b[] = TEXT("b");
    TCHAR x[] = TEXT("x");
    TCHAR face[] = { 0xd83d, 0xde00, 0 };
    TextCoord coord;
    size_w match = static_cast<size_w>(-1);
    size_w match_length = 0;
    seqchar raw[4];
    const ULONG abc[] = { 0xfeff, 'a', 'b', 'c' };

    for(int big_endian = 0; big_endian < 2; big_endian++)
    {
        CHECK(write_units_file(abc, 4, 4, big_endian != 0, path));
        CHECK(doc.init(path));

        CHECK(read_line(doc, 0) == L"abc");
        CHECK(doc.coord_from_pos(TextPos(12), &coord));
        CHECK(coord.column == 3);

        CHECK(doc_find(doc, b, 1, 0, doc.byte_length(), false, true, &match, &match_length));
        CHECK(match == 4);
        CHECK(match_length == 4);

        // an insert is stored in the document's own byte order, after the 4-byte BOM
        CHECK(doc_insert(doc, 0, x, 1) == 4);
        CHECK(TextDocumentLineIndexProbe::seq(doc).render(4, raw, 4) == 4);
        CHECK(raw[big_endian ? 3 : 0] == 'x');
        CHECK(raw[big_endian ? 0 : 3] == 0);

        // a surrogate pair is one UTF-32 unit
        CHECK(doc_replace(doc, 8, 12, face, 2) == 4);
        CHECK(read_document(doc) == std::wstring(L"xa") + face + L"c");

        CHECK(doc_find(doc, face, 2, 0, doc.byte_length(), false, true, &match, &match_length));
        CHECK(match == 8);
        CHECK(match_length == 4);

        doc.clear();
        DeleteFile(path);
    }
}

void textdocument_find_text_matchcase_stays_on_code_units()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TCHAR n[] = TEXT("N");
    size_w match = static_cast<size_w>(-1);
    size_w match_length = 0;

    // U+4E01 U+4E00 is 01 4E 00 4E in UTF-16LE: an "N" (4E 00) at byte 1, across two characters
    const ULONG before[] = { 0xfeff, 0x4e01, 0x4e00, 'N' };
    const ULONG after[]  = { 0xfeff, 'N', 0x4e01, 0x4e00 };

    // U+4E01 U+10000 is 01 4E 00 00 00 00 01 00 in UTF-32LE: an "N" at byte 1
    const ULONG utf32[]  = { 0xfeff, 0x4e01, 0x10000 };

    CHECK(write_units_file(before, 4, 2, false, path));
    CHECK(doc.init(path));
    CHECK(doc_find(doc, n, 1, 0, doc.byte_length(), false, true, &match, &match_length));
    CHECK(match == 4);
    CHECK(match_length == 2);
    doc.clear();
    DeleteFile(path);

    CHECK(write_units_file(after, 4, 2, false, path));
    CHECK(doc.init(path));
    CHECK(doc_find(doc, n, 1, 0, doc.byte_length(), true, true, &match, &match_length));
    CHECK(match == 0);
    CHECK(match_length == 2);
    doc.clear();
    DeleteFile(path);

    CHECK(write_units_file(utf32, 3, 4, false, path));
    CHECK(doc.init(path));
    CHECK(!doc_find(doc, n, 1, 0, doc.byte_length(), false, true, &match, &match_length));
    CHECK(!doc_find(doc, n, 1, 0, doc.byte_length(), true, true, &match, &match_length));
    doc.clear();
    DeleteFile(path);
}

void textpos_compares_and_measures()
{
    TextPos a(3);
    TextPos b(5);
    TextCoord coord;

    CHECK(a < b);
    CHECK(a <= b);
    CHECK(b > a);
    CHECK(b >= a);
    CHECK(a != b);
    CHECK(!(a == b));
    CHECK(b - a == 2);
    CHECK(TextPos() == TextPos(0));

    // a coordinate compares and measures as its position alone
    coord.byte_offset = 5;
    coord.line_begin = TextPos(4);
    CHECK(coord == b);
    CHECK(coord - a == 2);
    CHECK(a < coord);
}

void textdocument_coord_from_line_pos_rounds_back_inside_pair()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TextCoord line;
    TextCoord coord;
    TextCoord measured;

    // "a", U+1F600, "x": columns 0 a 1 [pair] 3 x 4, with column 2 between the halves
    const ULONG utf8[]  = { 0xef, 0xbb, 0xbf, 'a', 0xf0, 0x9f, 0x98, 0x80, 'x' };
    const ULONG utf16[] = { 0xfeff, 'a', 0xd83d, 0xde00, 'x' };
    const ULONG utf32[] = { 0xfeff, 'a', 0x1f600, 'x' };

    const struct
    {
        const ULONG *units;
        size_t count;
        size_t unit_size;
        bool big_endian;
        size_w after_a;
        size_w after_face;
        size_w after_x;
    }
    files[] =
    {
        { utf8,  sizeof(utf8) / sizeof(utf8[0]),   1, false, 1, 5, 6  },
        { utf16, sizeof(utf16) / sizeof(utf16[0]), 2, false, 2, 6, 8  },
        { utf16, sizeof(utf16) / sizeof(utf16[0]), 2, true,  2, 6, 8  },
        { utf32, sizeof(utf32) / sizeof(utf32[0]), 4, false, 4, 8, 12 },
        { utf32, sizeof(utf32) / sizeof(utf32[0]), 4, true,  4, 8, 12 },
    };

    for(size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++)
    {
        CHECK(write_units_file(files[i].units, files[i].count, files[i].unit_size, files[i].big_endian, path));
        CHECK(doc.init(path));
        CHECK(doc.coord_from_pos(TextPos(0), &line));

        CHECK(doc.coord_from_line_pos(&line, 1, &coord));
        CHECK(coord.byte_offset == files[i].after_a);
        CHECK(coord.column == 1);

        // between the halves of the pair: back to the pair's start
        CHECK(doc.coord_from_line_pos(&line, 2, &coord));
        CHECK(coord.byte_offset == files[i].after_a);
        CHECK(coord.column == 1);

        CHECK(doc.coord_from_line_pos(&line, 3, &coord));
        CHECK(coord.byte_offset == files[i].after_face);
        CHECK(coord.column == 3);

        CHECK(doc.coord_from_line_pos(&line, 4, &coord));
        CHECK(coord.byte_offset == files[i].after_x);
        CHECK(coord.column == 4);

        // a coordinate's column always matches its position
        CHECK(doc.coord_from_pos(coord, &measured));
        CHECK(measured.column == coord.column);

        doc.clear();
        DeleteFile(path);
    }
}

void textdocument_pos_after_change()
{
    TextDocument doc;

    // bytes [2, 4) replaced by 3 bytes
    TextChange change = { TextPos(2), 2, 3 };

    CHECK(TextDocumentLineIndexProbe::pos_after_change(doc, TextPos(1), &change) == TextPos(1));
    CHECK(TextDocumentLineIndexProbe::pos_after_change(doc, TextPos(2), &change) == TextPos(2));
    CHECK(TextDocumentLineIndexProbe::pos_after_change(doc, TextPos(3), &change) == TextPos(2));
    CHECK(TextDocumentLineIndexProbe::pos_after_change(doc, TextPos(4), &change) == TextPos(5));
    CHECK(TextDocumentLineIndexProbe::pos_after_change(doc, TextPos(6), &change) == TextPos(7));
}

void textdocument_find_text_reports_begin_and_end()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TCHAR beta[] = TEXT("beta");
    TCHAR upper[] = TEXT("BETA");
    TextPos begin;
    TextPos end;

    CHECK(write_textdocument_file("alpha beta", DOC_UTF16LE_BOM, path));
    CHECK(doc.init(path));

    CHECK(doc.find_text(beta, 4, TextPos(0), TextPos(doc.byte_length()), false, true, &begin, &end));
    CHECK(begin == TextPos(12));
    CHECK(end == TextPos(20));

    begin = end = TextPos();
    CHECK(doc.find_text(upper, 4, TextPos(0), TextPos(doc.byte_length()), true, false, &begin, &end));
    CHECK(begin == TextPos(12));
    CHECK(end == TextPos(20));

    doc.clear();
    DeleteFile(path);
}

void textreader_reads_whole_characters()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TCHAR buf[4];
    TextReader reader;

    // "a", U+1F600, "b"
    const ULONG utf8[]  = { 0xef, 0xbb, 0xbf, 'a', 0xf0, 0x9f, 0x98, 0x80, 'b' };
    const ULONG utf16[] = { 0xfeff, 'a', 0xd83d, 0xde00, 'b' };
    const ULONG utf32[] = { 0xfeff, 'a', 0x1f600, 'b' };

    const struct
    {
        const ULONG *units;
        size_t count;
        size_t unit_size;
        bool big_endian;
        size_w after_a;
        size_w after_face;
        size_w after_b;
    }
    files[] =
    {
        { utf8,  sizeof(utf8) / sizeof(utf8[0]),   1, false, 1, 5, 6  },
        { utf16, sizeof(utf16) / sizeof(utf16[0]), 2, false, 2, 6, 8  },
        { utf16, sizeof(utf16) / sizeof(utf16[0]), 2, true,  2, 6, 8  },
        { utf32, sizeof(utf32) / sizeof(utf32[0]), 4, false, 4, 8, 12 },
        { utf32, sizeof(utf32) / sizeof(utf32[0]), 4, true,  4, 8, 12 },
    };

    // nothing to read
    CHECK(reader.at_end());
    CHECK(reader.read(buf, 4) == 0);

    for(size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++)
    {
        CHECK(write_units_file(files[i].units, files[i].count, files[i].unit_size, files[i].big_endian, path));
        CHECK(doc.init(path));

        reader = doc.text_from_range(TextPos(0), TextPos(doc.byte_length()));

        // room for "a" and half the pair: only "a" is read
        CHECK(reader.read(buf, 2) == 1);
        CHECK(buf[0] == 'a');
        CHECK(reader.pos() == TextPos(files[i].after_a));

        // no room for the pair at all: nothing is read, but there is more to come
        CHECK(reader.read(buf, 1) == 0);
        CHECK(!reader.at_end());

        CHECK(reader.read(buf, 2) == 2);
        CHECK(buf[0] == 0xd83d && buf[1] == 0xde00);
        CHECK(reader.pos() == TextPos(files[i].after_face));

        CHECK(reader.read(buf, 4) == 1);
        CHECK(buf[0] == 'b');
        CHECK(reader.pos() == TextPos(files[i].after_b));
        CHECK(reader.at_end());
        CHECK(reader.read(buf, 4) == 0);

        doc.clear();
        DeleteFile(path);
    }
}

// The whole of a line's text, as the iterator reads it
std::wstring iterator_text(const LineIterator &line)
{
    TCHAR buf[64];

    return std::wstring(buf, line.text(buf, 64));
}

void lineiterator_walks_lines()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TCHAR buf[64];
    TextReader reader;

    // lines start at 0, 5 and 9
    CHECK(write_textdocument_file("one\r\ntwo\nthree", DOC_ASCII, path));
    CHECK(doc.init(path));

    // from part way into a line: the iterator is at the line's start
    LineIterator line = doc.line_at(TextPos(6));
    CHECK(line.line() == TextPos(5));
    CHECK(line.line().line_begin == TextPos(5));
    CHECK(line.line().line_next == TextPos(9));
    CHECK(line.line().column == 0);
    CHECK(line.linebreak() == TXL_LF);
    CHECK(iterator_text(line) == L"two\n");

    CHECK(line.next());
    CHECK(line.line() == TextPos(9));
    CHECK(line.linebreak() == 0);
    CHECK(iterator_text(line) == L"three");

    // the last line: no further, and no movement
    CHECK(!line.next());
    CHECK(line.line() == TextPos(9));

    CHECK(line.prev());
    CHECK(line.prev());
    CHECK(line.line() == TextPos(0));
    CHECK(line.linebreak() == TXL_CRLF);

    // the first line: no further back
    CHECK(!line.prev());
    CHECK(line.line() == TextPos(0));

    reader = line.reader();
    CHECK(std::wstring(buf, reader.read(buf, 64)) == L"one\r\n");
    CHECK(reader.at_end());

    doc.clear();
    DeleteFile(path);
}

void lineiterator_ends_with_empty_line_after_final_break()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];

    CHECK(write_textdocument_file("a\n", DOC_ASCII, path));
    CHECK(doc.init(path));

    LineIterator line = doc.line_at(TextPos(0));
    CHECK(line.linebreak() == TXL_LF);

    CHECK(line.next());
    CHECK(line.line() == TextPos(2));
    CHECK(line.line().line_next == TextPos(2));
    CHECK(line.linebreak() == 0);
    CHECK(iterator_text(line) == L"");

    CHECK(!line.next());
    CHECK(line.prev());
    CHECK(line.line() == TextPos(0));

    doc.clear();
    DeleteFile(path);
}

void lineiterator_on_empty_document()
{
    TextDocument doc;
    LineIterator none;

    doc.clear();

    LineIterator line = doc.line_at(TextPos(0));
    CHECK(line.line() == TextPos(0));
    CHECK(!line.next());
    CHECK(!line.prev());
    CHECK(line.linebreak() == 0);
    CHECK(iterator_text(line) == L"");

    // an iterator on no line can't move
    CHECK(!none.next());
    CHECK(!none.prev());
    CHECK(iterator_text(none) == L"");
    CHECK(none.reader().at_end());
}

// A temporary file name, with no file there
bool temp_name(TCHAR path[MAX_PATH])
{
    TCHAR temp_path[MAX_PATH];

    path[0] = 0;

    if(GetTempPath(MAX_PATH, temp_path) == 0 || GetTempFileName(temp_path, TEXT("seq"), 0, path) == 0)
        return false;

    return DeleteFile(path) != 0;
}

bool file_exists(const TCHAR *path)
{
    return GetFileAttributes(path) != INVALID_FILE_ATTRIBUTES;
}

std::string read_file(const TCHAR *path)
{
    std::string bytes;
    HANDLE hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, 0, OPEN_EXISTING, 0, 0);
    char buf[0x10000];
    DWORD got;

    if(hFile == INVALID_HANDLE_VALUE)
        return "<missing>";

    while(ReadFile(hFile, buf, sizeof(buf), &got, 0) && got != 0)
        bytes.append(buf, got);

    CloseHandle(hFile);
    return bytes;
}

void sequence_modified_follows_undo_history()
{
    sequence seq;

    init(seq, "abc");
    CHECK(!seq.modified());

    CHECK(insert_bytes(seq, 0, "X"));
    CHECK(seq.modified());

    // saved as "Xabc"
    seq.mark_saved();
    CHECK(!seq.modified());

    CHECK(insert_bytes(seq, 4, "Y"));
    CHECK(seq.modified());

    // undo and redo across the saved point
    CHECK(seq.undo());
    CHECK(render_content(seq) == "Xabc");
    CHECK(!seq.modified());
    CHECK(seq.redo());
    CHECK(seq.modified());

    // a different edit after undoing replaces the redo history, but undoing it
    // still lands back on the saved text
    CHECK(seq.undo());
    CHECK(insert_bytes(seq, 0, "Z"));
    CHECK(seq.modified());
    CHECK(seq.undo());
    CHECK(render_content(seq) == "Xabc");
    CHECK(!seq.modified());

    // and undoing past it is modified again
    CHECK(seq.undo());
    CHECK(render_content(seq) == "abc");
    CHECK(seq.modified());
}

void sequence_mark_saved_stops_typing_joining_the_saved_record()
{
    sequence seq;

    init(seq, "");

    // typing one character after another goes into one undo record...
    CHECK(insert_bytes(seq, 0, "a"));
    seq.mark_saved();

    // ...but not across a save: "b" would otherwise join the saved "a"
    CHECK(insert_bytes(seq, 1, "b"));
    CHECK(seq.modified());

    CHECK(seq.undo());
    CHECK(render_content(seq) == "a");
    CHECK(!seq.modified());
}

void sequence_modified_treats_group_as_one_edit()
{
    sequence seq;

    init(seq, "abc");
    seq.mark_saved();

    seq.group();
    CHECK(insert_bytes(seq, 0, "X"));
    CHECK(insert_bytes(seq, 4, "Y"));
    seq.ungroup();
    CHECK(seq.modified());

    // the whole group undoes at once, back to the saved text
    CHECK(seq.undo());
    CHECK(render_content(seq) == "abc");
    CHECK(!seq.modified());
    CHECK(!seq.canundo());
}

void textdocument_open_file_can_be_replaced_but_not_written()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TCHAR newpath[MAX_PATH];
    HANDLE writer;

    CHECK(write_textdocument_file("alpha\nbeta", DOC_ASCII, path));
    CHECK(write_textdocument_file("REPLACED", DOC_ASCII, newpath));
    CHECK(doc.init(path));

    // another program can't change the bytes the text is read from
    writer = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, 0, 0);
    CHECK(writer == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION);
    if(writer != INVALID_HANDLE_VALUE)
        CloseHandle(writer);

    // but the file can be replaced, as a save does, and the document still has its text
    CHECK(ReplaceFile(path, newpath, 0, REPLACEFILE_IGNORE_MERGE_ERRORS, 0, 0));
    CHECK(read_document(doc) == L"alpha\nbeta");

    doc.clear();
    DeleteFile(path);
    DeleteFile(newpath);
}

// Save a document as it is: its own encoding, byte-order mark and line breaks
bool save_as_is(TextDocument &doc, const TCHAR *path)
{
    return doc.save(path, doc.getformat(), doc.has_bom(), 0);
}

// Write text with a SaveFile, as a save does
bool save_bytes(const TCHAR *path, const char *text)
{
    SaveFile file;

    return file.open(path) && file.write(text, strlen(text)) && file.commit();
}

void savefile_writes_a_new_file()
{
    TCHAR path[MAX_PATH];
    std::wstring first;

    CHECK(temp_name(path));
    CHECK(save_bytes(path, "hello"));
    CHECK(read_file(path) == "hello");

    // the file written first, beside the target, has become the target
    first = path;
    first += TEXT(".~0");
    CHECK(!file_exists(first.c_str()));

    // saving again replaces it
    CHECK(save_bytes(path, ""));
    CHECK(read_file(path) == "");
    CHECK(!file_exists(first.c_str()));

    DeleteFile(path);
}

void savefile_steps_past_a_file_in_the_way()
{
    TCHAR path[MAX_PATH];
    std::wstring taken;
    std::wstring next;
    HANDLE hFile;
    DWORD written;

    CHECK(temp_name(path));
    taken = path;
    taken += TEXT(".~0");
    next = path;
    next += TEXT(".~1");

    // something already has the name the save would write to first
    hFile = CreateFile(taken.c_str(), GENERIC_WRITE, 0, 0, CREATE_NEW, 0, 0);
    CHECK(hFile != INVALID_HANDLE_VALUE);
    CHECK(WriteFile(hFile, "leftover", 8, &written, 0));
    CloseHandle(hFile);

    CHECK(save_bytes(path, "text"));
    CHECK(read_file(path) == "text");
    CHECK(read_file(taken.c_str()) == "leftover");
    CHECK(!file_exists(next.c_str()));

    DeleteFile(taken.c_str());
    DeleteFile(path);
}

void savefile_leaves_the_file_as_it_was_until_committed()
{
    TCHAR path[MAX_PATH];
    std::wstring other;

    CHECK(write_temp_file("original", 8, path));
    other = path;
    other += TEXT(".~0");

    // written, but never committed
    {
        SaveFile file;

        CHECK(file.open(path));
        CHECK(file.write("changed", 7));
        CHECK(file_exists(other.c_str()));
        CHECK(read_file(path) == "original");
    }

    CHECK(read_file(path) == "original");
    CHECK(!file_exists(other.c_str()));

    // a read-only file can't be replaced; the error is the one to report
    CHECK(SetFileAttributes(path, FILE_ATTRIBUTE_READONLY));
    CHECK(!save_bytes(path, "changed"));
    CHECK(GetLastError() == ERROR_ACCESS_DENIED);
    CHECK(read_file(path) == "original");
    CHECK(!file_exists(other.c_str()));

    // nor can it save where there's no directory to write to
    other = path;
    other += TEXT("\\missing.txt");
    CHECK(!save_bytes(other.c_str(), "changed"));

    SetFileAttributes(path, FILE_ATTRIBUTE_NORMAL);
    DeleteFile(path);
}

// What render passes on, joined, and how many blocks it came in
std::string render_blocks(const sequence &seq, size_w index, size_w length, size_t *blocks)
{
    std::string text;

    *blocks = 0;

    seq.render(index, [&](const seqchar *block, size_t blocklen)
    {
        text.append(reinterpret_cast<const char *>(block), blocklen);
        (*blocks)++;
        return true;
    }, length);

    return text;
}

void sequence_render_passes_a_block_per_span()
{
    sequence seq;
    size_t blocks;
    size_t calls = 0;

    // "ab", "XY" and "cdef" in three spans
    init(seq, "abcdef");
    CHECK(insert_bytes(seq, 2, "XY"));

    // from inside the first span, from a span's start, and from inside the last
    CHECK(render_blocks(seq, 1, 7, &blocks) == "bXYcdef");
    CHECK(blocks == 3);
    CHECK(render_blocks(seq, 2, 6, &blocks) == "XYcdef");
    CHECK(blocks == 2);
    CHECK(render_blocks(seq, 5, 3, &blocks) == "def");
    CHECK(blocks == 1);

    // only as far as the end, and nothing from there
    CHECK(render_blocks(seq, 6, 100, &blocks) == "ef");
    CHECK(render_blocks(seq, seq.size(), 1, &blocks) == "");
    CHECK(blocks == 0);

    // stopped at the second block, it has rendered only the first
    CHECK(seq.render(0, [&calls](const seqchar *, size_t) { return ++calls < 2; }, seq.size()) == 2);
    CHECK(calls == 2);
}

void textdocument_failed_save_stays_modified()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TCHAR x[] = TEXT("X");

    CHECK(write_textdocument_file("ab", DOC_ASCII, path));
    CHECK(doc.init(path));
    CHECK(doc_insert(doc, 0, x, 1) == 1);
    CHECK(SetFileAttributes(path, FILE_ATTRIBUTE_READONLY));

    CHECK(!doc.save(path, doc.getformat(), false, 0));
    CHECK(doc.modified());
    CHECK(read_file(path) == "ab");

    SetFileAttributes(path, FILE_ATTRIBUTE_NORMAL);
    doc.clear();
    DeleteFile(path);
}

void textdocument_save_over_its_own_file_keeps_text_and_undo()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    std::string original;
    std::string edited;
    TCHAR head[] = TEXT("HEAD");
    TCHAR tail[] = TEXT("TAIL");

    // more blocks than the document keeps in memory, so after the save most of
    // the text has to be read again - from the file that was replaced
    for(size_w i = 0; i < 6 * MEM_BLOCK_SIZE + 123; i++)
        original += (i % 61 == 60) ? '\n' : static_cast<char>('a' + i % 23);

    CHECK(write_temp_file(original.data(), original.size(), path));
    CHECK(doc.init(path));

    // edit the start, middle and end
    edited = original;
    CHECK(doc_insert(doc, 0, head, 4) == 4);
    edited.insert(0, "HEAD");
    CHECK(doc_erase(doc, 1000, 1010) == 10);
    edited.erase(1000, 10);
    CHECK(doc_insert(doc, edited.size(), tail, 4) == 4);
    edited += "TAIL";

    CHECK(save_as_is(doc, path));
    CHECK(read_file(path) == edited);
    CHECK(!doc.modified());
    CHECK(read_document(doc) == std::wstring(edited.begin(), edited.end()));

    // undo back to the original text, now only in the replaced file
    CHECK(doc.undo(0));
    CHECK(doc.undo(0));
    CHECK(doc.undo(0));
    CHECK(!doc.can_undo());
    CHECK(doc.modified());
    CHECK(read_document(doc) == std::wstring(original.begin(), original.end()));

    // which saves as well
    CHECK(save_as_is(doc, path));
    CHECK(read_file(path) == original);
    CHECK(!doc.modified());

    CHECK(doc.redo(0));
    CHECK(doc.modified());

    doc.clear();
    DeleteFile(path);
}

void textdocument_save_keeps_the_byte_order_mark()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TCHAR x[] = TEXT("X");
    const char expected[] = { '\xff', '\xfe', 'X', 0, 'a', 0, 'b', 0 };

    CHECK(write_textdocument_file("ab", DOC_UTF16LE_BOM, path));
    CHECK(doc.init(path));
    CHECK(doc_insert(doc, 0, x, 1) == 2);

    CHECK(doc.save(path, doc.getformat(), true, 0));
    CHECK(read_file(path) == std::string(expected, sizeof(expected)));

    // a document can write UTF-16 without one, though Neatpad doesn't offer it
    CHECK(doc.save(path, doc.getformat(), false, 0));
    CHECK(read_file(path) == std::string(expected + 2, sizeof(expected) - 2));

    doc.clear();
    DeleteFile(path);
}

void textdocument_save_with_or_without_the_byte_order_mark()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TCHAR x[] = TEXT("X");
    const std::string bom = "\xef\xbb\xbf";

    CHECK(write_textdocument_file("ab", DOC_UTF8_BOM, path));
    CHECK(doc.init(path));
    CHECK(doc.has_bom());
    CHECK(doc_insert(doc, 0, x, 1) == 1);

    CHECK(doc.save(path, doc.getformat(), false, 0));
    CHECK(read_file(path) == "Xab");
    CHECK(!doc.has_bom());
    CHECK(read_document(doc) == L"Xab");

    // and back again: the mark comes from the encoding, not the text
    CHECK(doc.save(path, doc.getformat(), true, 0));
    CHECK(read_file(path) == bom + "Xab");
    CHECK(doc.has_bom());

    doc.clear();
    CHECK(!doc.has_bom());
    DeleteFile(path);

    // ASCII has no byte-order mark to write
    CHECK(write_textdocument_file("ab", DOC_ASCII, path));
    CHECK(doc.init(path, NCP_ASCII));
    CHECK(!doc.has_bom());
    CHECK(doc.save(path, doc.getformat(), true, 0));
    CHECK(read_file(path) == "ab");
    CHECK(!doc.has_bom());

    doc.clear();
    DeleteFile(path);
}

// Text in UTF-16 big-endian, as bytes
std::string utf16be_bytes(const char *text, bool bom)
{
    std::string bytes;

    if(bom)
        bytes += "\xfe\xff";

    for(; *text; text++)
    {
        bytes += '\0';
        bytes += *text;
    }

    return bytes;
}

void textdocument_save_converts_the_encoding()
{
    TextDocument doc;
    TextDocument converted;
    TCHAR path[MAX_PATH];
    TCHAR newpath[MAX_PATH];
    TCHAR x[] = TEXT("X");

    // "a", a line break, e with an acute accent, and a grinning face (a surrogate pair)
    const ULONG units[] = { 0xfeff, 'a', '\r', '\n', 0xe9, 0xd83d, 0xde00 };
    const std::string utf8 = "Xa\r\n\xc3\xa9\xf0\x9f\x98\x80";
    const char utf32be[] = { 0, 0, '\xfe', '\xff', 0, 0, 0, 'X', 0, 0, 0, 'a', 0, 0, 0, '\r', 0, 0, 0, '\n',
                             0, 0, 0, '\xe9', 0, 1, '\xf6', 0 };
    std::wstring text(1, 'X');

    for(size_t i = 1; i < sizeof(units) / sizeof(units[0]); i++)
        text += static_cast<wchar_t>(units[i]);

    CHECK(write_units_file(units, sizeof(units) / sizeof(units[0]), 2, false, path));
    CHECK(doc.init(path));
    CHECK(doc_insert(doc, 0, x, 1) == 2);
    CHECK(temp_name(newpath));

    CHECK(doc.save(newpath, NCP_UTF8, true, 0));
    CHECK(read_file(newpath) == "\xef\xbb\xbf" + utf8);
    CHECK(doc.save(newpath, NCP_UTF8, false, 0));
    CHECK(read_file(newpath) == utf8);
    CHECK(doc.save(newpath, NCP_UTF32BE, true, 0));
    CHECK(read_file(newpath) == std::string(utf32be, sizeof(utf32be)));

    // the document is left as it was, edit and all
    CHECK(doc.getformat() == NCP_UTF16);
    CHECK(doc.has_bom());
    CHECK(doc.modified());
    CHECK(doc.can_undo());
    CHECK(read_document(doc) == text);

    // and the file opens again in its new encoding
    CHECK(converted.init(newpath));
    CHECK(converted.getformat() == NCP_UTF32BE);
    CHECK(read_document(converted) == text);

    // without a byte-order mark too
    CHECK(doc.save(newpath, NCP_UTF8, false, 0));
    CHECK(converted.init(newpath));
    CHECK(converted.getformat() == NCP_UTF8);
    CHECK(!converted.has_bom());
    CHECK(read_document(converted) == text);

    // an encoding that doesn't exist is an error
    CHECK(!doc.save(newpath, static_cast<TEXT_ENCODING>(99), false, 0));

    converted.clear();
    doc.clear();
    DeleteFile(path);
    DeleteFile(newpath);
}

void textdocument_save_converts_line_breaks()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TCHAR newpath[MAX_PATH];

    CHECK(write_textdocument_file("a\r\nb\nc\rd\r\n\r\ne", DOC_ASCII, path));
    CHECK(doc.init(path));
    CHECK(temp_name(newpath));

    CHECK(doc.save(newpath, NCP_ASCII, false, TXL_LF));
    CHECK(read_file(newpath) == "a\nb\nc\nd\n\ne");
    CHECK(doc.save(newpath, NCP_ASCII, false, TXL_CRLF));
    CHECK(read_file(newpath) == "a\r\nb\r\nc\r\nd\r\n\r\ne");
    CHECK(doc.save(newpath, NCP_ASCII, false, TXL_CR));
    CHECK(read_file(newpath) == "a\rb\rc\rd\r\re");

    // and the encoding at the same time
    CHECK(doc.save(newpath, NCP_UTF16BE, true, TXL_LF));
    CHECK(read_file(newpath) == utf16be_bytes("a\nb\nc\nd\n\ne", true));

    doc.clear();
    DeleteFile(path);
    DeleteFile(newpath);
}

void textdocument_save_keeps_a_crlf_split_between_reads_as_one()
{
    // the conversion reads 0x4000 UTF-16 units at a time: put the CR and LF of
    // a CRLF either side of that, and either side of that
    for(size_t before = 0x4000 - 3; before <= 0x4000 + 1; before++)
    {
        TextDocument doc;
        TCHAR path[MAX_PATH];
        std::string text(before, 'x');

        text += "\r\ny";

        CHECK(write_temp_file(text.data(), text.size(), path));
        CHECK(doc.init(path));
        CHECK(doc.save(path, NCP_ASCII, false, TXL_LF));
        CHECK(read_file(path) == std::string(before, 'x') + "\ny");

        doc.clear();
        DeleteFile(path);
    }
}

void textdocument_init_in_a_given_encoding()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    const char ansi[] = "caf\xe9";

    CHECK(write_temp_file(ansi, 4, path));

    // not valid UTF-8, so it opens as ASCII...
    CHECK(doc.init(path));
    CHECK(doc.getformat() == NCP_ASCII);

    // ...unless told otherwise, when its last byte isn't a character
    CHECK(doc.init(path, NCP_UTF8));
    CHECK(doc.getformat() == NCP_UTF8);
    CHECK(!doc.has_bom());
    CHECK(read_document(doc) == std::wstring(L"caf") + static_cast<wchar_t>(0xfffd));

    CHECK(!doc.init(path, static_cast<TEXT_ENCODING>(99)));

    doc.clear();
    DeleteFile(path);

    // the encoding's own byte-order mark is skipped, and another's is text
    CHECK(write_textdocument_file("ab", DOC_UTF8_BOM, path));
    CHECK(doc.init(path, NCP_UTF8));
    CHECK(doc.has_bom());
    CHECK(read_document(doc) == L"ab");

    CHECK(doc.init(path, NCP_ASCII));
    CHECK(!doc.has_bom());
    CHECK(doc.byte_length() == 5);

    doc.clear();
    DeleteFile(path);

    // UTF-16 starting with U+0000 looks like UTF-32's byte-order mark
    const ULONG units[] = { 0xfeff, 0, 'a' };

    CHECK(write_units_file(units, sizeof(units) / sizeof(units[0]), 2, false, path));
    CHECK(doc.init(path));
    CHECK(doc.getformat() == NCP_UTF32);

    CHECK(doc.init(path, NCP_UTF16));
    CHECK(doc.has_bom());
    CHECK(doc.byte_length() == 4);
    CHECK(read_document(doc) == std::wstring(1, L'\0') + L"a");

    doc.clear();
    DeleteFile(path);
}

void unicode_utf8_is_valid_only_without_errors()
{
    const UTF8 ascii[]    = { 'a', 'b', 'c' };
    const UTF8 accents[]  = { 'c', 'a', 'f', 0xc3, 0xa9, 0xf0, 0x9f, 0x98, 0x80 };  // cafe, a grinning face
    const UTF8 fffd[]     = { 'a', 0xef, 0xbf, 0xbd };  // a replacement character, as text
    const UTF8 ansi[]     = { 'c', 'a', 'f', 0xe9 };  // cafe in Windows-1252
    const UTF8 stray[]    = { 'a', 0x80, 'b' };  // a trailing byte on its own
    const UTF8 overlong[] = { 0xc0, 0x80 };  // NUL, longer than it needs to be
    const UTF8 badlead[]  = { 'a', 0xff };
    const UTF8 cut[]      = { 'a', 0xe2, 0x82 };  // a euro sign cut short

    CHECK(utf8_is_valid(ascii, sizeof(ascii)));
    CHECK(utf8_is_valid(accents, sizeof(accents)));
    CHECK(utf8_is_valid(fffd, sizeof(fffd)));
    CHECK(utf8_is_valid(ascii, 0));

    CHECK(!utf8_is_valid(ansi, sizeof(ansi)));
    CHECK(!utf8_is_valid(stray, sizeof(stray)));
    CHECK(!utf8_is_valid(overlong, sizeof(overlong)));
    CHECK(!utf8_is_valid(badlead, sizeof(badlead)));
    CHECK(!utf8_is_valid(cut, sizeof(cut)));

    // unless what cuts it short is left out
    CHECK(utf8_is_valid(cut, utf8_complete_length(cut, sizeof(cut))));
}

// How many UTF-16 units the display's own conversion makes of UTF-8
size_t decoded_units(const UTF8 *text, size_t length)
{
    std::vector<UTF16> out(length * 2 + 1);
    size_t units = out.size();

    utf8_to_utf16(text, length, &out[0], &units);
    return units;
}

void unicode_utf8_count_utf16_counts_lead_bytes()
{
    // what it counts, and what the display shows
    struct { const char *bytes; size_t length; size_t units; size_t shown; } cases[] =
    {
        // valid UTF-8 counts as it shows
        { "",                         0, 0, 0 },
        { "abc",                      3, 3, 3 },
        { "caf\xc3\xa9",              5, 4, 4 },      // a 2-byte character
        { "\xe2\x82\xac",             3, 1, 1 },      // a 3-byte one, the euro sign
        { "\xf0\x9f\x98\x80",         4, 2, 2 },      // a 4-byte one: a surrogate pair
        { "\xef\xbf\xbd",             3, 1, 1 },      // U+FFFD itself
        { "\xf4\x8f\xbf\xbf",         4, 2, 2 },      // U+10FFFF, the last character

        // broken UTF-8 counts its lead bytes, whatever it shows
        { "a\x80" "b",                3, 2, 3 },      // a trailing byte on its own
        { "\xe2\x82",                 2, 1, 1 },      // cut short by the end
        { "\xf0\x9f\x98",             3, 2, 1 },      // a 4-byte one cut short
        { "\xc3" "A",                 2, 2, 2 },      // cut short by the next character
        { "\xc0\x80",                 2, 1, 1 },      // longer than it needs to be
        { "\xf4\x90\x80\x80",         4, 2, 1 },      // past U+10FFFF
        { "\xf8\x88\x80\x80\x80",     5, 1, 1 },      // a 5-byte form
        { "\xfe\xff",                 2, 2, 2 },      // bytes that never lead
    };

    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        const UTF8 *bytes = reinterpret_cast<const UTF8 *>(cases[i].bytes);

        CHECK(utf8_count_utf16(bytes, cases[i].length) == cases[i].units);
        CHECK(decoded_units(bytes, cases[i].length) == cases[i].shown);
    }
}

void unicode_utf8_count_utf16_adds_up_across_joins()
{
    // characters of each length, from each end of their range
    const UTF32 chars[] = { 0x41, 0x7f, 0x80, 0x7ff, 0x800, 0x20ac, 0xfffd, 0x10000, 0x1f600, 0x10ffff };
    unsigned int seed = 1;

    for(int n = 0; n < 2000; n++)
    {
        UTF8 text[64];
        size_t length = 0;
        size_t whole;

        while(length + 4 <= sizeof(text))
        {
            seed = seed * 1103515245 + 12345;
            length += utf32_to_utf8(text + length, sizeof(text) - length, chars[(seed >> 16) % (sizeof(chars) / sizeof(chars[0]))]);
        }

        // valid text counts as it shows
        whole = utf8_count_utf16(text, length);
        CHECK(whole == decoded_units(text, length));

        // and two parts count as the whole, wherever the split cuts a character
        for(size_t split = 0; split <= length; split++)
            CHECK(utf8_count_utf16(text, split) + utf8_count_utf16(text + split, length - split) == whole);
    }
}

size_w utf8_chars(const std::string &text, size_t length)
{
    return utf8_count_utf16(reinterpret_cast<const UTF8 *>(text.data()), length);
}

// a, e-acute, the euro sign, U+1F600 and CRLF repeated: characters of each
// UTF-8 length, 12 bytes at a time, so 64 KB page boundaries cut characters
std::string utf8_text(size_t length)
{
    const char pattern[] = "a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80\r\n";
    std::string text;

    while(text.size() < length)
        text += pattern;

    text.resize(length);
    return text;
}

void sequence_counts_chars_after_random_edits()
{
    std::string original = utf8_text(5 * LINE_PAGE_SIZE + 100);
    std::string text = original;
    sequence seq;
    unsigned int seed = 7;

    CHECK(seq.init(reinterpret_cast<const seqchar *>(text.data()), text.size()));
    seq.set_line_scan_mode(sequence::line_scan_utf8);
    CHECK(seq.indexed_chars() == utf8_chars(text, text.size()));

    for(int n = 0; n < 300; n++)
    {
        size_t at;

        seed = seed * 1103515245 + 12345;
        at = (seed >> 8) % (text.size() + 1);       // any byte, so edits cut characters

        if(n % 3 != 0)
        {
            // a piece of the pattern from any byte, so it may start or end mid-character
            size_t from = (seed >> 4) % 12;
            std::string piece = utf8_text(from + 1 + (seed >> 12) % 40).substr(from);

            CHECK(seq.insert(at, reinterpret_cast<const seqchar *>(piece.data()), piece.size()));
            text.insert(at, piece);
        }
        else if(at < text.size())
        {
            size_t length = min(text.size() - at, static_cast<size_t>(1 + (seed >> 12) % 100));

            CHECK(seq.erase(at, length));
            text.erase(at, length);
        }

        CHECK(seq.linecount_known());
        CHECK(seq.indexed_chars() == utf8_chars(text, text.size()));
    }

    // undo brings back old spans with their counts
    while(seq.canundo())
    {
        CHECK(seq.undo());
        text = render_content(seq);
        CHECK(seq.indexed_chars() == utf8_chars(text, text.size()));
    }

    CHECK(text == original);
}

void sequence_counts_chars_in_a_file_as_it_indexes()
{
    std::string text = utf8_text(2 * 1024 * 1024 + 5);
    sequence seq;
    TCHAR path[MAX_PATH];
    const size_w inside_a_4_byte_char = 16 * LINE_PAGE_SIZE + 3;
    const size_w across_a_page = 3 * LINE_PAGE_SIZE - 5;

    CHECK(write_temp_file(text.data(), text.size(), path));
    CHECK(seq.open(path, true));
    seq.set_line_scan_mode(sequence::line_scan_utf8);

    // edits past what opening indexed, cutting characters at span boundaries
    CHECK(insert_bytes(seq, inside_a_4_byte_char, "x"));
    text.insert(static_cast<size_t>(inside_a_4_byte_char), "x");
    CHECK(seq.erase(across_a_page, 9));
    text.erase(static_cast<size_t>(across_a_page), 9);

    CHECK(seq.indexed_length() < text.size());

    // the indexed part's count is right at every step
    do
        CHECK(seq.indexed_chars() == utf8_chars(text, static_cast<size_t>(seq.indexed_length())));
    while(seq.index_next(LINE_PAGE_SIZE + 3));

    CHECK(seq.linecount_known());
    CHECK(seq.indexed_length() == text.size());
    CHECK(seq.indexed_chars() == utf8_chars(text, text.size()));

    seq.clear();
    DeleteFile(path);
}

void sequence_counts_chars_in_each_scan_mode()
{
    // a then U+1F600, a surrogate pair in UTF-16: three units, but five bytes as bytes
    struct { sequence::line_scan_mode mode; const char *bytes; size_t length; size_w chars; size_w first_unit; } cases[] =
    {
        { sequence::line_scan_bytes,   "a\xf0\x9f\x98\x80",               5, 5, 1 },
        { sequence::line_scan_utf8,    "a\xf0\x9f\x98\x80",               5, 3, 1 },
        { sequence::line_scan_utf16le, "a\0\x3d\xd8\x00\xde",             6, 3, 2 },
        { sequence::line_scan_utf16be, "\0a\xd8\x3d\xde\x00",             6, 3, 2 },
        { sequence::line_scan_utf32le, "a\0\0\0\x00\xf6\x01\x00",         8, 3, 4 },
        { sequence::line_scan_utf32be, "\0\0\0a\x00\x01\xf6\x00",         8, 3, 4 },
    };

    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        const seqchar *bytes = reinterpret_cast<const seqchar *>(cases[i].bytes);
        sequence seq;

        CHECK(seq.init(bytes, cases[i].length));
        seq.set_line_scan_mode(cases[i].mode);
        CHECK(seq.indexed_chars() == cases[i].chars);

        // a copy inserted after the first unit adds its count
        CHECK(seq.insert(cases[i].first_unit, bytes, cases[i].length));
        CHECK(seq.indexed_chars() == 2 * cases[i].chars);

        CHECK(seq.undo());
        CHECK(seq.indexed_chars() == cases[i].chars);
    }

    // changing the mode counts again
    {
        sequence seq;

        init(seq, "a\xf0\x9f\x98\x80");
        CHECK(seq.indexed_chars() == 5);

        seq.set_line_scan_mode(sequence::line_scan_utf8);
        CHECK(seq.indexed_chars() == 3);

        seq.set_line_scan_mode(sequence::line_scan_bytes);
        CHECK(seq.indexed_chars() == 5);
    }

    // to a mode whose units are too big for a span, leaving it none
    {
        sequence seq;

        init(seq, "a");
        CHECK(seq.indexed_chars() == 1);

        seq.set_line_scan_mode(sequence::line_scan_utf16le);
        CHECK(seq.indexed_chars() == 0);
    }
}

void textdocument_utf8_counts_chars()
{
    // a, e-acute and U+1F600: four units, and one more for a byte-order mark
    const char plain[] = "a\xc3\xa9\xf0\x9f\x98\x80";
    const char bom[] = "\xef\xbb\xbf" "a\xc3\xa9\xf0\x9f\x98\x80";
    struct { const char *bytes; size_t length; size_w chars; } cases[] =
    {
        { plain, sizeof(plain) - 1, 4 },
        { bom,   sizeof(bom) - 1,   5 },
    };

    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        TextDocument doc;
        TCHAR path[MAX_PATH];

        CHECK(write_temp_file(cases[i].bytes, cases[i].length, path));
        CHECK(doc.init(path));
        CHECK(TextDocumentLineIndexProbe::seq(doc).indexed_chars() == cases[i].chars);

        doc.clear();
        DeleteFile(path);
    }
}

// The characters in the first length bytes of text, counted apart from the sequence
size_w expected_chars(const std::string &text, size_t length, sequence::line_scan_mode mode)
{
    const UTF8 *bytes = reinterpret_cast<const UTF8 *>(text.data());
    std::vector<UTF16> out(length + 1);
    size_t units = out.size();

    switch(mode)
    {
    case sequence::line_scan_utf8:
        return utf8_count_utf16(bytes, length);

    case sequence::line_scan_utf16le:
    case sequence::line_scan_utf16be:
        return length / 2;

    case sequence::line_scan_utf32le:
        utf32le_to_utf16(bytes, length, &out[0], &units);
        return units;

    case sequence::line_scan_utf32be:
        utf32be_to_utf16(bytes, length, &out[0], &units);
        return units;

    default:
        return length;
    }
}

// Where the character holding unit n starts: the first unit whose count takes
// the count past n, or the end of the text
size_w expected_offset(const std::string &text, size_w n, sequence::line_scan_mode mode, size_t unit)
{
    for(size_t offset = 0; offset + unit <= text.size(); offset += unit)
    {
        if(expected_chars(text, offset + unit, mode) > n)
            return offset;
    }

    return text.size();
}

// Both lookups at every offset and every unit of the sequence's text
void expect_char_lookups(const sequence &seq, sequence::line_scan_mode mode, size_t unit)
{
    std::string text = render_content(seq);
    size_w total = expected_chars(text, text.size(), mode);
    size_w chars;
    size_w offset;

    CHECK(seq.indexed_chars() == total);

    for(size_t at = 0; at <= text.size(); at++)
    {
        CHECK(seq.chars_from_offset(at, &chars));
        CHECK(chars == expected_chars(text, at, mode));
    }

    for(size_w n = 0; n <= total; n++)
    {
        CHECK(seq.offset_from_chars(n, &offset));
        CHECK(offset == expected_offset(text, n, mode, unit));
    }

    // nothing past the end
    CHECK(!seq.chars_from_offset(text.size() + 1, &chars));
    CHECK(!seq.offset_from_chars(total + 1, &offset));
}

void sequence_char_lookups_match_a_count_in_each_scan_mode()
{
    // a, U+1F600 (a surrogate pair in UTF-16), CRLF and b
    struct { sequence::line_scan_mode mode; const char *bytes; size_t length; size_t unit; } cases[] =
    {
        { sequence::line_scan_bytes,   "a\xf0\x9f\x98\x80\r\nb",                                   8, 1 },
        { sequence::line_scan_utf8,    "a\xf0\x9f\x98\x80\r\nb",                                   8, 1 },
        { sequence::line_scan_utf16le, "a\0\x3d\xd8\x00\xde\r\0\n\0b\0",                          12, 2 },
        { sequence::line_scan_utf16be, "\0a\xd8\x3d\xde\x00\0\r\0\n\0b",                          12, 2 },
        { sequence::line_scan_utf32le, "a\0\0\0\x00\xf6\x01\x00\r\0\0\0\n\0\0\0b\0\0\0",          20, 4 },
        { sequence::line_scan_utf32be, "\0\0\0a\x00\x01\xf6\x00\0\0\0\r\0\0\0\n\0\0\0b",          20, 4 },
    };

    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        const seqchar *bytes = reinterpret_cast<const seqchar *>(cases[i].bytes);
        size_t unit = cases[i].unit;
        sequence seq;

        CHECK(seq.init(bytes, cases[i].length));
        seq.set_line_scan_mode(cases[i].mode);
        expect_char_lookups(seq, cases[i].mode, unit);

        // copies inserted between units, one inside the other, so spans cut it up
        CHECK(seq.insert(unit, bytes, cases[i].length));
        CHECK(seq.insert(3 * unit, bytes, cases[i].length));
        expect_char_lookups(seq, cases[i].mode, unit);

        // an erase joining pieces of different spans
        CHECK(seq.erase(2 * unit, 3 * unit));
        expect_char_lookups(seq, cases[i].mode, unit);

        // bytes can be inserted inside a UTF-8 character
        if(unit == 1)
        {
            CHECK(insert_bytes(seq, 2, "\xc3\xa9"));
            expect_char_lookups(seq, cases[i].mode, unit);
        }
    }
}

void sequence_char_lookups_cross_pages()
{
    std::string text = utf8_text(3 * LINE_PAGE_SIZE + 50);
    sequence seq;
    size_w chars;
    size_w offset;

    CHECK(seq.init(reinterpret_cast<const seqchar *>(text.data()), text.size()));
    seq.set_line_scan_mode(sequence::line_scan_utf8);

    // an edit just before the first page boundary, inside a character
    CHECK(insert_bytes(seq, LINE_PAGE_SIZE - 2, "x"));
    text.insert(static_cast<size_t>(LINE_PAGE_SIZE - 2), "x");

    for(size_w page = 1; page <= 3; page++)
    {
        for(size_t at = static_cast<size_t>(page * LINE_PAGE_SIZE - 16); at <= page * LINE_PAGE_SIZE + 16; at++)
        {
            unsigned char ch = static_cast<unsigned char>(text[at]);
            size_w n = utf8_chars(text, at);

            CHECK(seq.chars_from_offset(at, &chars));
            CHECK(chars == n);

            // a character's start comes back from its count, and from its
            // second unit if it takes two
            if((ch & 0xC0) != 0x80)
            {
                CHECK(seq.offset_from_chars(n, &offset));
                CHECK(offset == at);
            }

            if((ch & 0xF8) == 0xF0)
            {
                CHECK(seq.offset_from_chars(n + 1, &offset));
                CHECK(offset == at);
            }
        }
    }

    CHECK(seq.offset_from_chars(utf8_chars(text, text.size()), &offset));
    CHECK(offset == text.size());
}

void sequence_char_lookups_wait_for_the_index()
{
    std::string text = utf8_text(2 * 1024 * 1024 + 5);
    sequence seq;
    TCHAR path[MAX_PATH];
    size_w indexed;
    size_w chars;
    size_w offset;

    CHECK(write_temp_file(text.data(), text.size(), path));
    CHECK(seq.open(path, true));
    seq.set_line_scan_mode(sequence::line_scan_utf8);

    indexed = seq.indexed_length();
    CHECK(indexed < text.size());

    // the indexed part answers
    CHECK(seq.chars_from_offset(indexed, &chars));
    CHECK(chars == utf8_chars(text, static_cast<size_t>(indexed)));
    CHECK(seq.offset_from_chars(chars - 1, &offset));
    CHECK(utf8_chars(text, static_cast<size_t>(offset)) < chars && utf8_chars(text, static_cast<size_t>(offset) + 1) == chars);

    // past it, nothing: not even its end, as more text may follow
    CHECK(!seq.chars_from_offset(indexed + 1, &chars));
    CHECK(!seq.offset_from_chars(seq.indexed_chars(), &offset));

    // once indexed, all of it
    while(seq.index_next(0x100000))
        ;

    CHECK(seq.chars_from_offset(text.size(), &chars));
    CHECK(chars == utf8_chars(text, text.size()));
    CHECK(seq.offset_from_chars(chars, &offset));
    CHECK(offset == text.size());

    CHECK(seq.chars_from_offset(text.size() / 2, &chars));
    CHECK(chars == utf8_chars(text, text.size() / 2));

    seq.clear();
    DeleteFile(path);
}

void textdocument_char_results_wait_for_the_index()
{
    std::string text = utf8_text(static_cast<size_t>(TextDocumentLineIndexProbe::open_index_length()) + 2 * 1024 * 1024);
    size_t near_end = text.size() - 10;
    TextDocument doc;
    TCHAR path[MAX_PATH];
    size_w chars = 0;

    CHECK(write_temp_file(text.data(), text.size(), path));
    CHECK(doc.init(path));
    CHECK(doc.getformat() == NCP_UTF8);

    // opening indexes the first 4 MB: positions there answer, the rest wait
    CHECK(doc.charcount(&chars) == TDR_PENDING_INDEX);
    CHECK(doc.charoffset_from_pos(TextPos(1000), &chars) == TDR_OK);
    CHECK(chars == utf8_chars(text, 1000));
    CHECK(doc.charoffset_from_pos(TextPos(near_end), &chars) == TDR_PENDING_INDEX);

    while(doc.index_next(0x100000))
        ;

    CHECK(doc.charcount(&chars) == TDR_OK);
    CHECK(chars == utf8_chars(text, text.size()));
    CHECK(doc.charoffset_from_pos(TextPos(near_end), &chars) == TDR_OK);
    CHECK(chars == utf8_chars(text, near_end));

    // a position past the end is no position at all
    CHECK(doc.charoffset_from_pos(TextPos(text.size() + 1), &chars) == TDR_FAILED);

    doc.clear();
    DeleteFile(path);
}

// Text as a file in an encoding: UTF-8 without a byte-order mark, UTF-16 or
// UTF-32 with one
std::string encode_text(const wchar_t *text, TEXT_ENCODING encoding)
{
    size_t length = wcslen(text);
    size_t out = length * 4;
    std::string bytes;

    switch(encoding)
    {
    case NCP_UTF16:
        bytes.assign("\xff\xfe", 2);
        bytes.append(reinterpret_cast<const char *>(text), length * sizeof(wchar_t));
        break;

    case NCP_UTF32:
        bytes.assign("\xff\xfe\0\0", 4);
        bytes.resize(4 + out);
        utf16_to_utf32le(reinterpret_cast<const UTF16 *>(text), length, reinterpret_cast<UTF8 *>(&bytes[4]), &out);
        bytes.resize(4 + out);
        break;

    default:
        bytes.resize(out);
        bytes.resize(WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), &bytes[0], static_cast<int>(out), 0, 0));
        break;
    }

    return bytes;
}

// The last index notification a TextView sent its parent, and how many times
// it has said its text changed
TVNINDEXINFO g_index_notified;
int          g_changes_notified;

LRESULT CALLBACK HiddenParentProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if(msg == WM_NOTIFY && reinterpret_cast<NMHDR *>(lParam)->code == TVN_INDEX_CHANGE)
        g_index_notified = *reinterpret_cast<TVNINDEXINFO *>(lParam);

    if(msg == WM_NOTIFY && reinterpret_cast<NMHDR *>(lParam)->code == TVN_CHANGED)
        g_changes_notified++;

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

// All of a document's text, as UTF-16
std::wstring document_text(TextDocument &doc)
{
    std::wstring text;
    TextReader   reader = doc.text_from_range(TextPos(0), TextPos(doc.byte_length()));
    wchar_t      buf[256];
    uint32_t     len;

    while((len = reader.read(buf, 256)) != 0)
        text.append(buf, len);

    return text;
}

void textdocument_replace_linebreaks_makes_them_one_kind()
{
    // CRLF, LF, CR, and an LF then a CR, which is two line breaks
    const wchar_t mixed[] = L"a\r\nb\nc\rd\n\re";
    const TEXT_ENCODING encodings[] = { NCP_UTF8, NCP_UTF16, NCP_UTF32 };
    struct { uint32_t linebreak; const wchar_t *expected; } cases[] =
    {
        { TXL_CRLF, L"a\r\nb\r\nc\r\nd\r\n\r\ne" },
        { TXL_LF,   L"a\nb\nc\nd\n\ne" },
        { TXL_CR,   L"a\rb\rc\rd\r\re" },
    };

    for(size_t e = 0; e < sizeof(encodings) / sizeof(encodings[0]); e++)
    {
        for(size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++)
        {
            std::string  bytes = encode_text(mixed, encodings[e]);
            TextDocument doc;
            TextChange   change;
            TCHAR        path[MAX_PATH];

            CHECK(write_temp_file(bytes.data(), bytes.size(), path));
            CHECK(doc.init(path, encodings[e]));

            doc.replace_linebreaks(TextPos(0), TextPos(doc.byte_length()), cases[c].linebreak, &change);
            CHECK(document_text(doc) == cases[c].expected);

            // one undo step brings the mix back
            CHECK(doc.undo(&change));
            CHECK(document_text(doc) == mixed);

            doc.clear();
            DeleteFile(path);
        }
    }

    // longer text goes into the document in pieces, and still undoes in one step
    {
        std::string  text;
        TextDocument doc;
        TextChange   change;
        TCHAR        path[MAX_PATH];

        for(int i = 0; i < 1000; i++)
            text += "line\r\n";

        CHECK(write_temp_file(text.data(), text.size(), path));
        CHECK(doc.init(path, NCP_UTF8));

        doc.replace_linebreaks(TextPos(0), TextPos(doc.byte_length()), TXL_LF, &change);
        CHECK(doc.byte_length() == 5000);

        CHECK(doc.undo(&change));
        CHECK(doc.byte_length() == 6000);

        doc.clear();
        DeleteFile(path);
    }

    // part of the text converts just the line breaks inside it: "b\nc\r" here
    {
        TextDocument doc;
        TCHAR        path[MAX_PATH];

        CHECK(write_temp_file("a\r\nb\nc\rd", 8, path));
        CHECK(doc.init(path, NCP_UTF8));

        doc.replace_linebreaks(TextPos(3), TextPos(7), TXL_LF, 0);
        CHECK(document_text(doc) == L"a\r\nb\nc\nd");

        doc.clear();
        DeleteFile(path);
    }
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

void textview_enter_types_the_documents_line_break()
{
    struct { const wchar_t *text; const wchar_t *typed; } cases[] =
    {
        { L"a\r\nb", L"a\r\nb\r\n" },
        { L"a\nb",   L"a\nb\n" },
        { L"a\rb",   L"a\rb\r" },
    };

    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        EditPair pair;
        size_t   length = wcslen(cases[i].text);
        int      changes;

        CHECK(open_edit_pair(&pair, cases[i].text, NCP_UTF8));

        // Enter at the end of the text, then undone in one step
        SendMessage(pair.view, EM_SETSEL, length, length);
        changes = g_changes_notified;

        SendMessage(pair.view, WM_CHAR, '\r', 0);
        CHECK(window_text(pair.view) == cases[i].typed);
        CHECK(g_changes_notified > changes);

        SendMessage(pair.view, EM_UNDO, 0, 0);
        CHECK(window_text(pair.view) == cases[i].text);

        close_edit_pair(&pair);
    }
}

void textdocument_detects_utf8_without_a_bom()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    std::string text(MEM_BLOCK_SIZE - 1, 'a');

    // valid UTF-8
    CHECK(write_temp_file("caf\xc3\xa9", 5, path));
    CHECK(doc.init(path));
    CHECK(doc.getformat() == NCP_UTF8);
    CHECK(!doc.has_bom());
    CHECK(read_document(doc) == std::wstring(L"caf") + static_cast<wchar_t>(0xe9));
    doc.clear();
    DeleteFile(path);

    // plain ASCII is valid UTF-8, and so is nothing at all
    CHECK(write_temp_file("plain", 5, path));
    CHECK(doc.init(path));
    CHECK(doc.getformat() == NCP_UTF8);
    doc.clear();
    DeleteFile(path);

    CHECK(write_temp_file("", 0, path));
    CHECK(doc.init(path));
    CHECK(doc.getformat() == NCP_UTF8);
    doc.clear();
    DeleteFile(path);

    // anything else is ANSI
    CHECK(write_temp_file("caf\xe9 au lait", 12, path));
    CHECK(doc.init(path));
    CHECK(doc.getformat() == NCP_ASCII);
    doc.clear();
    DeleteFile(path);

    // only the first block is checked: a character it cuts in two is fine,
    // and what comes after it isn't looked at
    text += "\xc3\xa9";
    text += "caf\xe9";

    CHECK(write_temp_file(text.data(), text.size(), path));
    CHECK(doc.init(path));
    CHECK(doc.getformat() == NCP_UTF8);
    doc.clear();
    DeleteFile(path);
}

void textdocument_new_document_is_utf8()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TCHAR newpath[MAX_PATH];
    const TCHAR text[] = { 'a', 0xe9, '\n', 'b' };

    // after a UTF-16 file, none of which should be left behind
    CHECK(write_textdocument_file("x\ny", DOC_UTF16LE_BOM, path));
    CHECK(doc.init(path));
    CHECK(doc.clear());

    CHECK(doc.getformat() == NCP_UTF8);
    CHECK(!doc.has_bom());
    CHECK(doc_insert(doc, 0, text, 4) == 5);
    CHECK(doc.linecount() == 2);

    CHECK(temp_name(newpath));
    CHECK(save_as_is(doc, newpath));
    CHECK(read_file(newpath) == "a\xc3\xa9\nb");

    DeleteFile(path);
    DeleteFile(newpath);
}

// Numbered lines, each 14 bytes with its line feed
std::string numbered_lines(size_t line_count)
{
    std::string text;
    char line[32];

    for(size_t i = 0; i < line_count; i++)
    {
        snprintf(line, sizeof(line), "line %08u\n", static_cast<unsigned>(i));
        text += line;
    }

    return text;
}

void sequence_index_next_indexes_to_the_end()
{
    sequence seq;
    TCHAR path[MAX_PATH];
    const size_t lines = 600000;
    std::string text = numbered_lines(lines);
    size_w before;
    int steps = 0;

    CHECK(write_temp_file(text.data(), text.size(), path));
    CHECK(seq.open(path, true));

    // opening indexes only the start
    CHECK(!seq.linecount_known());
    before = seq.indexed_length();
    CHECK(before > 0 && before < text.size());

    // each step goes further than the last, to the end
    while(seq.index_next(0x100000))
    {
        CHECK(seq.indexed_length() > before);
        before = seq.indexed_length();
        steps++;
    }

    CHECK(steps > 4);
    CHECK(seq.linecount_known());
    CHECK(seq.indexed_length() == text.size());
    CHECK(seq.linecount() == lines + 1);
    CHECK(!seq.index_next(0x100000));

    seq.clear();
    DeleteFile(path);
}

void textdocument_open_indexes_the_first_4mb()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    const size_t lines = 600000;
    std::string text = numbered_lines(lines);
    TextCoord end;
    bool exact = true;

    CHECK(write_temp_file(text.data(), text.size(), path));
    CHECK(doc.init(path));
    CHECK(doc.indexed_length() == 0x400000);

    // the end's line number isn't known until indexing gets there
    CHECK(doc.coord_from_document_end(&end));
    doc_lineno(doc, end, &exact);
    CHECK(!exact);

    while(doc.index_next(0x100000))
        ;

    CHECK(doc.indexed_length() == doc.byte_length());
    CHECK(doc.linecount() == lines + 1);
    CHECK(doc_lineno(doc, end, &exact) == lines);
    CHECK(exact);

    doc.clear();
    DeleteFile(path);

    // a smaller file is indexed whole when it opens
    text = numbered_lines(200000);

    CHECK(write_temp_file(text.data(), text.size(), path));
    CHECK(doc.init(path));
    CHECK(doc.indexed_length() == doc.byte_length());
    CHECK(doc.linecount() == 200000 + 1);
    CHECK(!doc.index_next(0x100000));

    doc.clear();
    DeleteFile(path);

    // the byte-order mark isn't part of what's indexed
    CHECK(write_textdocument_file("a\nb", DOC_UTF8_BOM, path));
    CHECK(doc.init(path));
    CHECK(doc.indexed_length() == doc.byte_length());

    doc.clear();
    DeleteFile(path);

    // opened as another encoding, it's indexed again as that
    text = numbered_lines(400000);
    std::string utf16;

    for(size_t i = 0; i < text.size(); i++)
    {
        utf16 += text[i];
        utf16 += '\0';
    }

    CHECK(write_temp_file(utf16.data(), utf16.size(), path));
    CHECK(doc.init(path, NCP_UTF16));
    CHECK(doc.indexed_length() == 0x400000);

    while(doc.index_next(0x100000))
        ;

    CHECK(doc.linecount() == 400000 + 1);

    doc.clear();
    DeleteFile(path);
}

void textdocument_coord_from_lineno_finds_line_starts()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TextCoord line;

    CHECK(write_textdocument_file("ab\r\ncd\nef", DOC_UTF8_BOM, path));
    CHECK(doc.init(path));

    // each line's start, after the byte-order mark
    CHECK(doc.coord_from_lineno(0, &line) == TDR_OK);
    CHECK(line.byte_offset == 0);
    CHECK(doc.coord_from_lineno(1, &line) == TDR_OK);
    CHECK(line.byte_offset == 4);
    CHECK(doc.coord_from_lineno(2, &line) == TDR_OK);
    CHECK(line.byte_offset == 7);

    // and not a line it doesn't have
    CHECK(doc.coord_from_lineno(3, &line) == TDR_FAILED);
    CHECK(doc.coord_from_lineno(0, 0) == TDR_FAILED);

    // an empty document has one, empty, line
    doc.clear();
    CHECK(doc.coord_from_lineno(0, &line) == TDR_OK);
    CHECK(line.byte_offset == 0);
    CHECK(doc.coord_from_lineno(1, &line) == TDR_FAILED);

    DeleteFile(path);
}

void textdocument_lookups_are_pending_past_the_index()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    const size_t lines = 1000000;
    const size_w near_line = 0x400000 / 14 + 100;
    std::string text = numbered_lines(lines);
    TextCoord line;
    TextCoord end;
    size_w lineno = 0;

    CHECK(write_temp_file(text.data(), text.size(), path));
    CHECK(doc.init(path));

    // the end is far past the 4 MB that opening indexes: an estimate, pending
    CHECK(doc.coord_from_document_end(&end));
    CHECK(doc.lineno_from_pos(end, &lineno) == TDR_PENDING_INDEX);
    CHECK(lineno > 0);
    CHECK(doc.coord_from_lineno(lines - 1, &line) == TDR_PENDING_INDEX);

    // a line just past the index is indexed there and then
    CHECK(doc.coord_from_lineno(near_line, &line) == TDR_OK);
    CHECK(line.byte_offset == near_line * 14);

    // indexed, everything is exact, and a line past the end doesn't exist
    while(doc.index_next(0x100000))
        ;

    CHECK(doc.lineno_from_pos(end, &lineno) == TDR_OK);
    CHECK(lineno == lines);
    CHECK(doc.coord_from_lineno(lines - 1, &line) == TDR_OK);
    CHECK(line.byte_offset == (lines - 1) * 14);
    CHECK(doc.coord_from_lineno(lines + 1, &line) == TDR_FAILED);

    doc.clear();
    DeleteFile(path);
}

void textdocument_ascii_turns_what_it_cant_hold_into_question_marks()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TCHAR newpath[MAX_PATH];
    const ULONG units[] = { 0xfeff, 'a', 0xd83d, 0xde00, 'b' };
    const TCHAR face[] = { 0xd83d, 0xde00 };

    // a UTF-8 code page can hold everything
    if(GetACP() == CP_UTF8)
        return;

    // converted: a '?' for each half of the pair, and no byte-order mark
    CHECK(write_units_file(units, sizeof(units) / sizeof(units[0]), 2, false, path));
    CHECK(doc.init(path));
    CHECK(temp_name(newpath));
    CHECK(doc.save(newpath, NCP_ASCII, true, 0));
    CHECK(read_file(newpath) == "a??b");
    doc.clear();
    DeleteFile(path);

    // typed into an ASCII document, the same
    CHECK(write_textdocument_file("ab", DOC_ASCII, path));
    CHECK(doc.init(path, NCP_ASCII));
    CHECK(doc_insert(doc, 1, face, 2) == 2);
    CHECK(read_document(doc) == L"a??b");

    doc.clear();
    DeleteFile(path);
    DeleteFile(newpath);
}

// Neatpad keeps a file's window position in a stream of the file
void textdocument_save_keeps_the_files_other_streams()
{
    TextDocument doc;
    TCHAR path[MAX_PATH];
    TCHAR x[] = TEXT("X");
    std::wstring stream;
    HANDLE hFile;
    DWORD written;

    CHECK(write_textdocument_file("abc", DOC_ASCII, path));
    stream = path;
    stream += TEXT(":Neatpad.Settings");

    hFile = CreateFile(stream.c_str(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    CHECK(hFile != INVALID_HANDLE_VALUE);
    CHECK(WriteFile(hFile, "settings", 8, &written, 0));
    CloseHandle(hFile);

    CHECK(doc.init(path));
    CHECK(doc_insert(doc, 0, x, 1) == 1);
    CHECK(save_as_is(doc, path));

    CHECK(read_file(path) == "Xabc");
    CHECK(read_file(stream.c_str()) == "settings");

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
    const size_w open_index = TextDocumentLineIndexProbe::open_index_length();
    const size_t line_count = static_cast<size_t>((open_index + MEM_BLOCK_SIZE * 4) / line_length);
    const size_t file_length = sizeof(bom) + line_length * line_count;
    const ULONG target_line = static_cast<ULONG>((open_index + MEM_BLOCK_SIZE * 2) / line_length);
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

    CHECK(doc.coord_from_pos(TextPos(line_begin + 20), &coord));
    CHECK(coord.line_begin.byte_offset == line_begin);
    CHECK(doc_lineno(doc, coord, &exact) == target_line);

    CHECK(doc.coord_from_pos(TextPos(line_begin + 21), &coord));
    CHECK(coord.line_begin.byte_offset == line_begin);
    CHECK(doc_lineno(doc, coord, &exact) == target_line);

    CHECK(doc.previous_line_from_coord(&coord, 1, &prev));
    CHECK(doc_lineno(doc, prev, &exact) == target_line - 1);

    CHECK(doc.next_line_from_coord(&coord, 1, &next));
    CHECK(doc_lineno(doc, next, &exact) == target_line + 1);

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
    { "linebreak_kinds_finds_each_kind", linebreak_kinds_finds_each_kind },
    { "linebreak_kinds_joins_crlf_across_spans_and_pages", linebreak_kinds_joins_crlf_across_spans_and_pages },
    { "linebreak_kinds_follow_edits", linebreak_kinds_follow_edits },
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
    { "lazy_file_linebreak_kinds_grow_with_counting", lazy_file_linebreak_kinds_grow_with_counting },
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
    { "textdocument_coord_uses_byte_offset_for_ascii", textdocument_coord_uses_byte_offset_for_ascii },
    { "textdocument_coord_uses_local_utf8_byte_offset", textdocument_coord_uses_local_utf8_byte_offset },
    { "textdocument_coord_resolves_document_end", textdocument_coord_resolves_document_end },
    { "textdocument_only_cr_lf_end_lines", textdocument_only_cr_lf_end_lines },
    { "textdocument_linebreak_from_coord", textdocument_linebreak_from_coord },
    { "textdocument_linebreaks_seen", textdocument_linebreaks_seen },
    { "textdocument_charoffset_in_each_encoding", textdocument_charoffset_in_each_encoding },
    { "textdocument_coord_moves_by_local_lines", textdocument_coord_moves_by_local_lines },
    { "textdocument_coord_moves_by_local_utf8_lines", textdocument_coord_moves_by_local_utf8_lines },
    { "textdocument_coord_tracks_lazy_eof_after_newline_edits", textdocument_coord_tracks_lazy_eof_after_newline_edits },
    { "textdocument_coord_moves_across_changes", textdocument_coord_moves_across_changes },
    { "textdocument_edits_by_coord", textdocument_edits_by_coord },
    { "textdocument_change_excludes_header", textdocument_change_excludes_header },
    { "textdocument_find_text_searches_raw_sequence", textdocument_find_text_searches_raw_sequence },
    { "textdocument_find_text_matches_ascii_case_insensitive", textdocument_find_text_matches_ascii_case_insensitive },
    { "textdocument_find_text_matches_unicode_case_insensitive_across_encodings", textdocument_find_text_matches_unicode_case_insensitive_across_encodings },
    { "textdocument_find_text_converts_query_to_document_encoding", textdocument_find_text_converts_query_to_document_encoding },
    { "textdocument_find_text_nocase_slides_window", textdocument_find_text_nocase_slides_window },
    { "textdocument_find_text_nocase_backwards_over_utf8", textdocument_find_text_nocase_backwards_over_utf8 },
    { "textdocument_find_text_nocase_matches_supplementary_chars_in_each_encoding", textdocument_find_text_nocase_matches_supplementary_chars_in_each_encoding },
    { "textdocument_find_text_nocase_steps_over_split_surrogate_pair", textdocument_find_text_nocase_steps_over_split_surrogate_pair },
    { "unicode_utf8_prev_steps_back_to_lead_byte", unicode_utf8_prev_steps_back_to_lead_byte },
    { "unicode_utf16_prev_joins_surrogate_pair", unicode_utf16_prev_joins_surrogate_pair },
    { "unicode_utf32_char_reads_both_byte_orders", unicode_utf32_char_reads_both_byte_orders },
    { "unicode_utf8_complete_length_leaves_out_cut_sequence", unicode_utf8_complete_length_leaves_out_cut_sequence },
    { "unicode_utf16_to_utf8_leaves_character_that_does_not_fit", unicode_utf16_to_utf8_leaves_character_that_does_not_fit },
    { "unicode_utf32_streams_convert_both_byte_orders", unicode_utf32_streams_convert_both_byte_orders },
    { "textdocument_utf8_decodes_characters_across_blocks", textdocument_utf8_decodes_characters_across_blocks },
    { "textdocument_utf8_edits_keep_characters_across_buffers", textdocument_utf8_edits_keep_characters_across_buffers },
    { "textdocument_utf32_reads_and_edits_text", textdocument_utf32_reads_and_edits_text },
    { "textdocument_find_text_matchcase_stays_on_code_units", textdocument_find_text_matchcase_stays_on_code_units },
    { "textpos_compares_and_measures", textpos_compares_and_measures },
    { "textdocument_coord_from_line_pos_rounds_back_inside_pair", textdocument_coord_from_line_pos_rounds_back_inside_pair },
    { "textdocument_pos_after_change", textdocument_pos_after_change },
    { "textdocument_find_text_reports_begin_and_end", textdocument_find_text_reports_begin_and_end },
    { "textreader_reads_whole_characters", textreader_reads_whole_characters },
    { "lineiterator_walks_lines", lineiterator_walks_lines },
    { "lineiterator_ends_with_empty_line_after_final_break", lineiterator_ends_with_empty_line_after_final_break },
    { "lineiterator_on_empty_document", lineiterator_on_empty_document },
    { "sequence_modified_follows_undo_history", sequence_modified_follows_undo_history },
    { "sequence_mark_saved_stops_typing_joining_the_saved_record", sequence_mark_saved_stops_typing_joining_the_saved_record },
    { "sequence_modified_treats_group_as_one_edit", sequence_modified_treats_group_as_one_edit },
    { "textdocument_open_file_can_be_replaced_but_not_written", textdocument_open_file_can_be_replaced_but_not_written },
    { "savefile_writes_a_new_file", savefile_writes_a_new_file },
    { "savefile_steps_past_a_file_in_the_way", savefile_steps_past_a_file_in_the_way },
    { "savefile_leaves_the_file_as_it_was_until_committed", savefile_leaves_the_file_as_it_was_until_committed },
    { "sequence_render_passes_a_block_per_span", sequence_render_passes_a_block_per_span },
    { "textdocument_failed_save_stays_modified", textdocument_failed_save_stays_modified },
    { "textdocument_save_over_its_own_file_keeps_text_and_undo", textdocument_save_over_its_own_file_keeps_text_and_undo },
    { "textdocument_save_keeps_the_byte_order_mark", textdocument_save_keeps_the_byte_order_mark },
    { "textdocument_save_with_or_without_the_byte_order_mark", textdocument_save_with_or_without_the_byte_order_mark },
    { "textdocument_save_converts_the_encoding", textdocument_save_converts_the_encoding },
    { "textdocument_save_converts_line_breaks", textdocument_save_converts_line_breaks },
    { "textdocument_save_keeps_a_crlf_split_between_reads_as_one", textdocument_save_keeps_a_crlf_split_between_reads_as_one },
    { "textdocument_init_in_a_given_encoding", textdocument_init_in_a_given_encoding },
    { "unicode_utf8_is_valid_only_without_errors", unicode_utf8_is_valid_only_without_errors },
    { "unicode_utf8_count_utf16_counts_lead_bytes", unicode_utf8_count_utf16_counts_lead_bytes },
    { "unicode_utf8_count_utf16_adds_up_across_joins", unicode_utf8_count_utf16_adds_up_across_joins },
    { "sequence_counts_chars_after_random_edits", sequence_counts_chars_after_random_edits },
    { "sequence_counts_chars_in_a_file_as_it_indexes", sequence_counts_chars_in_a_file_as_it_indexes },
    { "sequence_counts_chars_in_each_scan_mode", sequence_counts_chars_in_each_scan_mode },
    { "textdocument_utf8_counts_chars", textdocument_utf8_counts_chars },
    { "sequence_char_lookups_match_a_count_in_each_scan_mode", sequence_char_lookups_match_a_count_in_each_scan_mode },
    { "sequence_char_lookups_cross_pages", sequence_char_lookups_cross_pages },
    { "sequence_char_lookups_wait_for_the_index", sequence_char_lookups_wait_for_the_index },
    { "textdocument_char_results_wait_for_the_index", textdocument_char_results_wait_for_the_index },
    { "textview_edit_messages_match_an_edit_control", textview_edit_messages_match_an_edit_control },
    { "textview_edit_selection_covers_whole_characters", textview_edit_selection_covers_whole_characters },
    { "textview_edit_positions_round_trip", textview_edit_positions_round_trip },
    { "textview_edit_replacesel_matches_an_edit_control", textview_edit_replacesel_matches_an_edit_control },
    { "textview_edit_scrolling_matches_an_edit_control", textview_edit_scrolling_matches_an_edit_control },
    { "textview_edit_messages_index_as_they_need", textview_edit_messages_index_as_they_need },
    { "textview_goto_line_reports_indexing", textview_goto_line_reports_indexing },
    { "textdocument_replace_linebreaks_makes_them_one_kind", textdocument_replace_linebreaks_makes_them_one_kind },
    { "textview_converts_line_breaks", textview_converts_line_breaks },
    { "textview_enter_types_the_documents_line_break", textview_enter_types_the_documents_line_break },
    { "textdocument_detects_utf8_without_a_bom", textdocument_detects_utf8_without_a_bom },
    { "textdocument_new_document_is_utf8", textdocument_new_document_is_utf8 },
    { "sequence_index_next_indexes_to_the_end", sequence_index_next_indexes_to_the_end },
    { "textdocument_open_indexes_the_first_4mb", textdocument_open_indexes_the_first_4mb },
    { "textdocument_coord_from_lineno_finds_line_starts", textdocument_coord_from_lineno_finds_line_starts },
    { "textdocument_lookups_are_pending_past_the_index", textdocument_lookups_are_pending_past_the_index },
    { "textdocument_ascii_turns_what_it_cant_hold_into_question_marks", textdocument_ascii_turns_what_it_cant_hold_into_question_marks },
    { "textdocument_save_keeps_the_files_other_streams", textdocument_save_keeps_the_files_other_streams },
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
