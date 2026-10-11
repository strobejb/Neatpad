//
//	MODULE:		TextDocumentCoord.cpp
//
//	PURPOSE:	TextDocument positions, lines and line-by-line navigation
//
//	NOTES:		www.catch22.net
//

#include "TextDocument.h"
#include "Unicode.h"

//
//	Return the number of lines. This is an estimate until linecount_known() is true.
//
size_w TextDocument::linecount()
{
	return byte_length() == 0 ? 0 : m_seq.linecount();
}

bool TextDocument::linecount_known()
{
	return byte_length() == 0 || m_seq.linecount_known();
}

bool TextDocument::index_next(size_w length)
{
	return m_seq.index_next(length);
}

//
//	How much of the document is indexed, from the top: not counting a
//	byte-order mark, the start of the sequence
//
size_w TextDocument::indexed_length()
{
	size_w indexed = m_seq.indexed_length();

	return indexed > (size_w)m_nHeaderSize ? indexed - m_nHeaderSize : 0;
}

//
//	The kinds of line break found in the exactly-counted part of the document
//
uint32_t TextDocument::linebreaks_seen()
{
	sequence::break_kinds kinds = m_seq.linebreak_kinds();

	return (kinds.crlf ? TXL_CRLF : 0) |
		   (kinds.cr   ? TXL_CR   : 0) |
		   (kinds.lf   ? TXL_LF   : 0);
}

//
//	The byte bounds [*line_begin, *line_next) of the line containing offset_bytes
//
bool TextDocument::line_bounds_from_offset(size_w offset_bytes, size_w *line_begin, size_w *line_next)
{
	size_w rawLength = m_seq.size() - m_nHeaderSize;
	size_w sequenceLineOffset = 0;
	size_w sequenceNextOffset = 0;

	if(!m_seq.line_bounds_from_offset(offset_bytes + m_nHeaderSize, &sequenceLineOffset, &sequenceNextOffset))
		return false;

	*line_begin = sequenceLineOffset <= (size_w)m_nHeaderSize ? 0 : sequenceLineOffset - m_nHeaderSize;
	*line_next  = sequenceNextOffset <= (size_w)m_nHeaderSize ? 0 : sequenceNextOffset - m_nHeaderSize;

	*line_begin = min_of(*line_begin, rawLength);
	*line_next  = min_of(max_of(*line_next, *line_begin), rawLength);
	return true;
}

//
//	Fill coord with the position 'column' UTF-16 units into the line
//	[line_begin, line_next), stopping at the end of the line. A column part way
//	through a character moves back to the character's start.
//
void TextDocument::coord_in_line(size_w line_begin, size_w line_next, size_w column, TextCoord *coord)
{
	size_w linelen_bytes = line_next >= line_begin ? line_next - line_begin : 0;
	size_w offset_bytes;

	// a code unit always takes at least one byte, so this just bounds the scan
	if(column > linelen_bytes)
		column = linelen_bytes;

	offset_bytes = count_chars(line_begin, &column);

	if(offset_bytes > linelen_bytes)
	{
		offset_bytes = linelen_bytes;
		column = count_code_units(line_begin, linelen_bytes);
	}

	coord->byte_offset = line_begin + offset_bytes;
	coord->line_begin  = TextPos(line_begin);
	coord->line_next   = TextPos(line_begin + linelen_bytes);
	coord->column      = column;
}

bool TextDocument::coord_from_pos(TextPos pos, TextCoord *coord)
{
	size_w rawLength = m_seq.size() - m_nHeaderSize;
	size_w offset = pos.byte_offset;
	size_w line_begin;
	size_w line_next;

	if(coord == 0)
		return false;

	if(offset > rawLength)
		offset = rawLength;

	if(rawLength == 0)
	{
		*coord = TextCoord();
		return true;
	}

	if(!line_bounds_from_offset(offset, &line_begin, &line_next))
		return false;

	coord->byte_offset = offset;
	coord->line_begin  = TextPos(line_begin);
	coord->line_next   = TextPos(line_next);
	coord->column      = count_code_units(line_begin, offset - line_begin);

	return true;
}

bool TextDocument::coord_from_document_end(TextCoord *coord)
{
	return coord_from_pos(TextPos(byte_length()), coord);
}

bool TextDocument::coord_from_line_pos(const TextCoord *line, size_w column, TextCoord *coord)
{
	if(line == 0 || coord == 0)
		return false;

	coord_in_line(line->line_begin.byte_offset, line->line_next.byte_offset, column, coord);
	return true;
}

