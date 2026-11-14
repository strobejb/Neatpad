//
//	TextDocument tests: encodings, positions, finding text, and opening and
//	saving files
//
#include "tests.h"

namespace
{
// Write code units of unit_size bytes in either byte order. Start the units
// with 0xFEFF to write the matching byte-order mark.
bool write_units_file(const uint32_t *units, size_t count, size_t unit_size, bool big_endian, seqfilechar path[TEST_PATH_MAX])
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

// find_text between document byte offsets, reporting the match as an offset and a length
bool doc_find(TextDocument &doc, const tchar_t *text, uint32_t length, size_w from, size_w to, bool backwards, bool matchcase, size_w *match, size_w *match_length)
{
    TextPos begin;
    TextPos end;

    if(!doc.find_text(text, length, TextPos(from), TextPos(to), backwards, matchcase, &begin, &end))
        return false;

    *match = begin.byte_offset;
    *match_length = end - begin;
    return true;
}

void textdocument_edits_beyond_4gb()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    tchar_t bang[] = TSTR("!");
    const size_w size = LARGE_FILE_SIZE;
    const size_w last_line = size - strlen("last line");
    TextCoord eof;
    TextCoord prev;
    TextChange change;
    tchar_t buf[32];
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
    CHECK(memcmp(buf, TSTR("second to last\r\n"), 16 * sizeof(tchar_t)) == 0);

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
    delete_file(path);
}

void textdocument_utf16_coord_reads_line()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    const char16_t text[] = { 0xfeff, 'a', '\r', '\n', 'b', 0xd83d, 0xde00, '\r', '\n', 'c' };
    TextCoord coord;
    tchar_t buf[16];
    bool exact = false;

    CHECK(write_temp_file(reinterpret_cast<const char *>(text), sizeof(text), path));
    CHECK(doc.init(path));

    CHECK(doc.getformat() == NCP_UTF16);
    CHECK(doc.byte_length() == 18);

    // the CR on the second line, after 'b' and a surrogate pair
    CHECK(doc.coord_from_pos(TextPos(12), &coord));
    CHECK(coord.line_begin.byte_offset == 6);
    CHECK(coord.line_next.byte_offset == 16);
    CHECK(coord.column == tstr(U"b\U0001F600").size());
    CHECK(doc_lineno(doc, coord, &exact) == 1);

    CHECK(tstring(buf, doc.getline(coord, buf, 16)) == tstr(U"b\U0001F600\r\n"));

    doc.clear();
    delete_file(path);
}

void textdocument_utf16be_coord_reads_line()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
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
    tchar_t buf[16];
    bool exact = false;

    CHECK(write_temp_file(reinterpret_cast<const char *>(text), sizeof(text), path));
    CHECK(doc.init(path));

    CHECK(doc.getformat() == NCP_UTF16BE);
    CHECK(doc.byte_length() == 18);

    // the CR on the second line, after 'b' and a surrogate pair
    CHECK(doc.coord_from_pos(TextPos(12), &coord));
    CHECK(coord.line_begin.byte_offset == 6);
    CHECK(coord.line_next.byte_offset == 16);
    CHECK(coord.column == tstr(U"b\U0001F600").size());
    CHECK(doc_lineno(doc, coord, &exact) == 1);

    CHECK(tstring(buf, doc.getline(coord, buf, 16)) == tstr(U"b\U0001F600\r\n"));

    doc.clear();
    delete_file(path);
}

void textdocument_utf8_coord_reads_line()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
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
    tchar_t buf[16];
    bool exact = false;

    CHECK(write_temp_file(reinterpret_cast<const char *>(text), sizeof(text), path));
    CHECK(doc.init(path));

    CHECK(doc.getformat() == NCP_UTF8);
    CHECK(doc.byte_length() == 11);

    // the CR on the second line, after 'b' and a surrogate pair
    CHECK(doc.coord_from_pos(TextPos(8), &coord));
    CHECK(coord.line_begin.byte_offset == 3);
    CHECK(coord.line_next.byte_offset == 10);
    CHECK(coord.column == tstr(U"b\U0001F600").size());
    CHECK(doc_lineno(doc, coord, &exact) == 1);

    CHECK(tstring(buf, doc.getline(coord, buf, 16)) == tstr(U"b\U0001F600\r\n"));

    doc.clear();
    delete_file(path);
}

void textdocument_coord_uses_byte_offset_for_ascii()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    TextCoord coord;

    CHECK(write_textdocument_file("abc\r\nxyz", DOC_ASCII, path));
    CHECK(doc.init(path));

    CHECK(doc.coord_from_pos(TextPos(6), &coord));
    CHECK(coord.byte_offset == 6);
    CHECK(coord.line_begin.byte_offset == 5);
    CHECK(coord.line_next.byte_offset == 8);
    CHECK(coord.column == 1);

    doc.clear();
    delete_file(path);
}

void textdocument_coord_uses_local_utf8_byte_offset()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
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

    // after the emoji: its four bytes are two UTF-16 units, or four UTF-8 ones
    CHECK(doc.coord_from_pos(TextPos(8), &coord));
    CHECK(coord.column == tstr(U"b\U0001F600").size());

    doc.clear();
    delete_file(path);
}

void textdocument_coord_resolves_document_end()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
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
    delete_file(path);
}

void textdocument_coord_moves_by_local_lines()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
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
    delete_file(path);
}

void textdocument_coord_moves_by_local_utf8_lines()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
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
    CHECK(doc.coord_from_line_pos(&line, tstr(U"b\U0001F600").size(), &at_pos));
    CHECK(at_pos.byte_offset == 8);
    CHECK(at_pos.column == tstr(U"b\U0001F600").size());

    doc.clear();
    delete_file(path);
}

void textdocument_coord_moves_across_changes()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    tchar_t xyz[] = TSTR("XYZ");
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
    delete_file(path);
}

tstring read_range(TextDocument &doc, uint32_t from_byte, uint32_t to_byte)
{
    TextCoord from;
    TextCoord to;
    tchar_t buf[64];
    uint32_t len;

    CHECK(doc.coord_from_pos(TextPos(from_byte), &from));
    CHECK(doc.coord_from_pos(TextPos(to_byte), &to));

    TextReader reader = doc.text_from_range(from, to);
    len = reader.read(buf, 64);

    return tstring(buf, len);
}

void textdocument_edits_by_coord()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    tchar_t xyz[] = TSTR("XYZ");
    TextChange change;
    TextCoord from;
    TextCoord to;

    CHECK(write_textdocument_file("hello\r\nworld\r\n", DOC_ASCII, path));
    CHECK(doc.init(path));

    CHECK(doc.coord_from_pos(TextPos(1), &from));
    CHECK(doc.insert_text(from, xyz, 3, &change) == 3);
    CHECK(change.at.byte_offset == 1 && change.erased == 0 && change.inserted == 3);
    CHECK(read_range(doc, 0, 17) == TSTR("hXYZello\r\nworld\r\n"));

    // A range may be given in either order.
    CHECK(doc.coord_from_pos(TextPos(8), &from));
    CHECK(doc.coord_from_pos(TextPos(4), &to));
    CHECK(doc.erase_text(from, to, &change) == 4);
    CHECK(change.at.byte_offset == 4 && change.erased == 4 && change.inserted == 0);
    CHECK(read_range(doc, 0, 13) == TSTR("hXYZ\r\nworld\r\n"));

    CHECK(doc.coord_from_pos(TextPos(6), &from));
    CHECK(doc.coord_from_pos(TextPos(11), &to));
    CHECK(doc.replace_text(from, to, xyz, 3, &change) == 3);
    CHECK(change.at.byte_offset == 6 && change.erased == 5 && change.inserted == 3);
    CHECK(read_range(doc, 0, 11) == TSTR("hXYZ\r\nXYZ\r\n"));
    CHECK(read_range(doc, 6, 9) == TSTR("XYZ"));

    // Undoing the replace brings back the erased word as one change.
    CHECK(doc.undo(&change));
    CHECK(change.at.byte_offset == 6 && change.erased == 3 && change.inserted == 5);
    CHECK(read_range(doc, 0, 13) == TSTR("hXYZ\r\nworld\r\n"));

    CHECK(doc.redo(&change));
    CHECK(change.at.byte_offset == 6 && change.erased == 5 && change.inserted == 3);

    doc.clear();
    delete_file(path);
}

