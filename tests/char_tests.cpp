//
//	Character tests: the conversions in Unicode.c, and the character index -
//	the UTF-16 positions counted beside the line index
//
#include "tests.h"

namespace
{
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

const test_case tests[] =
{
    { "textdocument_charoffset_in_each_encoding", textdocument_charoffset_in_each_encoding },
    { "unicode_utf8_prev_steps_back_to_lead_byte", unicode_utf8_prev_steps_back_to_lead_byte },
    { "unicode_utf16_prev_joins_surrogate_pair", unicode_utf16_prev_joins_surrogate_pair },
    { "unicode_utf32_char_reads_both_byte_orders", unicode_utf32_char_reads_both_byte_orders },
    { "unicode_utf8_complete_length_leaves_out_cut_sequence", unicode_utf8_complete_length_leaves_out_cut_sequence },
    { "unicode_utf16_to_utf8_leaves_character_that_does_not_fit", unicode_utf16_to_utf8_leaves_character_that_does_not_fit },
    { "unicode_utf32_streams_convert_both_byte_orders", unicode_utf32_streams_convert_both_byte_orders },
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
};
}

const test_group char_tests = { "chars", tests, sizeof(tests) / sizeof(tests[0]) };
