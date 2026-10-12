//
//	Line index tests: counting line breaks and finding lines, as files are
//	read lazily and as their text is edited, in each line-scan mode
//
#include "tests.h"

namespace
{
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

const test_case tests[] =
{
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
    { "textdocument_only_cr_lf_end_lines", textdocument_only_cr_lf_end_lines },
    { "textdocument_linebreak_from_coord", textdocument_linebreak_from_coord },
    { "textdocument_linebreaks_seen", textdocument_linebreaks_seen },
    { "textdocument_coord_tracks_lazy_eof_after_newline_edits", textdocument_coord_tracks_lazy_eof_after_newline_edits },
    { "sequence_index_next_indexes_to_the_end", sequence_index_next_indexes_to_the_end },
    { "textdocument_open_indexes_the_first_4mb", textdocument_open_indexes_the_first_4mb },
    { "textdocument_coord_from_lineno_finds_line_starts", textdocument_coord_from_lineno_finds_line_starts },
    { "textdocument_lookups_are_pending_past_the_index", textdocument_lookups_are_pending_past_the_index },
    { "textdocument_sequence_index_matches_ascii_after_edits", textdocument_sequence_index_matches_ascii_after_edits },
    { "textdocument_sequence_index_matches_utf16le", textdocument_sequence_index_matches_utf16le },
    { "textdocument_sequence_index_matches_utf16be", textdocument_sequence_index_matches_utf16be },
    { "textdocument_sequence_index_matches_utf8_bom", textdocument_sequence_index_matches_utf8_bom },
    { "textdocument_utf8_lf_lazy_lookup_returns_bounded_line", textdocument_utf8_lf_lazy_lookup_returns_bounded_line },
};
}

const test_group line_tests = { "lines", tests, sizeof(tests) / sizeof(tests[0]) };