void textdocument_change_excludes_header()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    tchar_t x[] = TSTR("X");
    TextChange change;

    CHECK(write_textdocument_file("ab\r\ncd", DOC_UTF16LE_BOM, path));
    CHECK(doc.init(path));

    CHECK(doc_insert(doc, 2, x, 1, &change) == 2);
    CHECK(change.at.byte_offset == 2 && change.erased == 0 && change.inserted == 2);

    CHECK(doc.undo(&change));
    CHECK(change.at.byte_offset == 2 && change.erased == 2 && change.inserted == 0);

    doc.clear();
    delete_file(path);
}

void textdocument_find_text_searches_raw_sequence()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    tchar_t alpha[] = TSTR("alpha");
    tchar_t missing[] = TSTR("omega");
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
    delete_file(path);
}

void textdocument_find_text_matches_ascii_case_insensitive()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    tchar_t alpha[] = TSTR("alpha");
    size_w match = static_cast<size_w>(-1);
    size_w match_length = 0;

    CHECK(write_textdocument_file("ALPHA beta", DOC_UTF16LE_BOM, path));
    CHECK(doc.init(path));

    CHECK(!doc_find(doc, alpha, 5, 0, doc.byte_length(), false, true, &match, &match_length));
    CHECK(doc_find(doc, alpha, 5, 0, doc.byte_length(), false, false, &match, &match_length));
    CHECK(match == 0);
    CHECK(match_length == 10);

    doc.clear();
    delete_file(path);
}

void textdocument_find_text_matches_unicode_case_insensitive_across_encodings()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    tstring cafe = tstr(U"caf\u00e9");
    size_w match = static_cast<size_w>(-1);
    size_w match_length = 0;

    const char utf8[] =
    {
        static_cast<char>(0xef), static_cast<char>(0xbb), static_cast<char>(0xbf),
        'C', 'A', 'F', static_cast<char>(0xc3), static_cast<char>(0x89)
    };
    CHECK(write_temp_file(utf8, sizeof(utf8), path));
    CHECK(doc.init(path));
    CHECK(doc_find(doc, cafe.c_str(), static_cast<uint32_t>(cafe.size()), 0, doc.byte_length(), false, false, &match, &match_length));
    CHECK(match == 0);
    CHECK(match_length == 5);
    doc.clear();
    delete_file(path);

    const char16_t utf16[] = { 0xfeff, 'C', 'A', 'F', 0x00c9 };
    CHECK(write_temp_file(reinterpret_cast<const char *>(utf16), sizeof(utf16), path));
    CHECK(doc.init(path));
    CHECK(doc_find(doc, cafe.c_str(), static_cast<uint32_t>(cafe.size()), 0, doc.byte_length(), false, false, &match, &match_length));
    CHECK(match == 0);
    CHECK(match_length == 8);
    doc.clear();
    delete_file(path);
}

void textdocument_find_text_converts_query_to_document_encoding()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    tchar_t beta[] = TSTR("beta");
    size_w match = static_cast<size_w>(-1);
    size_w match_length = 0;

    CHECK(write_textdocument_file("alpha beta gamma", DOC_UTF16BE_BOM, path));
    CHECK(doc.init(path));

    CHECK(doc_find(doc, beta, 4, 0, doc.byte_length(), false, true, &match, &match_length));
    CHECK(match == 12);
    CHECK(match_length == 8);

    doc.clear();
    delete_file(path);
}

void textdocument_find_text_nocase_slides_window()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    tchar_t alpha[] = TSTR("alpha");
    tchar_t z[] = TSTR("Z");
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
    delete_file(path);
}

void textdocument_find_text_nocase_backwards_over_utf8()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    tstring cafe = tstr(U"caf\u00e9");
    size_w match = static_cast<size_w>(-1);
    size_w match_length = 0;

    // "cafe CAFE x", each final e accented as a 2-byte sequence
    const uint32_t utf8[] =
    {
        0xef, 0xbb, 0xbf,
        'c', 'a', 'f', 0xc3, 0xa9, ' ', 'C', 'A', 'F', 0xc3, 0x89, ' ', 'x'
    };

    CHECK(write_units_file(utf8, sizeof(utf8) / sizeof(utf8[0]), 1, false, path));
    CHECK(doc.init(path));

    CHECK(doc_find(doc, cafe.c_str(), static_cast<uint32_t>(cafe.size()), 0, doc.byte_length(), true, false, &match, &match_length));
    CHECK(match == 6);
    CHECK(match_length == 5);

    CHECK(doc_find(doc, cafe.c_str(), static_cast<uint32_t>(cafe.size()), 0, 6, true, false, &match, &match_length));
    CHECK(match == 0);
    CHECK(match_length == 5);

    // ending the search between the bytes of the second accented E leaves only the first match
    CHECK(doc_find(doc, cafe.c_str(), static_cast<uint32_t>(cafe.size()), 0, 10, true, false, &match, &match_length));
    CHECK(match == 0);
    CHECK(match_length == 5);

    doc.clear();
    delete_file(path);
}

void textdocument_find_text_nocase_matches_supplementary_chars_in_each_encoding()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    tstring pattern = tstr(U"\U0001F600a");
    size_w match = static_cast<size_w>(-1);
    size_w match_length = 0;

    // U+1F600 then A, twice over with a space between
    const uint32_t utf8[]  = { 0xef, 0xbb, 0xbf, 0xf0, 0x9f, 0x98, 0x80, 'A', ' ', 0xf0, 0x9f, 0x98, 0x80, 'A' };
    const uint32_t utf16[] = { 0xfeff, 0xd83d, 0xde00, 'A', ' ', 0xd83d, 0xde00, 'A' };
    const uint32_t utf32[] = { 0xfeff, 0x1f600, 'A', ' ', 0x1f600, 'A' };

    const struct
    {
        const uint32_t *units;
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

        CHECK(doc_find(doc, pattern.c_str(), static_cast<uint32_t>(pattern.size()), 0, doc.byte_length(), false, false, &match, &match_length));
        CHECK(match == 0);
        CHECK(match_length == files[i].length);

        CHECK(doc_find(doc, pattern.c_str(), static_cast<uint32_t>(pattern.size()), 0, doc.byte_length(), true, false, &match, &match_length));
        CHECK(match == files[i].second);
        CHECK(match_length == files[i].length);

        doc.clear();
        delete_file(path);
    }
}

void textdocument_find_text_nocase_steps_over_split_surrogate_pair()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    tstring pattern = tstr(U"\U0001F600a");
    size_w match = static_cast<size_w>(-1);
    size_w match_length = 0;

    const uint32_t utf16[] = { 0xfeff, 0xd83d, 0xde00, 'A', ' ', 0xd83d, 0xde00, 'A' };

    CHECK(write_units_file(utf16, sizeof(utf16) / sizeof(utf16[0]), 2, false, path));
    CHECK(doc.init(path));

    // starting after the first high surrogate, the search picks up from the lone low one
    CHECK(doc_find(doc, pattern.c_str(), static_cast<uint32_t>(pattern.size()), 2, doc.byte_length(), false, false, &match, &match_length));
    CHECK(match == 8);
    CHECK(match_length == 6);

    // ending after the second high surrogate, the search steps back from the lone high one
    CHECK(doc_find(doc, pattern.c_str(), static_cast<uint32_t>(pattern.size()), 0, 10, true, false, &match, &match_length));
    CHECK(match == 0);
    CHECK(match_length == 6);

    doc.clear();
    delete_file(path);
}

// Room for all of a document's text: a byte decodes to at most one UTF-16
// unit, or three UTF-8 ones (as U+FFFD)
size_t text_room(TextDocument &doc)
{
    return static_cast<size_t>(doc.byte_length()) * 3 + 1;
}

