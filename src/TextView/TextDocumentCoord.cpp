//
//	MODULE:		TextDocumentCoord.cpp
//
//	PURPOSE:	TextDocument line lookup and TextCoord navigation
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
//	Return the line number of a position. Past the exactly counted part of a
//	lazily indexed file the number is an estimate, and *exact is false.
//
size_w TextDocument::lineno_from_pos(TextPos pos, bool *exact)
{
	size_w sequenceOffset;
	size_w line = 0;

	*exact = true;

	if(m_seq.size() <= (size_w)m_nHeaderSize)
		return 0;

	sequenceOffset = pos.byte_offset + m_nHeaderSize;

	if(!m_seq.lineno_from_offset(sequenceOffset, &line, 0))
	{
		*exact = false;
		return 0;
	}

	*exact = m_seq.lineno_known_at(sequenceOffset);
	return line;
}

bool TextDocument::charoffset_from_pos(TextPos pos, size_w *offset_chars)
{
	return charoffset_from_bytes(pos.byte_offset, offset_chars);
}

bool TextDocument::charcount(size_w *chars)
{
	return charoffset_from_bytes(byte_length(), chars);
}

//
//	UTF-16 units before a byte offset. Only the fixed-width encodings can say
//	without scanning the text in front of it.
//
bool TextDocument::charoffset_from_bytes(size_w offset_bytes, size_w *offset_chars)
{
	if(offset_chars == 0)
		return false;

	switch(m_nFileFormat)
	{
	case NCP_ASCII:
		*offset_chars = offset_bytes;
		return true;

	case NCP_UTF16:
	case NCP_UTF16BE:
		*offset_chars = offset_bytes / sizeof(UTF16);
		return true;

	default:
		return false;
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
