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
class SaveFile;

//
//	TextDocument is the boundary between the view and the storage engine.
//
//	Positions are TextPos: byte offsets into the document's text. A TextCoord is
//	a position with its line cached, as TextView lays it out. Text is decoded to
//	UTF-16 for the view and encoded back to the file's format on edit.
//
//	sequence stores raw bytes. It does not know about the display encoding,
//	the caret, or the TextView line layout.
//

//
//	A position in the document: a byte offset into its text, after any BOM.
//	TextPos always falls on a character boundary. The TextDocument issues TestPos objects;
//	anything made from a raw number (n) has to be explicit with TextPos(n).
//
struct TextPos
{
	TextPos() : byte_offset(0) {}
	explicit TextPos(size_w offset) : byte_offset(offset) {}

	bool operator==(TextPos p) const { return byte_offset == p.byte_offset; }
	bool operator!=(TextPos p) const { return byte_offset != p.byte_offset; }
	bool operator< (TextPos p) const { return byte_offset <  p.byte_offset; }
	bool operator<=(TextPos p) const { return byte_offset <= p.byte_offset; }
	bool operator> (TextPos p) const { return byte_offset >  p.byte_offset; }
	bool operator>=(TextPos p) const { return byte_offset >= p.byte_offset; }

	// The bytes from p up to this position (p must not come after it). There is
	// no pos + n: moving through text is the document's job, so a position can't
	// land part way through a character.
	size_w operator-(TextPos p) const { return byte_offset - p.byte_offset; }

	size_w byte_offset;
};

//
//	A position together with its line, as the view needs it. Everything is local
//	to the position's own line, so a coordinate never needs global line numbers
//	or character offsets: those are queries. Only valid until the document
//	changes; coord_after_change() brings one up to date.
//
struct TextCoord : TextPos
{
	// The start of the document (and the only position in an empty one)
	TextCoord() : column(0) {}

	// The line holding the position, [line_begin, line_next), its line break included
	TextPos line_begin;
	TextPos line_next;

	// UTF-16 code units from line_begin to the position, as the status bar's "Col"
	// and Qt's QTextCursor::columnNumber() count them: a surrogate pair is two,
	// a tab is one. Never between the halves of a pair.
	size_w  column;
};

//
//	Single change event to the document: 
//   the bytes:
//     [at, at + erased) 
//   were replaced by:
//	   [at, at + inserted)
//
struct TextChange
{
	TextPos at;
	size_w  erased;
	size_w  inserted;
};

//
//	Reads a stretch of the document as UTF-16, front to back. Each read ends on
//	a whole character, never between the halves of a surrogate pair. Like a
//	TextCoord, only valid until the document changes.
//
class TextReader
{
public:
	// A reader with nothing to read
	TextReader();

	// Read up to buflen units: returns how many, or 0 at the end. A surrogate pair
	// needs room for 2.
	uint32_t read(textchar *buf, uint32_t buflen);

	TextPos pos() const;		// where the next read starts
	bool    at_end() const;

private:
	friend class TextDocument;

	TextReader(TextDocument *doc, TextPos from, TextPos to);

	TextDocument *m_doc;
	TextPos		  m_pos;
	TextPos		  m_end;
};

