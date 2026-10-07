#pragma once

#ifndef NEATPAD_TEXTDOCUMENT_INCLUDED
#define NEATPAD_TEXTDOCUMENT_INCLUDED

#include <windows.h>
#include "codepages.h"
#include "sequence.h"

class TextDocument;
class TextDocumentLineIndexProbe;
class TextLineMapper;

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

// Physical document line. offset_chars/length_chars are exact UTF-16 document
// coordinates; index can be provisional until index_known is true.
struct DocLine
{
	ULONG index;
	bool  index_known;
	ULONG offset_chars;
	ULONG length_chars;
};

struct TextCoord
{
	// The start of the document (and the only position in an empty one)
	TextCoord()
		: byte_anchor(0), line_begin(0), line_next(0), line_offset_chars(0), offset_chars(0)
	{
		line.index = 0;
		line.index_known = true;
		line.offset_chars = 0;
		line.length_chars = 0;
	}

	// Exact byte position in the document payload, excluding any BOM/header.
	ULONG byte_anchor;

	// Exact byte bounds for the containing physical line, in the same coordinate
	// space as byte_anchor. These avoid rediscovering the local line span when
	// moving/rendering around the anchor.
	ULONG line_begin;
	ULONG line_next;

	// Exact UTF-16 code-unit offset from line_begin to byte_anchor.
	ULONG line_offset_chars;

	// Compatibility mirrors while TextView is migrated away from global
	// character offsets and absolute line numbers.
	ULONG offset_chars;
	DocLine line;
};

struct TextRange
{
	TextCoord anchor;
	TextCoord active;
};

// One change to the document, in document bytes (excluding any BOM/header):
// bytes [offset, offset + erased) were replaced by [offset, offset + inserted).
struct TextChange
{
	ULONG offset;
	ULONG erased;
	ULONG inserted;
};

// Byte-backed reader which decodes UTF-16 text for callers.
class TextReader
{
public:
	// default constructor sets all members to zero
	TextReader();

	// default copy-constructor
	TextReader(const TextReader &reader);

	// assignment operator
	TextReader & operator= (const TextReader &reader);

	ULONG read(TCHAR *buf, ULONG buflen);

	operator bool();

private:
	friend class TextDocument;

	TextReader(ULONG off, ULONG len, TextDocument *td);

	TextDocument *text_doc;
	
	ULONG off_bytes;
	ULONG len_bytes;
};

class TextDocument
{
	friend class TextReader;
	friend class TextDocumentLineIndexProbe;
	friend class TextLineMapper;

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
	bool	undo(TextChange *change);
	bool	redo(TextChange *change);
	bool	undo(ULONG *offset_start, ULONG *offset_end, TextChange *change = 0);
	bool	redo(ULONG *offset_start, ULONG *offset_end, TextChange *change = 0);
	bool	can_undo();
	bool	can_redo();
	void	undo_group_begin();
	void	undo_group_end();
	void	undo_group_break();

	// Text-editing interface. Callers use character offsets; TextDocument maps them to backing bytes.
	// Each edit can report the bytes it changed, so callers can move their TextCoords across it.
	ULONG	insert_text  (ULONG offset_chars, TCHAR *text, ULONG length, TextChange *change = 0);
	ULONG	replace_text (ULONG offset_chars, TCHAR *text, ULONG length, ULONG erase_len, TextChange *change = 0);
	ULONG	erase_text   (ULONG offset_chars, ULONG length, TextChange *change = 0);

	// The same edits by coordinate. A range is [from, to) in either order; each returns
	// the bytes it inserted (insert/replace) or erased (erase).
	ULONG	insert_text  (const TextCoord *at, TCHAR *text, ULONG length, TextChange *change);
	ULONG	replace_text (const TextCoord *from, const TextCoord *to, TCHAR *text, ULONG length, TextChange *change);
	ULONG	erase_text   (const TextCoord *from, const TextCoord *to, TextChange *change);

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
	bool  line_from_offset(ULONG offset_chars, DocLine *line);
	bool  line_from_index(ULONG index, DocLine *line);
	bool  previous_line_from_offset(ULONG offset_chars, ULONG num_lines, DocLine *line);
	bool  next_line_from_offset(ULONG offset_chars, ULONG num_lines, DocLine *line);
	bool  coord_from_offset(ULONG offset_chars, TextCoord *coord);
	bool  coord_from_byte_anchor(ULONG byte_anchor, TextCoord *coord);
	bool  coord_from_document_end(TextCoord *coord);
	bool  coord_from_line_offset(DocLine *line, ULONG offset_chars, TextCoord *coord);
	bool  coord_from_line_pos(TextCoord *line, ULONG line_offset_chars, TextCoord *coord);
	bool  previous_line_from_coord(TextCoord *coord, ULONG num_lines, TextCoord *line);
	bool  next_line_from_coord(TextCoord *coord, ULONG num_lines, TextCoord *line);

	// Move a coordinate across a change and refresh its line bounds. A coordinate
	// exactly at the change offset stays before any inserted text.
	bool  coord_after_change(TextCoord *coord, const TextChange *change);

	// Line numbers are a query, never stored in a coordinate. Returns an estimate when *exact is false.
	ULONG lineno_from_coord(const TextCoord *coord, bool *exact);

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
	TextReader text_from_range(const TextCoord *from, const TextCoord *to);

	ULONG getline(ULONG nLineNo, TCHAR *buf, ULONG buflen, ULONG *off_chars);
	ULONG getline(DocLine &line, TCHAR *buf, ULONG buflen, ULONG *off_chars);
	ULONG getline(TextCoord &coord, TCHAR *buf, ULONG buflen, ULONG *off_chars);

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

	void update_text_length();
	bool raw_lineinfo_from_offset(ULONG offset_chars, RawLineInfo *lineinfo);
	bool raw_lineinfo_from_lineno(ULONG lineno, RawLineInfo *lineinfo);
	bool previous_raw_lineinfo_from_offset(ULONG offset_chars, ULONG num_lines, RawLineInfo *lineinfo);
	bool next_raw_lineinfo_from_offset(ULONG offset_chars, ULONG num_lines, RawLineInfo *lineinfo);
	bool raw_offset_ends_with_linebreak(ULONG offset_bytes);
	void copy_line(RawLineInfo *source, DocLine *dest);
	void copy_coord(RawLineInfo *source, ULONG offset_chars, TextCoord *dest);

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
	ULONG	replace_raw(ULONG offset_bytes, TCHAR *text, ULONG length, ULONG erase_bytes);
	ULONG	erase_raw(ULONG offset_bytes, ULONG length);
	void	event_change(TextChange *change);


	// Raw byte storage.
	sequence m_seq;

	// Cached document length. This is UTF-16 for decoded documents, and provisional for large lazy UTF-8 files.
	ULONG  m_nDocLength_chars;

	TEXT_ENCODING m_nFileFormat;
	int    m_nHeaderSize;
};

class LineIterator
{
public:
	LineIterator();

private:
	TextDocument *m_pTextDoc;
};

#endif