bool TextDocument::previous_line_from_coord(const TextCoord *coord, uint32_t num_lines, TextCoord *line)
{
	TextCoord target;

	if(coord == 0 || line == 0)
		return false;

	target = *coord;

	while(num_lines-- > 0 && target.line_begin.byte_offset > 0)
	{
		size_w line_begin;
		size_w line_next;

		// the line which ends where this one begins
		if(!line_bounds_from_offset(target.line_begin.byte_offset - 1, &line_begin, &line_next))
			return false;

		coord_in_line(line_begin, line_next, 0, &target);
	}

	*line = target;
	return true;
}

bool TextDocument::next_line_from_coord(const TextCoord *coord, uint32_t num_lines, TextCoord *line)
{
	TextCoord target;
	size_w rawLength = m_seq.size() - m_nHeaderSize;

	if(coord == 0 || line == 0)
		return false;

	target = *coord;

	while(num_lines-- > 0)
	{
		size_w sequenceLineNext = 0;
		size_w line_next;

		if(target.line_next.byte_offset >= rawLength)
		{
			// a line break at the very end is followed by one more, empty, line
			if(target.line_next.byte_offset == rawLength && linebreak_from_coord(&target))
				coord_in_line(rawLength, rawLength, 0, &target);

			break;
		}

		if(!m_seq.next_line_from_offset(target.line_next.byte_offset + m_nHeaderSize, &sequenceLineNext))
			return false;

		line_next = sequenceLineNext <= (size_w)m_nHeaderSize ? 0 : sequenceLineNext - m_nHeaderSize;

		coord_in_line(target.line_next.byte_offset, min_of(line_next, rawLength), 0, &target);
	}

	*line = target;
	return true;
}

//
//	Move a position across a change to the document. A position inside the
//	erased bytes moves to the start of the change; one exactly at the change
//	stays before any inserted text.
//
TextPos TextDocument::pos_after_change(TextPos pos, const TextChange *change)
{
	if(change == 0 || pos <= change->at)
		return pos;

	if(pos - change->at >= change->erased)
		return TextPos(pos.byte_offset - change->erased + change->inserted);

	return change->at;
}

//
//	Move a coordinate across a change, as pos_after_change(), and refresh its line
//
bool TextDocument::coord_after_change(TextCoord *coord, const TextChange *change)
{
	if(coord == 0 || change == 0)
		return false;

	return coord_from_pos(pos_after_change(*coord, change), coord);
}

//
//	Return the line number of a position. Past the indexed part of a lazily
//	opened file the number is an estimate, and pending.
//
TEXT_RESULT TextDocument::lineno_from_pos(TextPos pos, size_w *lineno)
{
	size_w sequenceOffset;

	*lineno = 0;

	if(m_seq.size() <= (size_w)m_nHeaderSize)
		return TDR_OK;

	sequenceOffset = pos.byte_offset + m_nHeaderSize;

	if(!m_seq.lineno_from_offset(sequenceOffset, lineno, 0))
		return TDR_FAILED;

	return m_seq.lineno_known_at(sequenceOffset) ? TDR_OK : TDR_PENDING_INDEX;
}

//
//	The start of a line: exact or nothing, as Go To needs it, rather than the
//	estimate that offset_from_lineno gives for a line far past the index
//
TEXT_RESULT TextDocument::coord_from_lineno(size_w lineno, TextCoord *coord)
{
	size_w offset;

	if(coord == 0)
		return TDR_FAILED;

	// an empty document has one, empty, line
	if(byte_length() == 0)
		return lineno == 0 && coord_from_pos(TextPos(0), coord) ? TDR_OK : TDR_FAILED;

	if(!m_seq.offset_from_lineno(lineno, &offset) || !m_seq.lineno_known(lineno))
		return m_seq.linecount_known() ? TDR_FAILED : TDR_PENDING_INDEX;

	// the first line starts at the top of the sequence, before any byte-order mark
	offset = offset > (size_w)m_nHeaderSize ? offset - m_nHeaderSize : 0;

	return coord_from_pos(TextPos(offset), coord) ? TDR_OK : TDR_FAILED;
}

TEXT_RESULT TextDocument::charoffset_from_pos(TextPos pos, size_w *offset_chars)
{
	return charoffset_from_bytes(pos.byte_offset, offset_chars);
}

TEXT_RESULT TextDocument::charcount(size_w *chars)
{
	return charoffset_from_bytes(byte_length(), chars);
}

