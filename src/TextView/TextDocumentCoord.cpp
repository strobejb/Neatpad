//
//	MODULE:		TextDocumentCoord.cpp
//
//	PURPOSE:	TextDocument line lookup and TextCoord navigation
//
//	NOTES:		www.catch22.net
//

#define STRICT
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include "TextDocument.h"

//
//	Return the number of lines. This is an estimate until linecount_known() is true.
//
ULONG TextDocument::linecount()
{
	return byte_length() == 0 ? 0 : (ULONG)m_seq.linecount();
}

bool TextDocument::linecount_known()
{
	return byte_length() == 0 || m_seq.linecount_known();
}

//
//	Return the length of longest line
//
ULONG TextDocument::longestline(int tabwidth)
{
	//ULONG i;
	ULONG longest = 0;
	ULONG xpos = 0;
//	char *bufptr = (char *)(buffer + m_nHeaderSize);
/*
	for(i = 0; i < length_bytes; i++)
	{
		if(bufptr[i] == '\r')
		{
			if(bufptr[i+1] == '\n')
				 i++;

			longest = max(longest, xpos);
			xpos = 0;
		}
		else if(bufptr[i] == '\n')
		{
			longest = max(longest, xpos);
			xpos = 0;
		}
		else if(bufptr[i] == '\t')
		{
			xpos += tabwidth - (xpos % tabwidth);
		}
		else
		{
			xpos ++;
		}
	}

	longest = max(longest, xpos);*/
	return 100;//longest;
}

//
//	The byte bounds [*line_begin, *line_next) of the line containing offset_bytes
//
bool TextDocument::line_bounds_from_offset(ULONG offset_bytes, ULONG *line_begin, ULONG *line_next)
{
	ULONG rawLength = m_seq.size() - m_nHeaderSize;
	size_w sequenceLineOffset = 0;
	size_w sequenceNextOffset = 0;

	if(!m_seq.line_bounds_from_offset(offset_bytes + m_nHeaderSize, &sequenceLineOffset, &sequenceNextOffset))
		return false;

	*line_begin = sequenceLineOffset <= (size_w)m_nHeaderSize ? 0 : (ULONG)(sequenceLineOffset - m_nHeaderSize);
	*line_next  = sequenceNextOffset <= (size_w)m_nHeaderSize ? 0 : (ULONG)(sequenceNextOffset - m_nHeaderSize);

	*line_begin = min(*line_begin, rawLength);
	*line_next  = min(max(*line_next, *line_begin), rawLength);
	return true;
}

//
//	Fill coord with the position line_offset_chars UTF-16 units into the line
//	[line_begin, line_next), stopping at the end of the line
//
void TextDocument::coord_in_line(ULONG line_begin, ULONG line_next, ULONG line_offset_chars, TextCoord *coord)
{
	ULONG linelen_bytes = line_next >= line_begin ? line_next - line_begin : 0;
	ULONG offset_bytes;

	// a code unit always takes at least one byte, so this just bounds the scan
	if(line_offset_chars > linelen_bytes)
		line_offset_chars = linelen_bytes;

	offset_bytes = count_chars(line_begin, line_offset_chars);

	if(offset_bytes > linelen_bytes)
	{
		offset_bytes = linelen_bytes;
		line_offset_chars = count_code_units(line_begin, linelen_bytes);
	}

	coord->byte_anchor = line_begin + offset_bytes;
	coord->line_begin = line_begin;
	coord->line_next = line_begin + linelen_bytes;
	coord->line_offset_chars = line_offset_chars;
}

bool TextDocument::coord_from_byte_anchor(ULONG byte_anchor, TextCoord *coord)
{
	ULONG rawLength = m_seq.size() - m_nHeaderSize;
	ULONG line_begin;
	ULONG line_next;

	if(coord == 0)
		return false;

	if(byte_anchor > rawLength)
		byte_anchor = rawLength;

	if(rawLength == 0)
	{
		*coord = TextCoord();
		return true;
	}

	if(!line_bounds_from_offset(byte_anchor, &line_begin, &line_next))
		return false;

	coord->byte_anchor = byte_anchor;
	coord->line_begin = line_begin;
	coord->line_next = line_next;
	coord->line_offset_chars = count_code_units(line_begin, byte_anchor - line_begin);

	return true;
}

bool TextDocument::coord_from_document_end(TextCoord *coord)
{
	ULONG rawLength = m_seq.size() - m_nHeaderSize;

	return coord_from_byte_anchor(rawLength, coord);
}

bool TextDocument::coord_from_line_pos(TextCoord *line, ULONG line_offset_chars, TextCoord *coord)
{
	if(line == 0 || coord == 0)
		return false;

	coord_in_line(line->line_begin, line->line_next, line_offset_chars, coord);
	return true;
}

