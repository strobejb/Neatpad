#ifndef TEXTDOC_INCLUDED
#define TEXTDOC_INCLUDED

#include "codepages.h"
#include "sequence.h"
#include "TextLineIndex.h"

class TextReader;

//
//	TextDocument is the boundary between the view and the storage engine.
//
//	TextView works in UTF-16 code units: caret offsets, selections, line
//	lengths and decoded text all use character offsets. TextDocument converts
//	those requests to the byte offsets used by the backing sequence.
//
//	sequence stores raw bytes. It does not know about the display encoding,
//	the caret, or the TextView line layout.
//

// Document/view boundary object: line number plus UTF-16 range.
struct TextLineInfo
{
	ULONG lineno;
	ULONG lineoff_chars;
	ULONG linelen_chars;
};

class TextDocument
{
	friend class TextReader;
	friend class TextLineIndex;

public:
	TextDocument();
	~TextDocument();

	//
	//	Lifetime
	//

	// Load and reset the document.
	bool  init(TCHAR *filename);
	
	bool  clear();

	//
	//	Editing
	//
	bool	undo(ULONG *offset_start, ULONG *offset_end);
	bool	redo(ULONG *offset_start, ULONG *offset_end);
	bool	can_undo();
	bool	can_redo();
	void	undo_group_begin();
	void	undo_group_end();
	void	undo_group_break();

	// Text-editing interface. Callers use character offsets; TextDocument maps them to backing bytes.
	ULONG	insert_text  (ULONG offset_chars, TCHAR *text, ULONG length);
	ULONG	replace_text (ULONG offset_chars, TCHAR *text, ULONG length, ULONG erase_len);
	ULONG	erase_text   (ULONG offset_chars, ULONG length);

	//
	//	Line lookup
	//

	// Line/offset lookup. Offsets and lengths describe the resolved physical text.
	// Returned line numbers can be provisional until lineno_known() says otherwise.
	ULONG lineno_from_offset(ULONG offset);
	ULONG offset_from_lineno(ULONG lineno);

	// Query line ranges. Character offsets are exact; line numbers can be provisional in lazy regions.
	bool  lineinfo_from_offset(ULONG offset_chars, ULONG *lineno, ULONG *lineoff_chars,  ULONG *linelen_chars);
	bool  lineinfo_from_lineno(ULONG lineno,                      ULONG *lineoff_chars,  ULONG *linelen_chars);
	bool  lineinfo_from_offset(ULONG offset_chars, TextLineInfo *lineinfo);
	ULONG line_text_end(TextLineInfo *lineinfo);

	// Move by physical lines from a character offset, capped at the document ends.
	bool  previous_lineinfo_from_offset(ULONG offset_chars, ULONG num_lines, TextLineInfo *lineinfo);
	bool  next_lineinfo_from_offset(ULONG offset_chars, ULONG num_lines, TextLineInfo *lineinfo);

	//
	//	Text access
	//

	// Text access helpers used by TextView layout and painting.
	TextReader text_from_offset(ULONG offset_chars);
	TextReader text_from_line(ULONG lineno, ULONG *linestart = 0, ULONG *linelen = 0);

	ULONG getline(ULONG nLineNo, TCHAR *buf, ULONG buflen, ULONG *off_chars);

	//
	//	Document facts
	//

	// Document-wide facts. linecount() may be estimated until linecount_known() is true.
	TEXT_ENCODING getformat();
	ULONG linecount();
	bool  linecount_known();

	// Lazy line-index state. Unknown line numbers should be hidden or marked provisional by the UI.
	bool  lineno_known(ULONG lineno);
	bool  line_number_range_known(ULONG offset_chars, ULONG length_chars);
	void  index_lines(ULONG offset_chars, ULONG length_chars);

	ULONG longestline(int tabwidth);
	ULONG text_length();

private:

	//
	//	Line indexing
	//

	struct RawLineInfo
	{
		ULONG lineno;
		ULONG lineoff_chars;
		ULONG linelen_chars;
		ULONG lineoff_bytes;
		ULONG linelen_bytes;
		bool  chars_known;
	};

	bool init_line_index();
	void copy_lineindex_info(TextLineIndexInfo *source, RawLineInfo *dest);
	bool raw_lineinfo_from_offset(ULONG offset_chars, RawLineInfo *lineinfo);
	bool raw_lineinfo_from_lineno(ULONG lineno, RawLineInfo *lineinfo);

	//
	//	UTF-16 / backing-byte conversion
	//

	ULONG charoffset_to_byteoffset(ULONG offset_chars);
	ULONG byteoffset_to_charoffset(ULONG offset_bytes);

	ULONG count_chars(ULONG offset_bytes, ULONG length_chars);
	ULONG count_code_units(ULONG offset_bytes, ULONG length_bytes);

	size_t utf16_to_rawdata(TCHAR *utf16str, size_t utf16len, BYTE *rawdata, size_t *rawlen);
	size_t rawdata_to_utf16(BYTE *rawdata, size_t rawlen, TCHAR *utf16str, size_t *utf16len);

	//
	//	Raw file access
	//

	TEXT_ENCODING detect_file_format(int *headersize);
	ULONG decode_text(ULONG offset_bytes, ULONG lenbytes, TCHAR *buf, ULONG *len);
	int   decode_char(ULONG offset_bytes, ULONG lenbytes, ULONG *pch32);

	//
	//	Raw editing
	//

	ULONG	insert_raw(ULONG offset_bytes, TCHAR *text, ULONG length);
	ULONG	replace_raw(ULONG offset_bytes, TCHAR *text, ULONG length, ULONG erase_len);
	ULONG	erase_raw(ULONG offset_bytes, ULONG length);


	// Raw byte storage.
	sequence m_seq;

	// Cached document length. This is UTF-16 for decoded documents, and provisional for large lazy UTF-8 files.
	ULONG  m_nDocLength_chars;

	// Lazy document line index. Offsets are stored in TextDocument's UTF-16 coordinate space.
	TextLineIndex m_lineIndex;
	
	TEXT_ENCODING m_nFileFormat;
	int    m_nHeaderSize;
};

// Byte-backed reader which decodes UTF-16 text for callers.
class TextReader
{
public:
	// default constructor sets all members to zero
	TextReader()
		: text_doc(0), off_bytes(0), len_bytes(0)
	{
	}

	// default copy-constructor
	TextReader(const TextReader &reader)
		: text_doc(reader.text_doc), off_bytes(reader.off_bytes), len_bytes(reader.len_bytes)
	{
	}

	// assignment operator
	TextReader & operator= (const TextReader &reader)
	{
		text_doc  = reader.text_doc;
		off_bytes = reader.off_bytes;
		len_bytes = reader.len_bytes;
		return *this;
	}

	ULONG read(TCHAR *buf, ULONG buflen)
	{
		if(text_doc)
		{
			// get text from the TextDocument at the specified byte-offset
			ULONG len = text_doc->decode_text(off_bytes, len_bytes, buf, &buflen);

			// adjust the reader's internal position
			off_bytes += len;
			len_bytes -= len;

			return buflen;
		}
		else
		{
			return 0;
		}
	}

	operator bool()
	{
		return text_doc ? true : false;
	}

private:
	friend class TextDocument;

	TextReader(ULONG off, ULONG len, TextDocument *td)
		: text_doc(td), off_bytes(off), len_bytes(len)
	{
	}

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

#endif
