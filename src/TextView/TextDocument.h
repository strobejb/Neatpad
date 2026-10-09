#pragma once

#ifndef NEATPAD_TEXTDOCUMENT_INCLUDED
#define NEATPAD_TEXTDOCUMENT_INCLUDED

#include <stdint.h>
#include "codepages.h"
#include "sequence.h"

//
//	A unit of the UTF-16 text TextDocument reads and writes. On Windows it is a
//	wchar_t, so the view's WCHAR buffers pass straight in; elsewhere it is a
//	char16_t, which is what Qt's QString holds.
//
#ifdef _WIN32
typedef wchar_t	 textchar;
#else
typedef char16_t textchar;
#endif

static_assert(sizeof(textchar) == 2, "textchar must be a UTF-16 code unit");

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
	size_w byte_anchor;

	// Exact byte bounds for the containing physical line, in the same coordinate
	// space as byte_anchor. These avoid rediscovering the local line span when
	// moving/rendering around the anchor.
	size_w line_begin;
	size_w line_next;

	// Exact UTF-16 code-unit offset from line_begin to byte_anchor.
	size_w line_offset_chars; 
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
	size_w offset;
	size_w erased;
	size_w inserted;
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

	uint32_t read(textchar *buf, uint32_t buflen);

	operator bool();

private:
	friend class TextDocument;

	TextReader(size_w off, size_w len, TextDocument *td);

	TextDocument *text_doc;

	size_w off_bytes;
	size_w len_bytes;
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
	bool  init(const seqfilechar *filename);

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
	size_w	insert_text  (const TextCoord *at, textchar *text, uint32_t length, TextChange *change);
	size_w	replace_text (const TextCoord *from, const TextCoord *to, textchar *text, uint32_t length, TextChange *change);
	size_w	erase_text   (const TextCoord *from, const TextCoord *to, TextChange *change);

	//
	//	Coordinates
	//
	bool  coord_from_byte_anchor(size_w byte_anchor, TextCoord *coord);
	bool  coord_from_document_end(TextCoord *coord);
	bool  coord_from_line_pos(TextCoord *line, size_w line_offset_chars, TextCoord *coord);
	bool  previous_line_from_coord(TextCoord *coord, uint32_t num_lines, TextCoord *line);
	bool  next_line_from_coord(TextCoord *coord, uint32_t num_lines, TextCoord *line);

	// Move a coordinate across a change and refresh its line bounds. A coordinate
	// exactly at the change offset stays before any inserted text.
	bool  coord_after_change(TextCoord *coord, const TextChange *change);

	// Line numbers are a query, never stored in a coordinate. Returns an estimate when *exact is false.
	size_w lineno_from_coord(const TextCoord *coord, bool *exact);

	// UTF-16 offset of a coordinate from the start of the document. Only fixed-width
	// encodings can answer without scanning the file; the others return false.
	bool  charoffset_from_coord(const TextCoord *coord, size_w *offset_chars);

	// The line break ending a coordinate's line: TXL_CRLF, TXL_LF or TXL_CR, or 0 for a last line without one.
	uint32_t linebreak_from_coord(const TextCoord *line);

	//
	//	Text access
	//
	TextReader text_from_range(const TextCoord *from, const TextCoord *to);
	uint32_t getline(TextCoord &coord, textchar *buf, uint32_t buflen);
	bool  find_text(textchar *text, uint32_t length, size_w from, size_w to, bool backwards, bool matchcase, size_w *match, size_w *match_length);

	//
	//	Document facts
	//

	// Document-wide facts. linecount() may be estimated until linecount_known() is true.
	TEXT_ENCODING getformat();
	size_w linecount();
	bool  linecount_known();

	// The kinds of line break found so far (TXL_CRLF, TXL_CR, TXL_LF bits). Covers the
	// part of the document with exact line numbers, so the whole of it once linecount_known().
	uint32_t linebreaks_seen();

	size_w byte_length();
	size_w file_length();

	// Length of the document in UTF-16 units, where that is cheap (as charoffset_from_coord).
	bool  charcount(size_w *chars);

private:

	//
	//	The smaller and larger of two values of the same type. Not min() and max(),
	//	which windows.h defines as macros; a mix of types won't compile, rather than
	//	comparing signed with unsigned.
	//
	template <class T> static T min_of(T a, T b) { return a < b ? a : b; }
	template <class T> static T max_of(T a, T b) { return a < b ? b : a; }

	//
	//	Line navigation
	//
	bool unit_before(size_w offset_bytes, uint32_t *ch, uint32_t *size);
	bool line_bounds_from_offset(size_w offset_bytes, size_w *line_begin, size_w *line_next);
	void coord_in_line(size_w line_begin, size_w line_next, size_w line_offset_chars, TextCoord *coord);
	bool find_text_nocase(textchar *text, uint32_t length, size_w from, size_w to, bool backwards, size_w *match, size_w *match_length);

	//
	//	UTF-16 / backing-byte conversion
	//
	size_w count_chars(size_w offset_bytes, size_w length_chars);
	size_w count_code_units(size_w offset_bytes, size_w length_bytes);
	bool   charoffset_from_bytes(size_w offset_bytes, size_w *offset_chars);

	size_t utf16_to_rawdata(textchar *utf16str, size_t utf16len, uint8_t *rawdata, size_t *rawlen);
	size_t rawdata_to_utf16(uint8_t *rawdata, size_t rawlen, textchar *utf16str, size_t *utf16len);
	size_t rawdata_complete_length(uint8_t *rawdata, size_t rawlen);

	//
	//	Raw file access
	//

	TEXT_ENCODING detect_file_format(int *headersize);
	size_w decode_text(size_w offset_bytes, size_w lenbytes, textchar *buf, uint32_t *len);
	size_w decode_char(size_w offset_bytes, size_t lenbytes, uint32_t *pch32);
	size_w decode_prev_char(size_w offset_bytes, size_w lower_bound, uint32_t *pch32);

	//
	//	Raw editing
	//

	size_w	insert_raw(size_w offset_bytes, textchar *text, uint32_t length);
	size_w	replace_raw(size_w offset_bytes, textchar *text, uint32_t length, size_w erase_bytes);
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
