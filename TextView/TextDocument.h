#ifndef TEXTDOC_INCLUDED
#define TEXTDOC_INCLUDED

#include "codepages.h"
#include "sequence.h"

class TextIterator;

// Document/view boundary object: line number plus both logical-character and raw-byte range.
struct TextLineInfo
{
	ULONG lineno;
	ULONG lineoff_chars;
	ULONG linelen_chars;
	ULONG lineoff_bytes;
	ULONG linelen_bytes;
};

class TextDocument
{
	friend class TextIterator;
	friend class TextView;

public:
	TextDocument();
	~TextDocument();

	// Load and reset the document.
	bool  init(HANDLE hFile);
	bool  init(TCHAR *filename);
	
	bool  clear();
	bool EmptyDoc();

	// Undo/redo report the changed character range.
	bool	Undo(ULONG *offset_start, ULONG *offset_end);
	bool	Redo(ULONG *offset_start, ULONG *offset_end);

	// Text-editing interface. Callers use character offsets; TextDocument maps them to storage bytes.
	ULONG	insert_text(ULONG offset_chars, TCHAR *text, ULONG length);
	ULONG	replace_text(ULONG offset_chars, TCHAR *text, ULONG length, ULONG erase_len);
	ULONG	erase_text(ULONG offset_chars, ULONG length);

	// Line/offset lookup. Line numbers may be estimated until the lazy line index reaches that range.
	ULONG lineno_from_offset(ULONG offset);
	ULONG offset_from_lineno(ULONG lineno);

	// Query line ranges. The TextLineInfo overload is preferred for new code.
	bool  lineinfo_from_offset(ULONG offset_chars, ULONG *lineno, ULONG *lineoff_chars,  ULONG *linelen_chars, ULONG *lineoff_bytes, ULONG *linelen_bytes);
	bool  lineinfo_from_lineno(ULONG lineno,                      ULONG *lineoff_chars,  ULONG *linelen_chars, ULONG *lineoff_bytes, ULONG *linelen_bytes);	
	bool  lineinfo_from_offset(ULONG offset_chars, TextLineInfo *lineinfo);

	// Move by physical lines from a character offset, capped at the document ends.
	bool  previous_lineinfo_from_offset(ULONG offset_chars, ULONG num_lines, TextLineInfo *lineinfo);
	bool  next_lineinfo_from_offset(ULONG offset_chars, ULONG num_lines, TextLineInfo *lineinfo);

	// Text access helpers used by TextView layout and painting.
	TextIterator iterate(ULONG offset);
	TextIterator iterate_line(ULONG lineno, ULONG *linestart = 0, ULONG *linelen = 0);
	TextIterator iterate_line_offset(ULONG offset_chars, ULONG *lineno, ULONG *linestart = 0);

	ULONG getdata(ULONG offset, BYTE *buf, size_t len);
	ULONG getline(ULONG nLineNo, TCHAR *buf, ULONG buflen, ULONG *off_chars);

	// Document-wide facts. linecount() may be estimated until linecount_known() is true.
	int   getformat();
	ULONG linecount();
	bool  linecount_known();

	// Lazy line-index state. Unknown line numbers should not be shown as exact UI facts.
	bool  lineno_known(ULONG lineno);
	bool  line_numbers_known(ULONG offset_chars, ULONG length_chars);
	void  index_lines(ULONG offset_chars, ULONG length_chars);

	ULONG longestline(int tabwidth);
	ULONG size();

private:
	
	bool init_linebuffer();
	bool use_sequence_linebuffer() const;

	ULONG charoffset_to_byteoffset(ULONG offset_chars);
	ULONG byteoffset_to_charoffset(ULONG offset_bytes);

	ULONG count_chars(ULONG offset_bytes, ULONG length_chars);

	size_t utf16_to_rawdata(TCHAR *utf16str, size_t utf16len, BYTE *rawdata, size_t *rawlen);
	size_t rawdata_to_utf16(BYTE *rawdata, size_t rawlen, TCHAR *utf16str, size_t *utf16len);

	int   detect_file_format(int *headersize);
	ULONG	  gettext(ULONG offset, ULONG lenbytes, TCHAR *buf, ULONG *len);
	int   getchar(ULONG offset, ULONG lenbytes, ULONG *pch32);

	// UTF-16 text-editing interface
	ULONG	insert_raw(ULONG offset_bytes, TCHAR *text, ULONG length);
	ULONG	replace_raw(ULONG offset_bytes, TCHAR *text, ULONG length, ULONG erase_len);
	ULONG	erase_raw(ULONG offset_bytes, ULONG length);


	sequence m_seq;

	ULONG  m_nDocLength_chars;
	ULONG  m_nDocLength_bytes;

	ULONG *m_pLineBuf_byte;
	ULONG *m_pLineBuf_char;
	ULONG  m_nNumLines;
	
	int	   m_nFileFormat;
	int    m_nHeaderSize;
};

class TextIterator
{
public:
	// default constructor sets all members to zero
	TextIterator()
		: text_doc(0), off_bytes(0), len_bytes(0)
	{
	}

	TextIterator(ULONG off, ULONG len, TextDocument *td)
		: text_doc(td), off_bytes(off), len_bytes(len)
	{
		
	}

	// default copy-constructor
	TextIterator(const TextIterator &ti) 
		: text_doc(ti.text_doc), off_bytes(ti.off_bytes), len_bytes(ti.len_bytes)
	{
	}

	// assignment operator
	TextIterator & operator= (TextIterator &ti)
	{
		text_doc  = ti.text_doc;
		off_bytes = ti.off_bytes;
		len_bytes = ti.len_bytes;
		return *this;
	}

	ULONG gettext(TCHAR *buf, ULONG buflen)
	{
		if(text_doc)
		{
			// get text from the TextDocument at the specified byte-offset
			ULONG len = text_doc->gettext(off_bytes, len_bytes, buf, &buflen);

			// adjust the iterator's internal position
			off_bytes += len;
			len_bytes -= len;

			return buflen;
		}
		else
		{
			return 0;
		}
	}

	/*int insert_text(TCHAR *buf, int buflen)
	{
		if(text_doc)
		{
			// get text from the TextDocument at the specified byte-offset
			int len = text_doc->insert(off_bytes, buf, buflen);

			// adjust the iterator's internal position
			off_bytes += len;
			return buflen;
		}
		else
		{
			return 0;
		}
	}

	int replace_text(TCHAR *buf, int buflen)
	{
		if(text_doc)
		{
			// get text from the TextDocument at the specified byte-offset
			int len = text_doc->replace(off_bytes, buf, buflen);

			// adjust the iterator's internal position
			off_bytes += len;
			return buflen;
		}
		else
		{
			return 0;
		}
	}

	int erase_text(int length)
	{
		if(text_doc)
		{
			// get text from the TextDocument at the specified byte-offset
			int len = text_doc->erase(off_bytes, length);

			// adjust the iterator's internal position
			off_bytes += len;
			return len;
		}
		else
		{
			return 0;
		}
	}*/


	operator bool()
	{
		return text_doc ? true : false;
	}

private:

	TextDocument *text_doc;
	
	ULONG off_bytes;
	ULONG len_bytes;
};

class LineIterator
{
public:
	LineIterator();

private:
	TextDocument *m_pTextDoc;
};

struct _BOM_LOOKUP
{
	DWORD  bom;
	ULONG  len;
	int    type;
};

#endif