bool TextDocument::previous_line_from_coord(TextCoord *coord, ULONG num_lines, TextCoord *line)
{
	TextCoord target;

	if(coord == 0 || line == 0)
		return false;

	target = *coord;

	while(num_lines-- > 0 && target.line_begin > 0)
	{
		ULONG line_begin;
		ULONG line_next;

		// the line which ends where this one begins
		if(!line_bounds_from_offset(target.line_begin - 1, &line_begin, &line_next))
			return false;

		coord_in_line(line_begin, line_next, 0, &target);
	}

	*line = target;
	return true;
}

bool TextDocument::next_line_from_coord(TextCoord *coord, ULONG num_lines, TextCoord *line)
{
	TextCoord target;
	ULONG rawLength = m_seq.size() - m_nHeaderSize;

	if(coord == 0 || line == 0)
		return false;

	target = *coord;

	while(num_lines-- > 0)
	{
		size_w sequenceLineNext = 0;
		ULONG line_next;

		if(target.line_next >= rawLength)
		{
			// a line break at the very end is followed by one more, empty, line
			if(target.line_next == rawLength && target.line_begin < rawLength && raw_offset_ends_with_linebreak(rawLength))
				coord_in_line(rawLength, rawLength, 0, &target);

			break;
		}

		if(!m_seq.next_line_from_offset(target.line_next + m_nHeaderSize, &sequenceLineNext))
			return false;

		line_next = sequenceLineNext <= (size_w)m_nHeaderSize ? 0 : (ULONG)(sequenceLineNext - m_nHeaderSize);

		coord_in_line(target.line_next, min(line_next, rawLength), 0, &target);
	}

	*line = target;
	return true;
}

//
//	Move a coordinate across a change to the document and refresh its line bounds.
//	A coordinate inside the erased bytes moves to the start of the change; one
//	exactly at the change offset stays before any inserted text.
//
bool TextDocument::coord_after_change(TextCoord *coord, const TextChange *change)
{
	ULONG anchor;

	if(coord == 0 || change == 0)
		return false;

	anchor = coord->byte_anchor;

	if(anchor > change->offset)
	{
		if(anchor - change->offset >= change->erased)
			anchor = anchor - change->erased + change->inserted;
		else
			anchor = change->offset;
	}

	return coord_from_byte_anchor(anchor, coord);
}

//
//	Return the line number of a coordinate. Past the exactly counted part of a
//	lazily indexed file the number is an estimate, and *exact is false.
//
ULONG TextDocument::lineno_from_coord(const TextCoord *coord, bool *exact)
{
	size_w sequenceOffset;
	size_w line = 0;

	*exact = true;

	if(coord == 0 || m_seq.size() <= (size_w)m_nHeaderSize)
		return 0;

	sequenceOffset = coord->byte_anchor + m_nHeaderSize;

	if(!m_seq.lineno_from_offset(sequenceOffset, &line, 0))
	{
		*exact = false;
		return 0;
	}

	*exact = m_seq.lineno_known_at(sequenceOffset);
	return (ULONG)line;
}

bool TextDocument::charoffset_from_coord(const TextCoord *coord, ULONG *offset_chars)
{
	if(coord == 0 || offset_chars == 0)
		return false;

	switch(m_nFileFormat)
	{
	case NCP_ASCII:
		*offset_chars = coord->byte_anchor;
		return true;

	case NCP_UTF16:
	case NCP_UTF16BE:
		*offset_chars = coord->byte_anchor / sizeof(WCHAR);
		return true;

	default:
		return false;
	}
}

//
//	Does the text end with a line break at offset_bytes? A line break is CR or
//	LF, the same as the sequence counts, in the file's own code units.
//
bool TextDocument::raw_offset_ends_with_linebreak(ULONG offset_bytes)
{
	BYTE raw[4];
	ULONG unit = 1;
	ULONG ch = 0;

	if(offset_bytes == 0)
		return false;

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
		ch = (ULONG)raw[0] | ((ULONG)raw[1] << 8);
		break;

	case NCP_UTF16BE:
		ch = ((ULONG)raw[0] << 8) | (ULONG)raw[1];
		break;

	case NCP_UTF32:
		ch = (ULONG)raw[0] | ((ULONG)raw[1] << 8) | ((ULONG)raw[2] << 16) | ((ULONG)raw[3] << 24);
		break;

	case NCP_UTF32BE:
		ch = ((ULONG)raw[0] << 24) | ((ULONG)raw[1] << 16) | ((ULONG)raw[2] << 8) | (ULONG)raw[3];
		break;

	case NCP_ASCII:
	case NCP_UTF8:
	default:
		ch = raw[0];
		break;
	}

	return ch == '\r' || ch == '\n';
}
