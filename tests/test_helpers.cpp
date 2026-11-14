//
//	Helpers more than one group of tests uses
//
#include "tests.h"

#ifndef _WIN32
#include <stdlib.h>
#include <unistd.h>
#endif

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

// A new, empty temporary file
bool temp_file(seqfilechar path[TEST_PATH_MAX])
{
    path[0] = 0;

#ifdef _WIN32
    seqfilechar temp_path[TEST_PATH_MAX];

    if(GetTempPath(static_cast<DWORD>(TEST_PATH_MAX), temp_path) == 0)
        return false;

    return GetTempFileName(temp_path, L"seq", 0, path) != 0;
#else
    std::string name = (std::filesystem::temp_directory_path() / "seqXXXXXX").string();
    int         fd;

    if(name.size() >= TEST_PATH_MAX)
        return false;

    strcpy(path, name.c_str());
    fd = mkstemp(path);

    if(fd == -1)
    {
        path[0] = 0;
        return false;
    }

    close(fd);
    return true;
#endif
}

bool write_temp_file(const char *data, size_t length, seqfilechar path[TEST_PATH_MAX])
{
    if(!temp_file(path))
        return false;

    std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);

    file.write(data, static_cast<std::streamsize>(length));
    file.close();

    if(!file)
    {
        delete_file(path);
        path[0] = 0;
        return false;
    }

    return true;
}

bool delete_file(const seqfilechar *path)
{
    std::error_code error;

    return std::filesystem::remove(path, error);
}

// fopen, for a path in the platform's own form
FILE *open_file(const std::filesystem::path &path, const char *mode)
{
#ifdef _WIN32
    std::wstring wide(mode, mode + strlen(mode));
    FILE        *fp = 0;

    _wfopen_s(&fp, path.c_str(), wide.c_str());
    return fp;
#else
    return fopen(path.c_str(), mode);
#endif
}

// Write such a file sparsely, so only the text takes disk space. Returns false
// where the volume can't make sparse files; the tests that need one then skip.
bool write_sparse_file(const char *head, const char *tail, unsigned long long size, seqfilechar path[TEST_PATH_MAX])
{
    if(!temp_file(path))
        return false;

#ifdef _WIN32
    LARGE_INTEGER pos;
    HANDLE hFile;
    DWORD bytes;
    bool ok;

    hFile = CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);

    if(hFile == INVALID_HANDLE_VALUE)
    {
        delete_file(path);
        return false;
    }

    pos.QuadPart = static_cast<LONGLONG>(size - strlen(tail));

    ok = DeviceIoControl(hFile, FSCTL_SET_SPARSE, 0, 0, 0, 0, &bytes, 0) &&
         WriteFile(hFile, head, static_cast<DWORD>(strlen(head)), &bytes, 0) &&
         SetFilePointerEx(hFile, pos, 0, FILE_BEGIN) &&
         WriteFile(hFile, tail, static_cast<DWORD>(strlen(tail)), &bytes, 0);

    CloseHandle(hFile);
#else
    // POSIX file systems leave the gap a seek skips unallocated
    std::ofstream file(path, std::ios::binary | std::ios::trunc);

    file.write(head, static_cast<std::streamsize>(strlen(head)));
    file.seekp(static_cast<std::streamoff>(size - strlen(tail)));
    file.write(tail, static_cast<std::streamsize>(strlen(tail)));
    file.close();

    bool ok = !file.fail();
#endif

    if(!ok)
        delete_file(path);

    return ok;
}

bool write_textdocument_file(const char *text, DocTestEncoding encoding, seqfilechar path[TEST_PATH_MAX])
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

// Bytes per character of the plain test text in each encoding
uint32_t unit_size(DocTestEncoding encoding)
{
    return encoding == DOC_UTF16LE_BOM || encoding == DOC_UTF16BE_BOM ? 2 : 1;
}

