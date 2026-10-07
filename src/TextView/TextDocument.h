#pragma once

#ifndef NEATPAD_TEXTDOCUMENT_INCLUDED
#define NEATPAD_TEXTDOCUMENT_INCLUDED

#include <windows.h>
#include "codepages.h"
#include "sequence.h"

class TextDocument;
class TextDocumentLineIndexProbe;

//
//	TextDocument is the boundary between the view and the storage engine.
//
//	Positions are TextCoords: byte offsets into the document, plus the UTF-16
//	position within their own line that TextView lays out. Text is decoded to
//	UTF-16 for the view and encoded back to the file's format on edit.
//
//	sequence stores raw bytes. It does not know about the display encoding,
//	the caret, or the TextView line layout.
//

// A position in the document. Everything is local to the position's own line, so a
// coordinate never needs global line numbers or character offsets: those are queries.
struct TextCoord
{
	// The start of the document (and the only position in an empty one)
	TextCoord()
		: byte_anchor(0), line_begin(0), line_next(0), line_offset_chars(0)
	{
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
	bool	can_undo();
	bool	can_redo();
	void	undo_group_begin();
	void	undo_group_end();
	void	undo_group_break();

	// Edits by coordinate. A range is [from, to) in either order; each returns the bytes
	// it inserted (insert/replace) or erased (erase), and can report the bytes it changed
	// so callers can move their TextCoords across it.
	ULONG	insert_text  (const TextCoord *at, TCHAR *text, ULONG length, TextChange *change);
	ULONG	replace_text (const TextCoord *from, const TextCoord *to, TCHAR *text, ULONG length, TextChange *change);
	ULONG	erase_text   (const TextCoord *from, const TextCoord *to, TextChange *change);

	//
	//	Coordinates
	//
	bool  coord_from_byte_anchor(ULONG byte_anchor, TextCoord *coord);
	bool  coord_from_document_end(TextCoord *coord);
	bool  coord_from_line_pos(TextCoord *line, ULONG line_offset_chars, TextCoord *coord);
	bool  previous_line_from_coord(TextCoord *coord, ULONG num_lines, TextCoord *line);
	bool  next_line_from_coord(TextCoord *coord, ULONG num_lines, TextCoord *line);

	// Move a coordinate across a change and refresh its line bounds. A coordinate
	// exactly at the change offset stays before any inserted text.
	bool  coord_after_change(TextCoord *coord, const TextChange *change);

	// Line numbers are a query, never stored in a coordinate. Returns an estimate when *exact is false.
	ULONG lineno_from_coord(const TextCoord *coord, bool *exact);

	// UTF-16 offset of a coordinate from the start of the document. Only fixed-width
	// encodings can answer without scanning the file; the others return false.
	bool  charoffset_from_coord(const TextCoord *coord, ULONG *offset_chars);

	// The line break ending a coordinate's line: TXL_CRLF, TXL_LF or TXL_CR, or 0 for a last line without one.
	ULONG linebreak_from_coord(const TextCoord *line);

	//
	//	Text access
	//
	TextReader text_from_range(const TextCoord *from, const TextCoord *to);
	ULONG getline(TextCoord &coord, TCHAR *buf, ULONG buflen);

	//
	//	Document facts
	//

	// Document-wide facts. linecount() may be estimated until linecount_known() is true.
	TEXT_ENCODING getformat();
	ULONG linecount();
	bool  linecount_known();

	ULONG byte_length();

private:

	//
	//	Line navigation
	//
	bool unit_before(ULONG offset_bytes, ULONG *ch, ULONG *size);
	bool line_bounds_from_offset(ULONG offset_bytes, ULONG *line_begin, ULONG *line_next);
	void coord_in_line(ULONG line_begin, ULONG line_next, ULONG line_offset_chars, TextCoord *coord);

	//
	//	UTF-16 / backing-byte conversion
	//
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
	void	event_change(TextChange *change);


	// Raw byte storage.
	sequence m_seq;

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