// The whole of the line containing offset, as the view reads it to draw
tstring read_line(TextDocument &doc, size_w offset)
{
    TextCoord line;
    std::vector<tchar_t> buf(text_room(doc));

    CHECK(doc.coord_from_pos(TextPos(offset), &line));
    return tstring(&buf[0], doc.getline(line, &buf[0], static_cast<uint32_t>(buf.size())));
}

// The whole document, as the view reads it to copy
tstring read_document(TextDocument &doc)
{
    TextCoord from;
    TextCoord to;
    std::vector<tchar_t> buf(text_room(doc));

    CHECK(doc.coord_from_pos(TextPos(0), &from));
    CHECK(doc.coord_from_document_end(&to));

    TextReader reader = doc.text_from_range(from, to);
    return tstring(&buf[0], reader.read(&buf[0], static_cast<uint32_t>(buf.size())));
}

void textdocument_utf8_decodes_characters_across_blocks()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    TextCoord line;
    TextCoord coord;
    std::vector<uint32_t> bytes;
    tstring expected(255, 'a');
    tstring e_acute = tstr(U"\u00e9");
    tstring zhong = tstr(U"\u4e2d");

    // 255 bytes of 'a' put the two bytes of an accented e either side of byte 256
    bytes.push_back(0xef);
    bytes.push_back(0xbb);
    bytes.push_back(0xbf);
    bytes.insert(bytes.end(), 255, static_cast<uint32_t>('a'));
    bytes.push_back(0xc3);
    bytes.push_back(0xa9);
    bytes.push_back('b');
    expected += e_acute;
    expected += 'b';

    CHECK(write_units_file(&bytes[0], bytes.size(), 1, false, path));
    CHECK(doc.init(path));

    CHECK(read_line(doc, 0) == expected);
    CHECK(read_document(doc) == expected);

    CHECK(doc.coord_from_pos(TextPos(258), &coord));
    CHECK(coord.column == expected.size());

    CHECK(doc.coord_from_pos(TextPos(0), &line));
    CHECK(doc.coord_from_line_pos(&line, 255 + e_acute.size(), &coord));
    CHECK(coord.byte_offset == 257);

    doc.clear();
    delete_file(path);

    // 3-byte characters cross two block boundaries in three
    bytes.resize(3);
    for(int i = 0; i < 100; i++)
    {
        bytes.push_back(0xe4);
        bytes.push_back(0xb8);
        bytes.push_back(0xad);
    }
    expected.clear();
    for(int i = 0; i < 100; i++)
        expected += zhong;

    CHECK(write_units_file(&bytes[0], bytes.size(), 1, false, path));
    CHECK(doc.init(path));

    CHECK(read_line(doc, 0) == expected);
    CHECK(read_document(doc) == expected);

    CHECK(doc.coord_from_pos(TextPos(300), &coord));
    CHECK(coord.column == 100 * zhong.size());

    CHECK(doc.coord_from_pos(TextPos(0), &line));
    CHECK(doc.coord_from_line_pos(&line, 90 * zhong.size(), &coord));
    CHECK(coord.byte_offset == 270);

    doc.clear();
    delete_file(path);
}

void textdocument_decodes_whole_characters_to_text()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    TextCoord line;
    TextCoord coord;
    std::vector<uint32_t> units(1, 0xfeff);
    tstring expected(127, 'a');

    // UTF-16 with a surrogate pair either side of the 256th byte, where the
    // text is decoded a block at a time
    units.insert(units.end(), 127, static_cast<uint32_t>('a'));
    units.push_back(0xd83d);
    units.push_back(0xde00);
    expected += tstr(U"\U0001F600");

    CHECK(write_units_file(&units[0], units.size(), 2, false, path));
    CHECK(doc.init(path));
    CHECK(read_line(doc, 0) == expected);
    CHECK(doc.coord_from_pos(TextPos(258), &coord));
    CHECK(coord.column == expected.size());
    doc.clear();
    delete_file(path);

    // not UTF-8: each byte that isn't a character is a U+FFFD, one UTF-16 unit
    // or three UTF-8 ones, so a line can be more units than bytes
    CHECK(write_temp_file("a\x80\x80" "b", 4, path));
    CHECK(doc.init(path, NCP_UTF8));
    CHECK(read_line(doc, 0) == tstr(U"a\ufffd\ufffdb"));
    CHECK(doc.coord_from_pos(TextPos(4), &coord));
    CHECK(coord.column == tstr(U"a\ufffd\ufffdb").size());
    CHECK(doc.coord_from_pos(TextPos(0), &line));
    CHECK(doc.coord_from_line_pos(&line, tstr(U"a\ufffd\ufffd").size(), &coord));
    CHECK(coord.byte_offset == 3);
    doc.clear();
    delete_file(path);

#ifndef _WIN32
    // ASCII is Latin-1 here (Windows has its ANSI code page): an accented
    // letter is one byte but two UTF-8 units, and what Latin-1 can't hold is
    // saved as '?'
    tstring cafe = tstr(U"caf\u00e9");
    tstring face = tstr(U"\U0001F600");

    CHECK(write_temp_file("caf\xe9", 4, path));
    CHECK(doc.init(path, NCP_ASCII));
    CHECK(read_line(doc, 0) == cafe);
    CHECK(doc.coord_from_pos(TextPos(4), &coord));
    CHECK(coord.column == cafe.size());
    CHECK(doc_insert(doc, 4, face.c_str(), static_cast<uint32_t>(face.size())) == 1);
    CHECK(read_document(doc) == tstr(U"caf\u00e9?"));
    doc.clear();
    delete_file(path);
#endif
}

void textdocument_utf8_edits_keep_characters_across_buffers()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    const uint32_t bom[] = { 0xef, 0xbb, 0xbf };
    tstring text(255, 'a');
    tstring cjk;
    tchar_t xy[] = TSTR("xy");

    for(int i = 0; i < 100; i++)
        cjk += tstr(U"\u4e2d");

    // the accented e doesn't fit in what is left of the first 256-byte buffer
    text += tstr(U"\u00e9b");

    CHECK(write_units_file(bom, 3, 1, false, path));
    CHECK(doc.init(path));
    CHECK(doc_insert(doc, 0, &text[0], static_cast<uint32_t>(text.size())) == 258);
    CHECK(doc.byte_length() == 258);
    CHECK(read_document(doc) == text);
    doc.clear();
    delete_file(path);

    // nor do the 86th and later of these 3-byte characters
    CHECK(write_units_file(bom, 3, 1, false, path));
    CHECK(doc.init(path));
    CHECK(doc_insert(doc, 0, &cjk[0], static_cast<uint32_t>(cjk.size())) == 300);
    CHECK(read_document(doc) == cjk);
    doc.clear();
    delete_file(path);

    // replacing goes through the same buffer
    CHECK(write_units_file(bom, 3, 1, false, path));
    CHECK(doc.init(path));
    CHECK(doc_insert(doc, 0, xy, 2) == 2);
    CHECK(doc_replace(doc, 0, 2, &cjk[0], static_cast<uint32_t>(cjk.size())) == 300);
    CHECK(read_document(doc) == cjk);
    doc.clear();
    delete_file(path);
}

void textdocument_utf32_reads_and_edits_text()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    tchar_t b[] = TSTR("b");
    tchar_t x[] = TSTR("x");
    tstring face = tstr(U"\U0001F600");
    TextCoord coord;
    size_w match = static_cast<size_w>(-1);
    size_w match_length = 0;
    seqchar raw[4];
    const uint32_t abc[] = { 0xfeff, 'a', 'b', 'c' };

    for(int big_endian = 0; big_endian < 2; big_endian++)
    {
        CHECK(write_units_file(abc, 4, 4, big_endian != 0, path));
        CHECK(doc.init(path));

        CHECK(read_line(doc, 0) == TSTR("abc"));
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

        // U+1F600 is one UTF-32 unit
        CHECK(doc_replace(doc, 8, 12, face.c_str(), static_cast<uint32_t>(face.size())) == 4);
        CHECK(read_document(doc) == tstring(TSTR("xa")) + face + TSTR("c"));

        CHECK(doc_find(doc, face.c_str(), static_cast<uint32_t>(face.size()), 0, doc.byte_length(), false, true, &match, &match_length));
        CHECK(match == 8);
        CHECK(match_length == 4);

        doc.clear();
        delete_file(path);
    }
}

