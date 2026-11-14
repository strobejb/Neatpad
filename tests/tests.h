//
//	Neatpad's tests: what the test files share. Each file holds a group of
//	tests, and main.cpp runs the groups.
//
#ifndef TESTS_INCLUDED
#define TESTS_INCLUDED

// Windows-only tests use the Win32 API; everything else is portable
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../src/TextDocument/sequence.h"
#include "../src/TextDocument/SaveFile.h"
#include "../src/TextDocument/TextDocument.h"
#include "../src/TextDocument/Unicode.h"

// Test text in the build's text unit, tchar_t: UTF-16 on Windows, UTF-8
// elsewhere. Only ASCII belongs in a TSTR; tstr() makes the rest.
#ifdef _WIN32
#define TSTR(s) L##s
#else
#define TSTR(s) s
#endif

typedef std::basic_string<tchar_t> tstring;

inline size_t tstrlen(const tchar_t *text)
{
    return std::char_traits<tchar_t>::length(text);
}

// Room for a temporary file's name, in the platform's own form (seqfilechar)
const size_t TEST_PATH_MAX = 1024;

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

    static uint32_t header(TextDocument &doc)
    {
        return static_cast<uint32_t>(doc.m_nHeaderSize);
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

// Count a check that fails, and say where it is
extern int g_failures;

void check(bool condition, const char *expr, const char *file, int line);

#define CHECK(expr) check((expr), #expr, __FILE__, __LINE__)

// A test, and a file's group of them
struct test_case
{
    const char *name;
    void (*run)();
};

struct test_group
{
    const char      *name;
    const test_case *tests;
    size_t           count;
};

// The groups, in the order they run
extern const test_group sequence_tests;
extern const test_group line_tests;
extern const test_group char_tests;
extern const test_group document_tests;
extern const test_group textview_tests;

//
//	Helpers more than one group uses (test_helpers.cpp)
//

void init(sequence &seq, const char *text);

bool insert_bytes(sequence &seq, size_w index, const char *text);

bool replace_bytes(sequence &seq, size_w index, const char *text, size_w erase_length);

// A position's line number, and whether it's exact rather than pending
size_w doc_lineno(TextDocument &doc, TextPos pos, bool *exact);

// A new, empty temporary file
bool temp_file(seqfilechar path[TEST_PATH_MAX]);

bool write_temp_file(const char *data, size_t length, seqfilechar path[TEST_PATH_MAX]);

bool delete_file(const seqfilechar *path);

// fopen, for a path in the platform's own form
FILE *open_file(const std::filesystem::path &path, const char *mode);

// A file just over 4GB: text at the start and the end, and zeros between them
const unsigned long long LARGE_FILE_SIZE = 0x100000000ULL + 0x100000;
const char LARGE_FILE_HEAD[] = "first line\r\n";
const char LARGE_FILE_TAIL[] = "\r\nsecond to last\r\nlast line";

// Write such a file sparsely, so only the text takes disk space. Returns false
// where the volume can't make sparse files; the tests that need one then skip.
bool write_sparse_file(const char *head, const char *tail, unsigned long long size, seqfilechar path[TEST_PATH_MAX]);

enum DocTestEncoding
{
    DOC_ASCII,
    DOC_UTF8_BOM,
    DOC_UTF16LE_BOM,
    DOC_UTF16BE_BOM
};

bool write_textdocument_file(const char *text, DocTestEncoding encoding, seqfilechar path[TEST_PATH_MAX]);

std::string render_content(const sequence &seq);

void expect_content(const sequence &seq, const char *expected);

void expect_offset_from_lineno(const sequence &seq, size_w line, size_w expected);

// Bytes per character of the plain test text in each encoding
uint32_t unit_size(DocTestEncoding encoding);

// Edits at document byte offsets
size_w doc_insert(TextDocument &doc, size_w offset, const tchar_t *text, uint32_t length, TextChange *change = 0);

size_w doc_erase(TextDocument &doc, size_w from, size_w to, TextChange *change = 0);

size_w doc_replace(TextDocument &doc, size_w from, size_w to, const tchar_t *text, uint32_t length, TextChange *change = 0);

#ifdef _WIN32
// Whether another program could open a file to write it now
bool others_can_write(const seqfilechar *path);
#endif

size_w utf8_chars(const std::string &text, size_t length);

// a, e-acute, the euro sign, U+1F600 and CRLF repeated: characters of each
// UTF-8 length, 12 bytes at a time, so 64 KB page boundaries cut characters
std::string utf8_text(size_t length);

// Text as a file in an encoding: UTF-8 without a byte-order mark, UTF-16 or
// UTF-32 with one
std::string encode_text(const char32_t *text, TEXT_ENCODING encoding);

#ifdef _WIN32
std::string encode_text(const wchar_t *text, TEXT_ENCODING encoding);
#endif

// Text in the build's text unit, from code points: tstr(U"caf\u00e9")
tstring tstr(const char32_t *text);

#endif