//
//	The coordinate of a UTF-16 offset from the start of the document, counted
//	as charoffset_from_pos counts it. An offset between the halves of a
//	surrogate pair moves back to the start of its character.
//
TEXT_RESULT TextDocument::coord_from_charoffset(size_w offset_chars, TextCoord *coord)
{
	size_w   offset_bytes;
	uint32_t ch;
	uint32_t size;

	// a character always takes at least a byte
	if(coord == 0 || offset_chars > byte_length())
		return TDR_FAILED;

	switch(m_nFileFormat)
	{
	case NCP_ASCII:
		offset_bytes = offset_chars;
		break;

	case NCP_UTF16:
	case NCP_UTF16BE:
		offset_bytes = offset_chars * sizeof(UTF16);

		// after a high surrogate and before a low one: back to the high one
		if(unit_before(offset_bytes, &ch, &size) && ch >= UNI_SUR_HIGH_START && ch <= UNI_SUR_HIGH_END &&
		   unit_before(offset_bytes + sizeof(UTF16), &ch, &size) && ch >= UNI_SUR_LOW_START && ch <= UNI_SUR_LOW_END)
			offset_bytes -= sizeof(UTF16);

		break;

	default:
		// the sequence counts a byte-order mark as one character, U+FEFF
		if(!m_seq.offset_from_chars(offset_chars + (m_nHeaderSize > 0 ? 1 : 0), &offset_bytes))
			return m_seq.linecount_known() ? TDR_FAILED : TDR_PENDING_INDEX;

		offset_bytes -= m_nHeaderSize;
		break;
	}

	if(offset_bytes > byte_length())
		return TDR_FAILED;

	return coord_from_pos(TextPos(offset_bytes), coord) ? TDR_OK : TDR_FAILED;
}

//
//	UTF-16 units before a byte offset. The fixed-width encodings work it out;
//	UTF-8 and UTF-32 count it from the index, so past the indexed part of the
//	document it is pending.
//
TEXT_RESULT TextDocument::charoffset_from_bytes(size_w offset_bytes, size_w *offset_chars)
{
	size_w chars;

	if(offset_chars == 0)
		return TDR_FAILED;

	switch(m_nFileFormat)
	{
	case NCP_ASCII:
		*offset_chars = offset_bytes;
		return TDR_OK;

	case NCP_UTF16:
	case NCP_UTF16BE:
		*offset_chars = offset_bytes / sizeof(UTF16);
		return TDR_OK;

	default:
		if(!m_seq.chars_from_offset(m_nHeaderSize + offset_bytes, &chars))
			return m_seq.linecount_known() ? TDR_FAILED : TDR_PENDING_INDEX;

		// the sequence counts a byte-order mark as one character, U+FEFF
		*offset_chars = chars - (m_nHeaderSize > 0 ? 1 : 0);
		return TDR_OK;
	}
}

//
//	The line break that ends a line. A line break is CR, LF or CRLF in the file's
//	own code units: the same as the sequence counts.
//
uint32_t TextDocument::linebreak_from_coord(const TextCoord *line)
{
	uint32_t ch;
	uint32_t prev;
	uint32_t size;
	size_w   begin;
	size_w   next;

	if(line == 0)
		return 0;

	begin = line->line_begin.byte_offset;
	next  = line->line_next.byte_offset;

	// the last unit must lie inside the line
	if(!unit_before(next, &ch, &size) || next - size < begin)
		return 0;

	if(ch == '\r')
		return TXL_CR;

	if(ch != '\n')
		return 0;

	if(next - size - begin >= size && unit_before(next - size, &prev, &size) && prev == '\r')
		return TXL_CRLF;

	return TXL_LF;
}

//
//	The code unit that ends at offset_bytes, in the file's own encoding, and its size in bytes
//
bool TextDocument::unit_before(size_w offset_bytes, uint32_t *ch, uint32_t *size)
{
	uint8_t raw[4];
	uint32_t unit = 1;

	switch(m_nFileFormat)
	{
	case NCP_UTF16:
	case NCP_UTF16BE:
		unit = 2;
		break;

	case NCP_UTF32:
	case NCP_UTF32BE:
		unit = 4;
		break;

	case NCP_ASCII:
	case NCP_UTF8:
	default:
		unit = 1;
		break;
	}

	if(offset_bytes < unit)
		return false;

	if(m_seq.render(m_nHeaderSize + offset_bytes - unit, raw, unit) != unit)
		return false;

	switch(m_nFileFormat)
	{
	case NCP_UTF16:
		*ch = (uint32_t)raw[0] | ((uint32_t)raw[1] << 8);
		break;

	case NCP_UTF16BE:
		*ch = ((uint32_t)raw[0] << 8) | (uint32_t)raw[1];
		break;

	case NCP_UTF32:
		*ch = (uint32_t)raw[0] | ((uint32_t)raw[1] << 8) | ((uint32_t)raw[2] << 16) | ((uint32_t)raw[3] << 24);
		break;

	case NCP_UTF32BE:
		*ch = ((uint32_t)raw[0] << 24) | ((uint32_t)raw[1] << 16) | ((uint32_t)raw[2] << 8) | (uint32_t)raw[3];
		break;

	case NCP_ASCII:
	case NCP_UTF8:
	default:
		*ch = raw[0];
		break;
	}

	*size = unit;
	return true;
}