void textdocument_find_text_matchcase_stays_on_code_units()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    tchar_t n[] = TSTR("N");
    size_w match = static_cast<size_w>(-1);
    size_w match_length = 0;

    // U+4E01 U+4E00 is 01 4E 00 4E in UTF-16LE: an "N" (4E 00) at byte 1, across two characters
    const uint32_t before[] = { 0xfeff, 0x4e01, 0x4e00, 'N' };
    const uint32_t after[]  = { 0xfeff, 'N', 0x4e01, 0x4e00 };

    // U+4E01 U+10000 is 01 4E 00 00 00 00 01 00 in UTF-32LE: an "N" at byte 1
    const uint32_t utf32[]  = { 0xfeff, 0x4e01, 0x10000 };

    CHECK(write_units_file(before, 4, 2, false, path));
    CHECK(doc.init(path));
    CHECK(doc_find(doc, n, 1, 0, doc.byte_length(), false, true, &match, &match_length));
    CHECK(match == 4);
    CHECK(match_length == 2);
    doc.clear();
    delete_file(path);

    CHECK(write_units_file(after, 4, 2, false, path));
    CHECK(doc.init(path));
    CHECK(doc_find(doc, n, 1, 0, doc.byte_length(), true, true, &match, &match_length));
    CHECK(match == 0);
    CHECK(match_length == 2);
    doc.clear();
    delete_file(path);

    CHECK(write_units_file(utf32, 3, 4, false, path));
    CHECK(doc.init(path));
    CHECK(!doc_find(doc, n, 1, 0, doc.byte_length(), false, true, &match, &match_length));
    CHECK(!doc_find(doc, n, 1, 0, doc.byte_length(), true, true, &match, &match_length));
    doc.clear();
    delete_file(path);
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
    seqfilechar path[TEST_PATH_MAX];
    TextCoord line;
    TextCoord coord;
    TextCoord measured;
    size_w face = tstr(U"\U0001F600").size();

    // "a", U+1F600, "x": columns 0 a 1 [face] 1+face x 2+face, with column 2
    // inside the face (between the halves of its pair, in UTF-16)
    const uint32_t utf8[]  = { 0xef, 0xbb, 0xbf, 'a', 0xf0, 0x9f, 0x98, 0x80, 'x' };
    const uint32_t utf16[] = { 0xfeff, 'a', 0xd83d, 0xde00, 'x' };
    const uint32_t utf32[] = { 0xfeff, 'a', 0x1f600, 'x' };

    const struct
    {
        const uint32_t *units;
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

        // inside the face: back to its start
        CHECK(doc.coord_from_line_pos(&line, 2, &coord));
        CHECK(coord.byte_offset == files[i].after_a);
        CHECK(coord.column == 1);

        CHECK(doc.coord_from_line_pos(&line, 1 + face, &coord));
        CHECK(coord.byte_offset == files[i].after_face);
        CHECK(coord.column == 1 + face);

        CHECK(doc.coord_from_line_pos(&line, 2 + face, &coord));
        CHECK(coord.byte_offset == files[i].after_x);
        CHECK(coord.column == 2 + face);

        // a coordinate's column always matches its position
        CHECK(doc.coord_from_pos(coord, &measured));
        CHECK(measured.column == coord.column);

        doc.clear();
        delete_file(path);
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
    seqfilechar path[TEST_PATH_MAX];
    tchar_t beta[] = TSTR("beta");
    tchar_t upper[] = TSTR("BETA");
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
    delete_file(path);
}

void textreader_reads_whole_characters()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    tchar_t buf[4];
    TextReader reader;
    tstring face = tstr(U"\U0001F600");
    uint32_t room = static_cast<uint32_t>(face.size());

    // "a", U+1F600, "b"
    const uint32_t utf8[]  = { 0xef, 0xbb, 0xbf, 'a', 0xf0, 0x9f, 0x98, 0x80, 'b' };
    const uint32_t utf16[] = { 0xfeff, 'a', 0xd83d, 0xde00, 'b' };
    const uint32_t utf32[] = { 0xfeff, 'a', 0x1f600, 'b' };

    const struct
    {
        const uint32_t *units;
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

        // room for "a" and part of the face: only "a" is read
        CHECK(reader.read(buf, room) == 1);
        CHECK(buf[0] == 'a');
        CHECK(reader.pos() == TextPos(files[i].after_a));

        // no room for the face at all: nothing is read, but there is more to come
        CHECK(reader.read(buf, 1) == 0);
        CHECK(!reader.at_end());

        CHECK(tstring(buf, reader.read(buf, room)) == face);
        CHECK(reader.pos() == TextPos(files[i].after_face));

        CHECK(reader.read(buf, 4) == 1);
        CHECK(buf[0] == 'b');
        CHECK(reader.pos() == TextPos(files[i].after_b));
        CHECK(reader.at_end());
        CHECK(reader.read(buf, 4) == 0);

        doc.clear();
        delete_file(path);
    }
}

// The whole of a line's text, as the iterator reads it
tstring iterator_text(const LineIterator &line)
{
    tchar_t buf[64];

    return tstring(buf, line.text(buf, 64));
}

void lineiterator_walks_lines()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    tchar_t buf[64];
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
    CHECK(iterator_text(line) == TSTR("two\n"));

    CHECK(line.next());
    CHECK(line.line() == TextPos(9));
    CHECK(line.linebreak() == 0);
    CHECK(iterator_text(line) == TSTR("three"));

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
    CHECK(tstring(buf, reader.read(buf, 64)) == TSTR("one\r\n"));
    CHECK(reader.at_end());

    doc.clear();
    delete_file(path);
}

void lineiterator_ends_with_empty_line_after_final_break()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];

    CHECK(write_textdocument_file("a\n", DOC_ASCII, path));
    CHECK(doc.init(path));

    LineIterator line = doc.line_at(TextPos(0));
    CHECK(line.linebreak() == TXL_LF);

    CHECK(line.next());
    CHECK(line.line() == TextPos(2));
    CHECK(line.line().line_next == TextPos(2));
    CHECK(line.linebreak() == 0);
    CHECK(iterator_text(line) == TSTR(""));

    CHECK(!line.next());
    CHECK(line.prev());
    CHECK(line.line() == TextPos(0));

    doc.clear();
    delete_file(path);
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
    CHECK(iterator_text(line) == TSTR(""));

    // an iterator on no line can't move
    CHECK(!none.next());
    CHECK(!none.prev());
    CHECK(iterator_text(none) == TSTR(""));
    CHECK(none.reader().at_end());
}

// A temporary file name, with no file there
bool temp_name(seqfilechar path[TEST_PATH_MAX])
{
    return temp_file(path) && delete_file(path);
}

bool file_exists(const std::filesystem::path &path)
{
    std::error_code error;

    return std::filesystem::exists(path, error);
}

std::string read_file(const std::filesystem::path &path)
{
    std::string bytes;
    FILE *fp = open_file(path, "rb");
    char buf[0x10000];
    size_t got;

    if(fp == 0)
        return "<missing>";

    while((got = fread(buf, 1, sizeof(buf), fp)) != 0)
        bytes.append(buf, got);

    fclose(fp);
    return bytes;
}

#ifdef _WIN32
void textdocument_open_file_can_be_replaced_but_not_written()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    seqfilechar newpath[TEST_PATH_MAX];
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
    CHECK(read_document(doc) == TSTR("alpha\nbeta"));

    doc.clear();
    delete_file(path);
    delete_file(newpath);
}