// Edits at document byte offsets
size_w doc_insert(TextDocument &doc, size_w offset, const tchar_t *text, uint32_t length, TextChange *change)
{
    return doc.insert_text(TextPos(offset), text, length, change);
}

size_w doc_erase(TextDocument &doc, size_w from, size_w to, TextChange *change)
{
    return doc.erase_text(TextPos(from), TextPos(to), change);
}

size_w doc_replace(TextDocument &doc, size_w from, size_w to, const tchar_t *text, uint32_t length, TextChange *change)
{
    return doc.replace_text(TextPos(from), TextPos(to), text, length, change);
}

#ifdef _WIN32
// Whether another program could open a file to write it now
bool others_can_write(const seqfilechar *path)
{
    HANDLE writer = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, 0, 0);

    if(writer == INVALID_HANDLE_VALUE)
        return false;

    CloseHandle(writer);
    return true;
}
#endif

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

// Text as a file in an encoding: UTF-8 without a byte-order mark, UTF-16 or
// UTF-32 with one
std::string encode_text(const char32_t *text, TEXT_ENCODING encoding)
{
    std::string bytes;

    switch(encoding)
    {
    case NCP_UTF16:
        bytes.assign("\xff\xfe", 2);
        break;

    case NCP_UTF16BE:
        bytes.assign("\xfe\xff", 2);
        break;

    case NCP_UTF32:
        bytes.assign("\xff\xfe\0\0", 4);
        break;

    case NCP_UTF32BE:
        bytes.assign("\0\0\xfe\xff", 4);
        break;

    default:
        break;
    }

    for(; *text; text++)
    {
        UTF32  ch = *text;
        UTF16  units[2];
        UTF8   utf8[4];
        size_t len = 2;

        switch(encoding)
        {
        case NCP_UTF16:
        case NCP_UTF16BE:
            utf32_to_utf16(&ch, 1, units, &len);

            for(size_t i = 0; i < len; i++)
            {
                char lo = static_cast<char>(units[i]);
                char hi = static_cast<char>(units[i] >> 8);

                bytes.push_back(encoding == NCP_UTF16 ? lo : hi);
                bytes.push_back(encoding == NCP_UTF16 ? hi : lo);
            }
            break;

        case NCP_UTF32:
        case NCP_UTF32BE:
            for(int i = 0; i < 4; i++)
                bytes.push_back(static_cast<char>(ch >> (encoding == NCP_UTF32 ? i * 8 : 24 - i * 8)));
            break;

        default:
            len = utf32_to_utf8(utf8, 4, ch);
            bytes.append(reinterpret_cast<const char *>(utf8), len);
            break;
        }
    }

    return bytes;
}

#ifdef _WIN32
std::string encode_text(const wchar_t *text, TEXT_ENCODING encoding)
{
    size_t             length = wcslen(text);
    size_t             count  = length;
    std::vector<UTF32> utf32(length + 1);

    utf16_to_utf32(reinterpret_cast<const UTF16 *>(text), length, &utf32[0], &count);
    utf32[count] = 0;

    return encode_text(reinterpret_cast<const char32_t *>(&utf32[0]), encoding);
}
#endif

// Text in the build's text unit, from code points: tstr(U"caf\u00e9")
tstring tstr(const char32_t *text)
{
    tstring result;

    for(; *text; text++)
    {
        UTF32 ch = *text;

        if(TCHAR_UTF8)
        {
            UTF8   bytes[4];
            size_t len = utf32_to_utf8(bytes, 4, ch);

            for(size_t i = 0; i < len; i++)
                result.push_back(static_cast<tchar_t>(bytes[i]));
        }
        else
        {
            UTF16  units[2];
            size_t len = 2;

            utf32_to_utf16(&ch, 1, units, &len);

            for(size_t i = 0; i < len; i++)
                result.push_back(static_cast<tchar_t>(units[i]));
        }
    }

    return result;
}