//
//	The number of bytes taken by up to *length_chars UTF-16 units of text from
//	offset_bytes. Only whole characters are counted: a surrogate pair the count
//	would split is left out. On return *length_chars holds the units counted.
//
size_w TextDocument::count_chars(size_w offset_bytes, size_w *length_chars)
{
	size_w rawLength = m_seq.size() - m_nHeaderSize;
	size_w wanted = *length_chars;
	size_w remaining = wanted;
	size_w offset_start = offset_bytes;
	uint32_t ch32;

	switch(m_nFileFormat)
	{
	case NCP_ASCII:
		return wanted;

	case NCP_UTF16:
	case NCP_UTF16BE:
		offset_bytes += wanted * sizeof(UTF16);

		// ending between the halves of a pair? leave the pair out
		if(wanted > 0 && decode_char(offset_bytes - sizeof(UTF16), 2 * sizeof(UTF16), &ch32) == 2 * sizeof(UTF16))
		{
			offset_bytes -= sizeof(UTF16);
			*length_chars = wanted - 1;
		}

		return offset_bytes - offset_start;

	default:
		break;
	}

	while(remaining && offset_bytes < rawLength)
	{
		textchar buf[0x100];
		uint32_t charlen = (uint32_t)min_of(remaining, (size_w)0x100);
		size_w   bytelen = decode_text(offset_bytes, rawLength - offset_bytes, buf, &charlen);

		// nothing fits: the next character is a surrogate pair and one unit is left
		if(bytelen == 0 || charlen == 0)
			break;

		remaining    -= charlen;
		offset_bytes += bytelen;
	}

	*length_chars = wanted - remaining;
	return offset_bytes - offset_start;
}

size_w TextDocument::count_code_units(size_w offset_bytes, size_w length_bytes)
{
	size_w chars = 0;
	size_w rawLength = m_seq.size() - m_nHeaderSize;

	while(length_bytes && offset_bytes < rawLength)
	{
		textchar buf[0x100];
		uint32_t charlen = 0x100;
		size_w bytelen = decode_text(offset_bytes, min_of(length_bytes, rawLength - offset_bytes), buf, &charlen);

		if(bytelen == 0)
			break;

		chars += charlen;
		offset_bytes += bytelen;
		length_bytes -= bytelen;
	}

	return chars;
}

//
//	The line holding a position, its start as the iterator's position
//
LineIterator TextDocument::line_at(TextPos pos)
{
	TextCoord line;

	if(!coord_from_pos(pos, &line))
		return LineIterator();

	coord_in_line(line.line_begin.byte_offset, line.line_next.byte_offset, 0, &line);
	return LineIterator(this, line);
}

LineIterator::LineIterator()
	: m_doc(0)
{
}

LineIterator::LineIterator(TextDocument *doc, const TextCoord &line)
	: m_doc(doc), m_line(line)
{
}

//
//	next_line_from_coord() stays on the last line rather than failing,
//	so a step that lands on the same line is the end
//
bool LineIterator::next()
{
	TextCoord following;

	if(m_doc == 0 || !m_doc->next_line_from_coord(&m_line, 1, &following) || following.line_begin == m_line.line_begin)
		return false;

	m_line = following;
	return true;
}

bool LineIterator::prev()
{
	TextCoord preceding;

	if(m_doc == 0 || !m_doc->previous_line_from_coord(&m_line, 1, &preceding) || preceding.line_begin == m_line.line_begin)
		return false;

	m_line = preceding;
	return true;
}

const TextCoord &LineIterator::line() const
{
	return m_line;
}

uint32_t LineIterator::linebreak() const
{
	return m_doc ? m_doc->linebreak_from_coord(&m_line) : 0;
}

uint32_t LineIterator::text(textchar *buf, uint32_t buflen) const
{
	return m_doc ? m_doc->getline(m_line, buf, buflen) : 0;
}

TextReader LineIterator::reader() const
{
	return m_doc ? m_doc->text_from_range(m_line.line_begin, m_line.line_next) : TextReader();
}