void textdocument_reads_files_under_the_limit_into_memory()
{
    TextDocument doc;
    seqfilechar  path[TEST_PATH_MAX];
    HANDLE       writer;
    DWORD        written = 0;

    CHECK(write_textdocument_file("alpha\nbeta", DOC_ASCII, path));

    // a program writing the file, letting others read it
    writer = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, 0, OPEN_EXISTING, 0, 0);
    CHECK(writer != INVALID_HANDLE_VALUE);

    // shorter than the limit: read into memory, letting the program carry on
    doc.set_in_memory_limit(11);
    CHECK(doc.init(path));
    CHECK(read_document(doc) == TSTR("alpha\nbeta"));

    // and what it writes doesn't change the document
    CHECK(WriteFile(writer, "OVERWRITE!", 10, &written, 0) && written == 10);
    CloseHandle(writer);
    CHECK(read_document(doc) == TSTR("alpha\nbeta"));
    doc.clear();

    // as long as the limit: read from disk, and locked while it's open
    doc.set_in_memory_limit(10);
    CHECK(doc.init(path));
    CHECK(read_document(doc) == TSTR("OVERWRITE!"));
    CHECK(!others_can_write(path));

    doc.clear();
    delete_file(path);
}
#endif

// Save a document as it is: its own encoding, byte-order mark and line breaks
bool save_as_is(TextDocument &doc, const seqfilechar *path)
{
    return doc.save(path, doc.getformat(), doc.has_bom(), 0);
}

// Write text with a SaveFile, as a save does
bool save_bytes(const seqfilechar *path, const char *text)
{
    SaveFile file;

    return file.open(path) && file.write(text, strlen(text)) && file.commit();
}

// The view's filter writes its lines to one of these as a document reads it
void tempappendfile_grows_under_a_document()
{
    TempAppendFile         file;
    TextDocument           doc;
    std::filesystem::path  path;
    std::string            bom  = encode_text(U"\ufeff", TCHAR_UTF8 ? NCP_UTF8 : NCP_UTF16).substr(TCHAR_UTF8 ? 0 : 2);
    tstring                one  = tstr(U"one\u00e9\r\n");
    tstring                two  = tstr(U"two\U0001F600\r\n");
    size_w                 added = 0;

    CHECK(file.create());
    path = file.path();
    CHECK(file_exists(path));

    // the build's own byte-order mark, then text as the build holds it
    CHECK(file.write(bom.data(), bom.size()));
    CHECK(file.write(one.data(), one.size() * sizeof(tchar_t)));
    CHECK(doc.init(file.path(), true));
    CHECK(read_document(doc) == one);

    // what's written later shows when the document grows
    CHECK(file.write(two.data(), two.size() * sizeof(tchar_t)));
    CHECK(doc.grow(&added));
    CHECK(added == two.size() * sizeof(tchar_t));
    CHECK(read_document(doc) == one + two);

    // closed, and no longer read, it's gone
    doc.clear();
    file.close();
    CHECK(!file_exists(path));
    CHECK(!file.write("x", 1));
}

void savefile_writes_a_new_file()
{
    seqfilechar path[TEST_PATH_MAX];
    std::filesystem::path first;

    CHECK(temp_name(path));
    CHECK(save_bytes(path, "hello"));
    CHECK(read_file(path) == "hello");

    // the file written first, beside the target, has become the target
    first = path;
    first += ".~0";
    CHECK(!file_exists(first));

    // saving again replaces it
    CHECK(save_bytes(path, ""));
    CHECK(read_file(path) == "");
    CHECK(!file_exists(first));

    delete_file(path);
}

void savefile_steps_past_a_file_in_the_way()
{
    seqfilechar path[TEST_PATH_MAX];
    std::filesystem::path taken;
    std::filesystem::path next;
    FILE *fp;

    CHECK(temp_name(path));
    taken = path;
    taken += ".~0";
    next = path;
    next += ".~1";

    // something already has the name the save would write to first
    fp = open_file(taken, "wb");
    CHECK(fp != 0);
    CHECK(fp && fwrite("leftover", 1, 8, fp) == 8);
    if(fp)
        fclose(fp);

    CHECK(save_bytes(path, "text"));
    CHECK(read_file(path) == "text");
    CHECK(read_file(taken) == "leftover");
    CHECK(!file_exists(next));

    delete_file(taken.c_str());
    delete_file(path);
}

void savefile_leaves_the_file_as_it_was_until_committed()
{
    seqfilechar path[TEST_PATH_MAX];
    std::filesystem::path other;

    CHECK(write_temp_file("original", 8, path));
    other = path;
    other += ".~0";

    // written, but never committed
    {
        SaveFile file;

        CHECK(file.open(path));
        CHECK(file.write("changed", 7));
        CHECK(file_exists(other));
        CHECK(read_file(path) == "original");
    }

    CHECK(read_file(path) == "original");
    CHECK(!file_exists(other));

#ifdef _WIN32
    // a read-only file can't be replaced; the error is the one to report
    CHECK(SetFileAttributes(path, FILE_ATTRIBUTE_READONLY));
    CHECK(!save_bytes(path, "changed"));
    CHECK(GetLastError() == ERROR_ACCESS_DENIED);
    CHECK(read_file(path) == "original");
    CHECK(!file_exists(other));
    SetFileAttributes(path, FILE_ATTRIBUTE_NORMAL);
#endif

    // nor can it save where there's no directory to write to
    other = std::filesystem::path(path) / "missing.txt";
    CHECK(!save_bytes(other.c_str(), "changed"));

    delete_file(path);
}

#ifdef _WIN32
void textdocument_failed_save_stays_modified()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    tchar_t x[] = TSTR("X");

    CHECK(write_textdocument_file("ab", DOC_ASCII, path));
    CHECK(doc.init(path));
    CHECK(doc_insert(doc, 0, x, 1) == 1);
    CHECK(SetFileAttributes(path, FILE_ATTRIBUTE_READONLY));

    CHECK(!doc.save(path, doc.getformat(), false, 0));
    CHECK(doc.modified());
    CHECK(read_file(path) == "ab");

    SetFileAttributes(path, FILE_ATTRIBUTE_NORMAL);
    doc.clear();
    delete_file(path);
}
#endif

void textdocument_save_over_its_own_file_keeps_text_and_undo()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    std::string original;
    std::string edited;
    tchar_t head[] = TSTR("HEAD");
    tchar_t tail[] = TSTR("TAIL");

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
    CHECK(read_document(doc) == tstring(edited.begin(), edited.end()));

    // undo back to the original text, now only in the replaced file
    CHECK(doc.undo(0));
    CHECK(doc.undo(0));
    CHECK(doc.undo(0));
    CHECK(!doc.can_undo());
    CHECK(doc.modified());
    CHECK(read_document(doc) == tstring(original.begin(), original.end()));

    // which saves as well
    CHECK(save_as_is(doc, path));
    CHECK(read_file(path) == original);
    CHECK(!doc.modified());

    CHECK(doc.redo(0));
    CHECK(doc.modified());

    doc.clear();
    delete_file(path);
}

void textdocument_save_keeps_the_byte_order_mark()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    tchar_t x[] = TSTR("X");
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
    delete_file(path);
}