class LineIterator;

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

	// Load and reset the document: in the encoding its byte-order mark says
	// (ASCII without one), or in the given encoding.
	bool  init(const seqfilechar *filename);
	bool  init(const seqfilechar *filename, TEXT_ENCODING encoding);

	bool  clear();

	// Write the document to a file, replacing any that is there, in encoding,
	// with or without its byte-order mark (ASCII has none). linebreak - TXL_CR,
	// TXL_LF or TXL_CRLF - makes every line break the same; 0 keeps them.
	//
	// Kept in its own encoding with its line breaks, the document is unchanged,
	// undo history and all, and is unmodified afterwards. Converted, the file no
	// longer matches the document, which is left as it was: open the file again,
	// in the encoding it was written in, to carry on editing it.
	bool  save(const seqfilechar *filename, TEXT_ENCODING encoding, bool bom, uint32_t linebreak);

	// Whether the file has a byte-order mark: as it was opened, then as last saved.
	bool  has_bom();

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

	// Whether the text differs from its file. Undoing or redoing back to the
	// saved text is unmodified again.
	bool	modified();

	//
	// Editing (by position). 
	// A range is [from, to) in either order; each returns the bytes
	// it inserted (insert/replace) or erased (erase), and can report the bytes it changed
	// so callers can move their positions across it.
	size_w	insert_text  (TextPos at,               const textchar *text, uint32_t length, TextChange *change);
	size_w	replace_text (TextPos from, TextPos to, const textchar *text, uint32_t length, TextChange *change);
	size_w	erase_text   (TextPos from, TextPos to, TextChange *change);

	//
	//	Coordinates
	//

	// The coordinate of a position: the one place a position gains its line.
	bool  coord_from_pos(TextPos pos, TextCoord *coord);
	bool  coord_from_document_end(TextCoord *coord);

	// The start of a line, counted from 0. A line past the indexed part of the
	// document is TDR_PENDING_INDEX (one a little past it is indexed there and
	// then), and one the document doesn't have TDR_FAILED.
	TEXT_RESULT coord_from_lineno(size_w lineno, TextCoord *coord);

	// The coordinate at a UTF-16 column of a line. A column part way through a
	// character (between the halves of a surrogate pair) moves back to its start.
	bool  coord_from_line_pos(const TextCoord *line, size_w column, TextCoord *coord);
	bool  previous_line_from_coord(const TextCoord *coord, uint32_t num_lines, TextCoord *line);
	bool  next_line_from_coord(const TextCoord *coord, uint32_t num_lines, TextCoord *line);

	// Move a coordinate across a change and refresh its line. One exactly at the
	// change stays before any inserted text.
	bool  coord_after_change(TextCoord *coord, const TextChange *change);

	// A position's line number, counted from 0: line numbers are a query, never
	// stored in a coordinate. Past the indexed part of the document the answer
	// is TDR_PENDING_INDEX, with an estimate - for drawing and scrolling.
	TEXT_RESULT lineno_from_pos(TextPos pos, size_w *lineno);

	// UTF-16 offset of a position from the start of the document. Only fixed-width
	// encodings can answer yet: UTF-8 and UTF-32 are TDR_FAILED until the index
	// counts characters.
	TEXT_RESULT charoffset_from_pos(TextPos pos, size_w *offset_chars);

	// The line break ending a coordinate's line: TXL_CRLF, TXL_LF or TXL_CR, or 0 for a last line without one.
	uint32_t linebreak_from_coord(const TextCoord *line);

	//
	//	Text access
	//
	TextReader text_from_range(TextPos from, TextPos to);
	uint32_t getline(const TextCoord &line, textchar *buf, uint32_t buflen);

	// The line holding a position, to walk the document a line at a time
	LineIterator line_at(TextPos pos);

	// Find text within [from, to), from 'to' backwards if asked, and report where the match begins and ends.
	bool  find_text(const textchar *text, uint32_t length, TextPos from, TextPos to, bool backwards, bool matchcase, TextPos *match_begin, TextPos *match_end);

	//
	//	Document info
	//  linecount() may be an estimate until the whole document has been counted.
	//
	TEXT_ENCODING getformat();
	size_w linecount();

	//
	//	Indexing. Line numbers are exact over the indexed part of the document,
	//	which starts at the top: opening a file indexes its first 4 MB, and
	//	index_next() indexes about the next length bytes, false once all of it is
	//	indexed (or can't be read). indexed_length() is how much is, in bytes.
	//
	bool   index_next(size_w length);
	size_w indexed_length();

	// The kinds of line break found so far (TXL_CRLF, TXL_CR, TXL_LF bits). Covers the
	// part of the document with exact line numbers, so all of it once it has been counted.
	uint32_t linebreaks_seen();

	size_w byte_length();
	size_w file_length();

	// Length of the document in UTF-16 units, as charoffset_from_pos.
	TEXT_RESULT charcount(size_w *chars);

