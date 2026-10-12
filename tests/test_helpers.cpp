//
//	Helpers more than one group of tests uses
//
#include "tests.h"

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

// Bytes per character of the plain test text in each encoding
ULONG unit_size(DocTestEncoding encoding)
{
    return encoding == DOC_UTF16LE_BOM || encoding == DOC_UTF16BE_BOM ? 2 : 1;
}

// Edits at document byte offsets
size_w doc_insert(TextDocument &doc, size_w offset, const TCHAR *text, ULONG length, TextChange *change)
{
    return doc.insert_text(TextPos(offset), text, length, change);
}

size_w doc_erase(TextDocument &doc, size_w from, size_w to, TextChange *change)
{
    return doc.erase_text(TextPos(from), TextPos(to), change);
}

size_w doc_replace(TextDocument &doc, size_w from, size_w to, const TCHAR *text, ULONG length, TextChange *change)
{
    return doc.replace_text(TextPos(from), TextPos(to), text, length, change);
}

// Whether another program could open a file to write it now
bool others_can_write(const TCHAR *path)
{
    HANDLE writer = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, 0, 0);

    if(writer == INVALID_HANDLE_VALUE)
        return false;

    CloseHandle(writer);
    return true;
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