void textdocument_save_with_or_without_the_byte_order_mark()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    tchar_t x[] = TSTR("X");
    const std::string bom = "\xef\xbb\xbf";

    CHECK(write_textdocument_file("ab", DOC_UTF8_BOM, path));
    CHECK(doc.init(path));
    CHECK(doc.has_bom());
    CHECK(doc_insert(doc, 0, x, 1) == 1);

    CHECK(doc.save(path, doc.getformat(), false, 0));
    CHECK(read_file(path) == "Xab");
    CHECK(!doc.has_bom());
    CHECK(read_document(doc) == TSTR("Xab"));

    // and back again: the mark comes from the encoding, not the text
    CHECK(doc.save(path, doc.getformat(), true, 0));
    CHECK(read_file(path) == bom + "Xab");
    CHECK(doc.has_bom());

    doc.clear();
    CHECK(!doc.has_bom());
    delete_file(path);

    // ASCII has no byte-order mark to write
    CHECK(write_textdocument_file("ab", DOC_ASCII, path));
    CHECK(doc.init(path, NCP_ASCII));
    CHECK(!doc.has_bom());
    CHECK(doc.save(path, doc.getformat(), true, 0));
    CHECK(read_file(path) == "ab");
    CHECK(!doc.has_bom());

    doc.clear();
    delete_file(path);
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
    seqfilechar path[TEST_PATH_MAX];
    seqfilechar newpath[TEST_PATH_MAX];
    tchar_t x[] = TSTR("X");

    // "a", a line break, e with an acute accent, and a grinning face (a surrogate pair)
    const uint32_t units[] = { 0xfeff, 'a', '\r', '\n', 0xe9, 0xd83d, 0xde00 };
    const std::string utf8 = "Xa\r\n\xc3\xa9\xf0\x9f\x98\x80";
    const char utf32be[] = { 0, 0, '\xfe', '\xff', 0, 0, 0, 'X', 0, 0, 0, 'a', 0, 0, 0, '\r', 0, 0, 0, '\n',
                             0, 0, 0, '\xe9', 0, 1, '\xf6', 0 };
    tstring text = tstr(U"Xa\r\n\u00e9\U0001F600");

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
    delete_file(path);
    delete_file(newpath);
}

void textdocument_save_converts_line_breaks()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    seqfilechar newpath[TEST_PATH_MAX];

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
    delete_file(path);
    delete_file(newpath);
}

void textdocument_save_keeps_a_crlf_split_between_reads_as_one()
{
    // the conversion reads 0x4000 UTF-16 units at a time: put the CR and LF of
    // a CRLF either side of that, and either side of that
    for(size_t before = 0x4000 - 3; before <= 0x4000 + 1; before++)
    {
        TextDocument doc;
        seqfilechar path[TEST_PATH_MAX];
        std::string text(before, 'x');

        text += "\r\ny";

        CHECK(write_temp_file(text.data(), text.size(), path));
        CHECK(doc.init(path));
        CHECK(doc.save(path, NCP_ASCII, false, TXL_LF));
        CHECK(read_file(path) == std::string(before, 'x') + "\ny");

        doc.clear();
        delete_file(path);
    }
}

void textdocument_init_in_a_given_encoding()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    const char ansi[] = "caf\xe9";

    CHECK(write_temp_file(ansi, 4, path));

    // not valid UTF-8, so it opens as ASCII...
    CHECK(doc.init(path));
    CHECK(doc.getformat() == NCP_ASCII);

    // ...unless told otherwise, when its last byte isn't a character
    CHECK(doc.init(path, NCP_UTF8));
    CHECK(doc.getformat() == NCP_UTF8);
    CHECK(!doc.has_bom());
    CHECK(read_document(doc) == tstr(U"caf\ufffd"));

    CHECK(!doc.init(path, static_cast<TEXT_ENCODING>(99)));

    doc.clear();
    delete_file(path);

    // the encoding's own byte-order mark is skipped, and another's is text
    CHECK(write_textdocument_file("ab", DOC_UTF8_BOM, path));
    CHECK(doc.init(path, NCP_UTF8));
    CHECK(doc.has_bom());
    CHECK(read_document(doc) == TSTR("ab"));

    CHECK(doc.init(path, NCP_ASCII));
    CHECK(!doc.has_bom());
    CHECK(doc.byte_length() == 5);

    doc.clear();
    delete_file(path);

    // UTF-16 starting with U+0000 looks like UTF-32's byte-order mark
    const uint32_t units[] = { 0xfeff, 0, 'a' };

    CHECK(write_units_file(units, sizeof(units) / sizeof(units[0]), 2, false, path));
    CHECK(doc.init(path));
    CHECK(doc.getformat() == NCP_UTF32);

    CHECK(doc.init(path, NCP_UTF16));
    CHECK(doc.has_bom());
    CHECK(doc.byte_length() == 4);
    CHECK(read_document(doc) == tstring(1, TSTR('\0')) + TSTR("a"));

    doc.clear();
    delete_file(path);
}

// All of a document's text, as tchar_t text
tstring document_text(TextDocument &doc)
{
    tstring text;
    TextReader   reader = doc.text_from_range(TextPos(0), TextPos(doc.byte_length()));
    tchar_t      buf[256];
    uint32_t     len;

    while((len = reader.read(buf, 256)) != 0)
        text.append(buf, len);

    return text;
}

void textdocument_replace_linebreaks_makes_them_one_kind()
{
    // CRLF, LF, CR, and an LF then a CR, which is two line breaks
    const char32_t mixed[] = U"a\r\nb\nc\rd\n\re";
    const TEXT_ENCODING encodings[] = { NCP_UTF8, NCP_UTF16, NCP_UTF32 };
    struct { uint32_t linebreak; const tchar_t *expected; } cases[] =
    {
        { TXL_CRLF, TSTR("a\r\nb\r\nc\r\nd\r\n\r\ne") },
        { TXL_LF,   TSTR("a\nb\nc\nd\n\ne") },
        { TXL_CR,   TSTR("a\rb\rc\rd\r\re") },
    };

    for(size_t e = 0; e < sizeof(encodings) / sizeof(encodings[0]); e++)
    {
        for(size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++)
        {
            std::string  bytes = encode_text(mixed, encodings[e]);
            TextDocument doc;
            TextChange   change;
            seqfilechar  path[TEST_PATH_MAX];

            CHECK(write_temp_file(bytes.data(), bytes.size(), path));
            CHECK(doc.init(path, encodings[e]));

            doc.replace_linebreaks(TextPos(0), TextPos(doc.byte_length()), cases[c].linebreak, &change);
            CHECK(document_text(doc) == cases[c].expected);

            // one undo step brings the mix back
            CHECK(doc.undo(&change));
            CHECK(document_text(doc) == tstr(mixed));

            doc.clear();
            delete_file(path);
        }
    }

    // longer text goes into the document in pieces, and still undoes in one step
    {
        std::string  text;
        TextDocument doc;
        TextChange   change;
        seqfilechar  path[TEST_PATH_MAX];

        for(int i = 0; i < 1000; i++)
            text += "line\r\n";

        CHECK(write_temp_file(text.data(), text.size(), path));
        CHECK(doc.init(path, NCP_UTF8));

        doc.replace_linebreaks(TextPos(0), TextPos(doc.byte_length()), TXL_LF, &change);
        CHECK(doc.byte_length() == 5000);

        CHECK(doc.undo(&change));
        CHECK(doc.byte_length() == 6000);

        doc.clear();
        delete_file(path);
    }

    // part of the text converts just the line breaks inside it: "b\nc\r" here
    {
        TextDocument doc;
        seqfilechar  path[TEST_PATH_MAX];

        CHECK(write_temp_file("a\r\nb\nc\rd", 8, path));
        CHECK(doc.init(path, NCP_UTF8));

        doc.replace_linebreaks(TextPos(3), TextPos(7), TXL_LF, 0);
        CHECK(document_text(doc) == TSTR("a\r\nb\nc\nd"));

        doc.clear();
        delete_file(path);
    }
}

void textdocument_detects_utf8_without_a_bom()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    std::string text(MEM_BLOCK_SIZE - 1, 'a');

    // valid UTF-8
    CHECK(write_temp_file("caf\xc3\xa9", 5, path));
    CHECK(doc.init(path));
    CHECK(doc.getformat() == NCP_UTF8);
    CHECK(!doc.has_bom());
    CHECK(read_document(doc) == tstr(U"caf\u00e9"));
    doc.clear();
    delete_file(path);

    // plain ASCII is valid UTF-8, and so is nothing at all
    CHECK(write_temp_file("plain", 5, path));
    CHECK(doc.init(path));
    CHECK(doc.getformat() == NCP_UTF8);
    doc.clear();
    delete_file(path);

    CHECK(write_temp_file("", 0, path));
    CHECK(doc.init(path));
    CHECK(doc.getformat() == NCP_UTF8);
    doc.clear();
    delete_file(path);

    // anything else is ANSI
    CHECK(write_temp_file("caf\xe9 au lait", 12, path));
    CHECK(doc.init(path));
    CHECK(doc.getformat() == NCP_ASCII);
    doc.clear();
    delete_file(path);

    // only the first block is checked: a character it cuts in two is fine,
    // and what comes after it isn't looked at
    text += "\xc3\xa9";
    text += "caf\xe9";

    CHECK(write_temp_file(text.data(), text.size(), path));
    CHECK(doc.init(path));
    CHECK(doc.getformat() == NCP_UTF8);
    doc.clear();
    delete_file(path);
}