private:

	//
	//	The smaller and larger of two values of the same type. Not min() and max(),
	//	which windows.h defines as macros; a mix of types won't compile, rather than
	//	comparing signed with unsigned.
	//
	template <class T> static T min_of(T a, T b) { return a < b ? a : b; }
	template <class T> static T max_of(T a, T b) { return a < b ? b : a; }

	//
	//	Used inside TextDocument only
	//

	// Move a position across a change, as coord_after_change() does
	TextPos pos_after_change(TextPos pos, const TextChange *change);

	// Whether linecount() is exact: the whole document has been counted
	bool   linecount_known();

	//
	//	Line navigation
	//
	bool unit_before(size_w offset_bytes, uint32_t *ch, uint32_t *size);
	bool line_bounds_from_offset(size_w offset_bytes, size_w *line_begin, size_w *line_next);
	void coord_in_line(size_w line_begin, size_w line_next, size_w column, TextCoord *coord);
	bool find_text_nocase(const textchar *text, uint32_t length, size_w from, size_w to, bool backwards, size_w *match, size_w *match_length);

	//
	//	UTF-16 / backing-byte conversion
	//
	size_w count_chars           (size_w offset_bytes, size_w *length_chars);
	size_w count_code_units      (size_w offset_bytes, size_w  length_bytes);
	bool   charoffset_from_bytes (size_w offset_bytes, size_w *offset_chars);

	size_t utf16_to_rawdata(const textchar *utf16str, size_t utf16len, uint8_t *rawdata, size_t *rawlen);
	size_t rawdata_to_utf16(uint8_t *rawdata, size_t rawlen, textchar *utf16str, size_t *utf16len);
	size_t rawdata_complete_length(uint8_t *rawdata, size_t rawlen);

	bool   write_converted(SaveFile &file, TEXT_ENCODING encoding, uint32_t linebreak);

	//
	//	Raw file access
	//

	TEXT_ENCODING detect_file_format(int *headersize);
	size_w decode_text(size_w offset_bytes, size_w lenbytes, textchar *buf, uint32_t *len);
	size_w decode_char(size_w offset_bytes, size_t lenbytes, uint32_t *pch32);
	size_w decode_prev_char(size_w offset_bytes, size_w lower_bound, uint32_t *pch32);
	size_w code_unit_size();

	//
	//	Raw editing
	//

	size_w	insert_raw(size_w offset_bytes, const textchar *text, uint32_t length);
	size_w	replace_raw(size_w offset_bytes, const textchar *text, uint32_t length, size_w erase_bytes);
	void	event_change(TextChange *change);


	// Raw byte storage.
	sequence m_seq;

	// Opening a file indexes this much of it, so most files' line numbers are
	// exact at once. The rest of a larger file is indexed only when asked.
	static const size_w OPEN_INDEX_LENGTH = 0x400000;

	TEXT_ENCODING m_nFileFormat;
	int    m_nHeaderSize;	// the byte-order mark the sequence starts with, if the file had one
	bool   m_fBom;			// whether the file has one now
};

//
//	Walks the document a line at a time, forwards or backwards. It keeps no line
//	numbers, which are queries; like a TextCoord, only valid until the document
//	changes.
//
class LineIterator
{
public:
	// An iterator on no line: it can't move
	LineIterator();

	// Step to the next or previous line: false, without moving, at either end.
	// A final line break is followed by one more, empty, line.
	bool next();
	bool prev();

	// The line: its start as the position, with its bounds
	const TextCoord &line() const;

	// The line break ending the line: TXL_CRLF, TXL_LF, TXL_CR, or 0 for a last line without one
	uint32_t linebreak() const;

	// The line's text, its line break included, as getline()
	uint32_t text(textchar *buf, uint32_t buflen) const;

	// The line's text as a stream, for a line too long for one buffer
	TextReader reader() const;

private:
	friend class TextDocument;

	LineIterator(TextDocument *doc, const TextCoord &line);

	TextDocument *m_doc;
	TextCoord	  m_line;
};

#endif