void textdocument_new_document_is_utf8()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    seqfilechar newpath[TEST_PATH_MAX];
    const tstring text = tstr(U"a\u00e9\nb");

    // after a UTF-16 file, none of which should be left behind
    CHECK(write_textdocument_file("x\ny", DOC_UTF16LE_BOM, path));
    CHECK(doc.init(path));
    CHECK(doc.clear());

    CHECK(doc.getformat() == NCP_UTF8);
    CHECK(!doc.has_bom());
    CHECK(doc_insert(doc, 0, text.c_str(), static_cast<uint32_t>(text.size())) == 5);
    CHECK(doc.linecount() == 2);

    CHECK(temp_name(newpath));
    CHECK(save_as_is(doc, newpath));
    CHECK(read_file(newpath) == "a\xc3\xa9\nb");

    delete_file(path);
    delete_file(newpath);
}

#ifdef _WIN32
void textdocument_ascii_turns_what_it_cant_hold_into_question_marks()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    seqfilechar newpath[TEST_PATH_MAX];
    const uint32_t units[] = { 0xfeff, 'a', 0xd83d, 0xde00, 'b' };
    const tchar_t face[] = { 0xd83d, 0xde00 };

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
    delete_file(path);

    // typed into an ASCII document, the same
    CHECK(write_textdocument_file("ab", DOC_ASCII, path));
    CHECK(doc.init(path, NCP_ASCII));
    CHECK(doc_insert(doc, 1, face, 2) == 2);
    CHECK(read_document(doc) == TSTR("a??b"));

    doc.clear();
    delete_file(path);
    delete_file(newpath);
}

// Neatpad keeps a file's window position in a stream of the file
void textdocument_save_keeps_the_files_other_streams()
{
    TextDocument doc;
    seqfilechar path[TEST_PATH_MAX];
    tchar_t x[] = TSTR("X");
    tstring stream;
    HANDLE hFile;
    DWORD written;

    CHECK(write_textdocument_file("abc", DOC_ASCII, path));
    stream = path;
    stream += TSTR(":Neatpad.Settings");

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
    delete_file(path);
}
#endif

void textdocument_replaces_every_match_in_one_undo_step()
{
    TextDocument doc;
    seqfilechar  path[TEST_PATH_MAX];
    TextChange   change;
    std::string  lines;
    tstring replaced;

    // UTF-16, where a match's bytes aren't its characters
    CHECK(write_textdocument_file("cat Cat CAT concat", DOC_UTF16LE_BOM, path));
    CHECK(doc.init(path));

    // every match, whatever its case
    CHECK(doc.replace_all(TSTR("cat"), 3, false, TSTR("dog"), 3) == 4);
    CHECK(read_document(doc) == TSTR("dog dog dog condog"));

    // undone all at once
    CHECK(doc.undo(&change));
    CHECK(read_document(doc) == TSTR("cat Cat CAT concat"));
    CHECK(!doc.can_undo());

    // a replacement holding what it replaces isn't replaced again
    CHECK(doc.replace_all(TSTR("cat"), 3, true, TSTR("cats"), 4) == 2);
    CHECK(read_document(doc) == TSTR("cats Cat CAT concats"));

    // replacing with nothing erases
    CHECK(doc.replace_all(TSTR("cats"), 4, true, TSTR(""), 0) == 2);
    CHECK(read_document(doc) == TSTR(" Cat CAT con"));

    // no match, no change
    CHECK(doc.replace_all(TSTR("dog"), 3, true, TSTR("x"), 1) == 0);
    CHECK(read_document(doc) == TSTR(" Cat CAT con"));

    doc.clear();
    delete_file(path);

    // many matches across line pages, each replaced in place
    for(int i = 0; i < 50000; i++)
    {
        lines += "ab\n";
        replaced += TSTR("axyz\n");
    }

    CHECK(write_textdocument_file(lines.c_str(), DOC_ASCII, path));
    CHECK(doc.init(path));

    CHECK(doc.replace_all(TSTR("b"), 1, true, TSTR("xyz"), 3) == 50000);
    CHECK(read_document(doc) == replaced);
    CHECK(doc.linecount() == 50001);

    doc.clear();
    delete_file(path);
}

// The lines filter_lines finds, 'bytes' at a time, each followed by a |
tstring filtered_lines(TextDocument &doc, const tchar_t *text, bool matchcase, size_w bytes)
{
    tstring lines;
    TextPos      from;
    TextPos      next;

    while(from.byte_offset < doc.byte_length())
    {
        next = doc.filter_lines(text, (uint32_t)tstrlen(text), matchcase, from, bytes, [&](TextPos begin, TextPos line_next)
        {
            TextReader reader = doc.text_from_range(begin, line_next);
            tchar_t    buf[256];
            uint32_t   len;

            while((len = reader.read(buf, 256)) != 0)
                lines.append(buf, len);

            lines += TSTR("|");
        });

        // it always gets further
        CHECK(next > from);
        if(!(next > from))
            break;

        from = next;
    }

    return lines;
}

void textdocument_filters_lines_holding_a_match()
{
    const DocTestEncoding encodings[] = { DOC_ASCII, DOC_UTF16LE_BOM };
    const size_w          budgets[]   = { 1, 5, 13, 1000 };

    for(size_t e = 0; e < sizeof(encodings) / sizeof(encodings[0]); e++)
    {
        TextDocument doc;
        seqfilechar  path[TEST_PATH_MAX];

        // lines ending CRLF, LF and CR, one matching twice, and a last one without a break
        CHECK(write_textdocument_file("alpha one\r\nbeta\r\nALPHA two\ngamma alpha alpha\rdelta\nend alpha", encodings[e], path));
        CHECK(doc.init(path));

        // each line found once, in whole, however the document is split up
        for(size_t b = 0; b < sizeof(budgets) / sizeof(budgets[0]); b++)
        {
            CHECK(filtered_lines(doc, TSTR("alpha"), false, budgets[b]) == TSTR("alpha one\r\n|ALPHA two\n|gamma alpha alpha\r|end alpha|"));
            CHECK(filtered_lines(doc, TSTR("alpha"), true, budgets[b]) == TSTR("alpha one\r\n|gamma alpha alpha\r|end alpha|"));
        }

        CHECK(filtered_lines(doc, TSTR("zeta"), false, 1000) == TSTR(""));

        doc.clear();
        delete_file(path);
    }
}

void textdocument_diff_maps_encoded_insertions_to_utf16_columns()
{
    const TEXT_ENCODING encodings[] = {NCP_UTF8, NCP_UTF16, NCP_UTF16BE, NCP_UTF32, NCP_UTF32BE};

    for (TEXT_ENCODING encoding : encodings)
    {
        TextDocument doc;
        seqfilechar  path[TEST_PATH_MAX];
        std::string  bytes = encode_text(U"first\r\na\u00e9\U0001F600z\r\nlast", encoding);
        TextCoord    line;
        TextCoord    at;
        TextChange   change;
        size_w       column = tstr(U"a\u00e9\U0001F600").size();
        tstring      text   = tstr(U"\U0001F600!");

        CHECK(write_temp_file(bytes.data(), bytes.size(), path));
        CHECK(doc.init(path));
        CHECK(doc.coord_from_lineno(1, &line) == TDR_OK);
        CHECK(!doc.line_diff(line));
        CHECK(doc.coord_from_line_pos(&line, column, &at));
        CHECK(doc.insert_text(at, text.c_str(), static_cast<uint32_t>(text.size()), &change) > 0);
        CHECK(doc.coord_from_lineno(1, &line) == TDR_OK);

        std::vector<std::pair<size_w, size_w>> inserted;
        auto                                   collect = [&](size_w column, size_w length)
        {
            inserted.push_back({column, length});
        };

        CHECK(doc.line_diff(line, collect));
        CHECK(inserted.size() == 1 && inserted[0].first == column && inserted[0].second == text.size());
        inserted.clear();
        CHECK(doc.line_diff(line, collect, column + 1)); // never colour part of a character
        CHECK(inserted.empty());
        CHECK(doc.save(path, encoding, encoding != NCP_UTF8, 0));
        CHECK(!doc.line_diff(line));
        CHECK(doc.undo(&change));
        CHECK(doc.coord_from_lineno(1, &line) == TDR_OK);
        inserted.clear();
        CHECK(doc.line_diff(line, collect));
        CHECK(inserted.empty());

        doc.clear();
        CHECK(delete_file(path));
    }
}

void textdocument_diff_assigns_deletions_to_one_surviving_line()
{
    TextDocument doc;
    seqfilechar  path[TEST_PATH_MAX];
    TextCoord    line;

    CHECK(write_temp_file("left\r\nright\r\n", 13, path));
    CHECK(doc.init(path));
    CHECK(doc.erase_text(TextPos(6), TextPos(13), 0) == 7);
    CHECK(doc.coord_from_lineno(0, &line) == TDR_OK);
    CHECK(!doc.line_diff(line));
    CHECK(doc.coord_from_lineno(1, &line) == TDR_OK);
    CHECK(doc.line_diff(line) == TextDocument::diff_erased_before); // empty EOF line
    CHECK(doc.erase_text(TextPos(0), TextPos(6), 0) == 6);
    CHECK(doc.coord_from_pos(TextPos(0), &line));
    CHECK(doc.line_diff(line) == TextDocument::diff_erased_before);
    CHECK(doc.save(path, doc.getformat(), false, 0));
    CHECK(!doc.line_diff(line));

    doc.clear();
    CHECK(delete_file(path));
}

void textdocument_diff_distinguishes_insertions_and_deletion_sides()
{
    TextDocument doc;
    seqfilechar  path[TEST_PATH_MAX];
    TextCoord    line;
    TextChange   change;

    CHECK(write_temp_file("first\r\nremoved\r\nlast", 20, path));
    CHECK(doc.init(path));
    CHECK(doc.erase_text(TextPos(7), TextPos(16), &change) == 9);
    CHECK(doc.coord_from_lineno(0, &line) == TDR_OK);
    CHECK(doc.line_diff(line) == 0);
    CHECK(doc.coord_from_lineno(1, &line) == TDR_OK);
    CHECK(doc.line_diff(line) == TextDocument::diff_erased_before);
    CHECK(doc.undo(&change));
    CHECK(doc.coord_from_lineno(1, &line) == TDR_OK);
    CHECK(doc.line_diff(line) == 0);
    CHECK(doc.redo(&change));
    CHECK(doc.coord_from_lineno(1, &line) == TDR_OK);
    CHECK(doc.line_diff(line) == TextDocument::diff_erased_before);

    // A second deletion at an unterminated EOF line keeps both boundaries.
    CHECK(doc.erase_text(TextPos(9), TextPos(11), &change) == 2);
    CHECK(doc.coord_from_lineno(1, &line) == TDR_OK);
    CHECK(doc.line_diff(line) == (TextDocument::diff_erased_before | TextDocument::diff_erased_after));
    CHECK(doc.save(path, doc.getformat(), false, 0));
    CHECK(doc.line_diff(line) == 0);
    CHECK(doc.undo(&change));
    CHECK(doc.coord_from_lineno(1, &line) == TDR_OK);
    CHECK(doc.line_diff(line) == TextDocument::diff_inserted);

    doc.clear();
    CHECK(delete_file(path));
}

const test_case tests[] =
{
    { "textdocument_diff_distinguishes_insertions_and_deletion_sides", textdocument_diff_distinguishes_insertions_and_deletion_sides },
    { "textdocument_diff_maps_encoded_insertions_to_utf16_columns", textdocument_diff_maps_encoded_insertions_to_utf16_columns },
    { "textdocument_diff_assigns_deletions_to_one_surviving_line", textdocument_diff_assigns_deletions_to_one_surviving_line },
    { "textdocument_edits_beyond_4gb", textdocument_edits_beyond_4gb },
    { "textdocument_utf16_coord_reads_line", textdocument_utf16_coord_reads_line },
    { "textdocument_utf16be_coord_reads_line", textdocument_utf16be_coord_reads_line },
    { "textdocument_utf8_coord_reads_line", textdocument_utf8_coord_reads_line },
    { "textdocument_coord_uses_byte_offset_for_ascii", textdocument_coord_uses_byte_offset_for_ascii },
    { "textdocument_coord_uses_local_utf8_byte_offset", textdocument_coord_uses_local_utf8_byte_offset },
    { "textdocument_coord_resolves_document_end", textdocument_coord_resolves_document_end },
    { "textdocument_coord_moves_by_local_lines", textdocument_coord_moves_by_local_lines },
    { "textdocument_coord_moves_by_local_utf8_lines", textdocument_coord_moves_by_local_utf8_lines },
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
    { "textdocument_utf8_decodes_characters_across_blocks", textdocument_utf8_decodes_characters_across_blocks },
    { "textdocument_decodes_whole_characters_to_text", textdocument_decodes_whole_characters_to_text },
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
#ifdef _WIN32
    { "textdocument_open_file_can_be_replaced_but_not_written", textdocument_open_file_can_be_replaced_but_not_written },
    { "textdocument_reads_files_under_the_limit_into_memory", textdocument_reads_files_under_the_limit_into_memory },
#endif
    { "tempappendfile_grows_under_a_document", tempappendfile_grows_under_a_document },
    { "savefile_writes_a_new_file", savefile_writes_a_new_file },
    { "savefile_steps_past_a_file_in_the_way", savefile_steps_past_a_file_in_the_way },
    { "savefile_leaves_the_file_as_it_was_until_committed", savefile_leaves_the_file_as_it_was_until_committed },
#ifdef _WIN32
    { "textdocument_failed_save_stays_modified", textdocument_failed_save_stays_modified },
#endif
    { "textdocument_save_over_its_own_file_keeps_text_and_undo", textdocument_save_over_its_own_file_keeps_text_and_undo },
    { "textdocument_save_keeps_the_byte_order_mark", textdocument_save_keeps_the_byte_order_mark },
    { "textdocument_save_with_or_without_the_byte_order_mark", textdocument_save_with_or_without_the_byte_order_mark },
    { "textdocument_save_converts_the_encoding", textdocument_save_converts_the_encoding },
    { "textdocument_save_converts_line_breaks", textdocument_save_converts_line_breaks },
    { "textdocument_save_keeps_a_crlf_split_between_reads_as_one", textdocument_save_keeps_a_crlf_split_between_reads_as_one },
    { "textdocument_init_in_a_given_encoding", textdocument_init_in_a_given_encoding },
    { "textdocument_replace_linebreaks_makes_them_one_kind", textdocument_replace_linebreaks_makes_them_one_kind },
    { "textdocument_replaces_every_match_in_one_undo_step", textdocument_replaces_every_match_in_one_undo_step },
    { "textdocument_filters_lines_holding_a_match", textdocument_filters_lines_holding_a_match },
    { "textdocument_detects_utf8_without_a_bom", textdocument_detects_utf8_without_a_bom },
    { "textdocument_new_document_is_utf8", textdocument_new_document_is_utf8 },
#ifdef _WIN32
    { "textdocument_ascii_turns_what_it_cant_hold_into_question_marks", textdocument_ascii_turns_what_it_cant_hold_into_question_marks },
    { "textdocument_save_keeps_the_files_other_streams", textdocument_save_keeps_the_files_other_streams },
#endif
};
}

const test_group document_tests = { "document", tests, sizeof(tests) / sizeof(tests[0]) };
