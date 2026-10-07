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
#include "TextLineMapper.h"

static bool is_linebreak(TCHAR ch)
{
	return ch == '\r' || ch == '\n' ||
		ch == '\x0b' || ch == '\x0c' ||
		ch == '\x85' || ch == 0x2028 ||
		ch == 0x2029;
}

//
//	Return the number of lines. This is an estimate until linecount_known() is true.
//
ULONG TextDocument::linecount()
{
	ULONG lines = 0;

	TextLineMapper::linecount(this, &lines);
	return lines;
}

bool TextDocument::linecount_known()
{
	bool known = false;

	TextLineMapper::linecount_known(this, &known);
	return known;
}

bool TextDocument::lineno_known(ULONG lineno)
{
	bool known = false;

	TextLineMapper::lineno_known(this, lineno, &known);
	return known;
}

bool TextDocument::line_number_range_known(ULONG offset_chars, ULONG length_chars)
{
	bool known = false;

	TextLineMapper::line_number_range_known(this, offset_chars, length_chars, &known);
	return known;
}

void TextDocument::index_lines(ULONG offset_chars, ULONG length_chars)
{
	TextLineMapper::index_lines(this, offset_chars, length_chars);
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
//	Return information about specified line
//
bool TextDocument::raw_lineinfo_from_lineno(ULONG lineno, RawLineInfo *lineinfo)
{
	TextLineMapper::LineInfo mapperinfo;

	if(lineinfo == 0 || !TextLineMapper::lineinfo_from_lineno(this, lineno, &mapperinfo))
		return false;

	lineinfo->lineno = mapperinfo.lineno;
	lineinfo->lineoff_chars = mapperinfo.lineoff_chars;
	lineinfo->linelen_chars = mapperinfo.linelen_chars;
	lineinfo->lineoff_bytes = mapperinfo.lineoff_bytes;
	lineinfo->linelen_bytes = mapperinfo.linelen_bytes;
	lineinfo->chars_known = mapperinfo.chars_known;
	return true;
}

bool TextDocument::lineinfo_from_lineno(ULONG lineno, ULONG *lineoff_chars, ULONG *linelen_chars)
{
	RawLineInfo lineinfo;

	if(!raw_lineinfo_from_lineno(lineno, &lineinfo))
		return false;

	if(lineoff_chars) *lineoff_chars = lineinfo.lineoff_chars;
	if(linelen_chars) *linelen_chars = lineinfo.linelen_chars;

	return true;
}

//
//	Perform a reverse lookup - file-offset to line number
//
bool TextDocument::raw_lineinfo_from_offset(ULONG offset_chars, RawLineInfo *lineinfo)
{
	TextLineMapper::LineInfo mapperinfo;

	if(lineinfo == 0 || !TextLineMapper::lineinfo_from_offset(this, offset_chars, &mapperinfo))
		return false;

	lineinfo->lineno = mapperinfo.lineno;
	lineinfo->lineoff_chars = mapperinfo.lineoff_chars;
	lineinfo->linelen_chars = mapperinfo.linelen_chars;
	lineinfo->lineoff_bytes = mapperinfo.lineoff_bytes;
	lineinfo->linelen_bytes = mapperinfo.linelen_bytes;
	lineinfo->chars_known = mapperinfo.chars_known;
	return true;
}

bool TextDocument::lineinfo_from_offset(ULONG offset_chars, ULONG *lineno, ULONG *lineoff_chars, ULONG *linelen_chars)
{
	RawLineInfo lineinfo;

	if(!raw_lineinfo_from_offset(offset_chars, &lineinfo))
		return false;

	if(lineno) *lineno = lineinfo.lineno;
	if(lineoff_chars) *lineoff_chars = lineinfo.lineoff_chars;
	if(linelen_chars) *linelen_chars = lineinfo.linelen_chars;

	return true;
}

bool TextDocument::lineinfo_from_offset(ULONG offset_chars, TextLineInfo *lineinfo)
{
	RawLineInfo rawinfo;

	if(lineinfo == 0)
		return false;

	if(!raw_lineinfo_from_offset(offset_chars, &rawinfo))
		return false;

	lineinfo->lineno = rawinfo.lineno;
	lineinfo->lineoff_chars = rawinfo.lineoff_chars;
	lineinfo->linelen_chars = rawinfo.linelen_chars;

	return true;
}

void TextDocument::copy_line(RawLineInfo *source, DocLine *dest)
{
	dest->index = source->lineno;
	dest->index_known = lineno_known(source->lineno);
	dest->offset_chars = source->lineoff_chars;
	dest->length_chars = source->linelen_chars;
}

void TextDocument::copy_coord(RawLineInfo *source, ULONG offset_chars, TextCoord *dest)
{
	ULONG lineEnd = source->lineoff_chars + source->linelen_chars;
	ULONG lineOffsetChars;
	ULONG lineOffsetBytes;

	if(offset_chars < source->lineoff_chars)
		offset_chars = source->lineoff_chars;

	if(offset_chars > lineEnd)
		offset_chars = lineEnd;

	lineOffsetChars = offset_chars - source->lineoff_chars;
	lineOffsetBytes = count_chars(source->lineoff_bytes, lineOffsetChars);

	if(lineOffsetBytes > source->linelen_bytes)
		lineOffsetBytes = source->linelen_bytes;

	dest->byte_anchor = source->lineoff_bytes + lineOffsetBytes;
	dest->line_begin = source->lineoff_bytes;
	dest->line_next = source->lineoff_bytes + source->linelen_bytes;
	dest->line_offset_chars = lineOffsetChars;
	dest->offset_chars = offset_chars;
	copy_line(source, &dest->line);
}

bool TextDocument::line_from_offset(ULONG offset_chars, DocLine *line)
{
	RawLineInfo rawinfo;

	if(line == 0 || !raw_lineinfo_from_offset(offset_chars, &rawinfo))
		return false;

	copy_line(&rawinfo, line);
	return true;
}

bool TextDocument::line_from_index(ULONG index, DocLine *line)
{
	RawLineInfo rawinfo;

	if(line == 0 || !raw_lineinfo_from_lineno(index, &rawinfo))
		return false;

	copy_line(&rawinfo, line);
	return true;
}

bool TextDocument::previous_line_from_offset(ULONG offset_chars, ULONG num_lines, DocLine *line)
{
	RawLineInfo rawinfo;

	if(line == 0 || !previous_raw_lineinfo_from_offset(offset_chars, num_lines, &rawinfo))
		return false;

	copy_line(&rawinfo, line);
	return true;
}

bool TextDocument::next_line_from_offset(ULONG offset_chars, ULONG num_lines, DocLine *line)
{
	RawLineInfo rawinfo;

	if(line == 0 || !next_raw_lineinfo_from_offset(offset_chars, num_lines, &rawinfo))
		return false;

	copy_line(&rawinfo, line);
	return true;
}

bool TextDocument::coord_from_offset(ULONG offset_chars, TextCoord *coord)
{
	RawLineInfo rawinfo;

	if(coord == 0)
		return false;

	if(text_length() == 0)
		return coord_from_byte_anchor(0, coord);

	if(!raw_lineinfo_from_offset(offset_chars, &rawinfo))
		return false;

	copy_coord(&rawinfo, offset_chars, coord);
	return true;
}

bool TextDocument::coord_from_byte_anchor(ULONG byte_anchor, TextCoord *coord)
{
	ULONG rawLength = m_seq.size() - m_nHeaderSize;
	size_w sequenceOffset;
	size_w sequenceLineOffset = 0;
	size_w sequenceNextOffset = 0;
	RawLineInfo rawinfo;
	size_w sequenceLine = 0;
	ULONG nextoff_bytes;
	ULONG offset_chars;

	if(coord == 0)
		return false;

	if(byte_anchor > rawLength)
		byte_anchor = rawLength;

	if(rawLength == 0)
	{
		memset(&rawinfo, 0, sizeof(rawinfo));
		copy_coord(&rawinfo, 0, coord);
		return true;
	}

	sequenceOffset = byte_anchor + m_nHeaderSize;

	if(!m_seq.linebounds_from_offset(sequenceOffset, &sequenceLineOffset, &sequenceNextOffset))
		return false;

	rawinfo.lineoff_bytes = sequenceLineOffset <= (size_w)m_nHeaderSize ? 0 : (ULONG)(sequenceLineOffset - m_nHeaderSize);
	nextoff_bytes = sequenceNextOffset <= (size_w)m_nHeaderSize ? 0 : (ULONG)(sequenceNextOffset - m_nHeaderSize);
	rawinfo.linelen_bytes = nextoff_bytes >= rawinfo.lineoff_bytes ? nextoff_bytes - rawinfo.lineoff_bytes : 0;

	if(rawinfo.lineoff_bytes + rawinfo.linelen_bytes > rawLength)
		rawinfo.linelen_bytes = rawLength - rawinfo.lineoff_bytes;

	rawinfo.lineoff_chars = TextLineMapper::byteoffset_to_charoffset(this, rawinfo.lineoff_bytes);
	rawinfo.linelen_chars = count_code_units(rawinfo.lineoff_bytes, rawinfo.linelen_bytes);
	rawinfo.chars_known = m_nFileFormat != NCP_UTF8 || rawLength <= MEM_BLOCK_SIZE;

	if(m_seq.linefromoffset(sequenceOffset, &sequenceLine, 0))
		rawinfo.lineno = (ULONG)sequenceLine;
	else
		rawinfo.lineno = 0;

	offset_chars = rawinfo.lineoff_chars + count_code_units(rawinfo.lineoff_bytes, byte_anchor - rawinfo.lineoff_bytes);

	if(offset_chars > rawinfo.lineoff_chars + rawinfo.linelen_chars)
		offset_chars = rawinfo.lineoff_chars + rawinfo.linelen_chars;

	coord->byte_anchor = byte_anchor;
	coord->line_begin = rawinfo.lineoff_bytes;
	coord->line_next = rawinfo.lineoff_bytes + rawinfo.linelen_bytes;
	coord->line_offset_chars = offset_chars - rawinfo.lineoff_chars;
	coord->offset_chars = offset_chars;
	copy_line(&rawinfo, &coord->line);

	return true;
}

bool TextDocument::coord_from_document_end(TextCoord *coord)
{
	ULONG rawLength = m_seq.size() - m_nHeaderSize;

	return coord_from_byte_anchor(rawLength, coord);
}

bool TextDocument::coord_from_line_pos(TextCoord *line, ULONG line_offset_chars, TextCoord *coord)
{
	RawLineInfo rawinfo;

	if(line == 0 || coord == 0)
		return false;

	if(line_offset_chars > line->line.length_chars)
		line_offset_chars = line->line.length_chars;

	rawinfo.lineno = line->line.index;
	rawinfo.lineoff_chars = line->line.offset_chars;
	rawinfo.linelen_chars = line->line.length_chars;
	rawinfo.lineoff_bytes = line->line_begin;
	rawinfo.linelen_bytes = line->line_next >= line->line_begin ? line->line_next - line->line_begin : 0;
	rawinfo.chars_known = true;

	copy_coord(&rawinfo, rawinfo.lineoff_chars + line_offset_chars, coord);
	return true;
}

bool TextDocument::previous_line_from_coord(TextCoord *coord, ULONG num_lines, TextCoord *line)
{
	TextCoord target;
	ULONG rawLength = m_seq.size() - m_nHeaderSize;

	if(coord == 0 || line == 0)
		return false;

	target = *coord;

	while(num_lines-- > 0)
	{
		size_w sequenceProbe;
		size_w sequenceLineOffset = 0;
		size_w sequenceNextOffset = 0;
		ULONG lineoff_bytes;
		ULONG nextoff_bytes;
		ULONG linelen_chars;
		RawLineInfo rawinfo;

		if(target.line_begin == 0)
			break;

		sequenceProbe = target.line_begin - 1 + m_nHeaderSize;

		if(!m_seq.linebounds_from_offset(sequenceProbe, &sequenceLineOffset, &sequenceNextOffset))
			return false;

		lineoff_bytes = sequenceLineOffset <= (size_w)m_nHeaderSize ? 0 : (ULONG)(sequenceLineOffset - m_nHeaderSize);
		nextoff_bytes = sequenceNextOffset <= (size_w)m_nHeaderSize ? 0 : (ULONG)(sequenceNextOffset - m_nHeaderSize);

		if(lineoff_bytes > rawLength)
			lineoff_bytes = rawLength;

		if(nextoff_bytes > rawLength)
			nextoff_bytes = rawLength;

		rawinfo.lineno = target.line.index > 0 ? target.line.index - 1 : 0;
		rawinfo.lineoff_bytes = lineoff_bytes;
		rawinfo.linelen_bytes = nextoff_bytes >= lineoff_bytes ? nextoff_bytes - lineoff_bytes : 0;
		rawinfo.linelen_chars = count_code_units(rawinfo.lineoff_bytes, rawinfo.linelen_bytes);
		linelen_chars = rawinfo.linelen_chars;
		rawinfo.lineoff_chars = target.line.offset_chars >= linelen_chars ? target.line.offset_chars - linelen_chars : TextLineMapper::byteoffset_to_charoffset(this, rawinfo.lineoff_bytes);
		rawinfo.chars_known = m_nFileFormat != NCP_UTF8 || rawLength <= MEM_BLOCK_SIZE;

		copy_coord(&rawinfo, rawinfo.lineoff_chars, &target);
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
		size_w sequenceNextOffset;
		size_w sequenceLineNext = 0;
		RawLineInfo rawinfo;

		if(target.line_next >= rawLength)
		{
			if(target.line_next == rawLength && target.line_begin < rawLength && raw_offset_ends_with_linebreak(rawLength))
			{
				rawinfo.lineno = target.line.index + 1;
				rawinfo.lineoff_bytes = rawLength;
				rawinfo.linelen_bytes = 0;
				rawinfo.lineoff_chars = target.line.offset_chars + target.line.length_chars;
				rawinfo.linelen_chars = 0;
				rawinfo.chars_known = true;

				copy_coord(&rawinfo, rawinfo.lineoff_chars, &target);
			}

			break;
		}

		sequenceNextOffset = target.line_next + m_nHeaderSize;

		if(!m_seq.next_lineoffset(sequenceNextOffset, &sequenceLineNext))
			return false;

		rawinfo.lineno = target.line.index + 1;
		rawinfo.lineoff_bytes = target.line_next;
		rawinfo.linelen_bytes = sequenceLineNext <= (size_w)m_nHeaderSize ? 0 : (ULONG)(sequenceLineNext - m_nHeaderSize - target.line_next);

		if(rawinfo.lineoff_bytes > rawLength)
			rawinfo.lineoff_bytes = rawLength;

		if(rawinfo.lineoff_bytes + rawinfo.linelen_bytes > rawLength)
			rawinfo.linelen_bytes = rawLength - rawinfo.lineoff_bytes;

		rawinfo.lineoff_chars = target.line.offset_chars + target.line.length_chars;
		rawinfo.linelen_chars = count_code_units(rawinfo.lineoff_bytes, rawinfo.linelen_bytes);
		rawinfo.chars_known = m_nFileFormat != NCP_UTF8 || rawLength <= MEM_BLOCK_SIZE;

		copy_coord(&rawinfo, rawinfo.lineoff_chars, &target);
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

	if(!m_seq.linefromoffset(sequenceOffset, &line, 0))
	{
		*exact = false;
		return 0;
	}

	*exact = m_seq.line_numbers_known(0, sequenceOffset);
	return (ULONG)line;
}

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

	return is_linebreak((TCHAR)ch);
}

bool TextDocument::coord_from_line_offset(DocLine *line, ULONG offset_chars, TextCoord *coord)
{
	RawLineInfo rawinfo;

	if(line == 0 || coord == 0)
		return false;

	if(offset_chars < line->offset_chars)
		offset_chars = line->offset_chars;

	if(offset_chars > line->offset_chars + line->length_chars)
		offset_chars = line->offset_chars + line->length_chars;

	if(!raw_lineinfo_from_offset(line->offset_chars, &rawinfo))
		return false;

	copy_coord(&rawinfo, offset_chars, coord);
	return true;
}

ULONG TextDocument::line_text_end(TextLineInfo *lineinfo)
{
	RawLineInfo rawinfo;
	ULONG offset_chars;
	ULONG offset_bytes;
	ULONG bytes_left;

	if(lineinfo == 0)
		return 0;

	if(!raw_lineinfo_from_offset(lineinfo->lineoff_chars, &rawinfo))
		return lineinfo->lineoff_chars;

	offset_chars = rawinfo.lineoff_chars;
	offset_bytes = rawinfo.lineoff_bytes;
	bytes_left = rawinfo.linelen_bytes;

	while(bytes_left)
	{
		TCHAR buf[0x100];
		ULONG chars = 0x100;
		ULONG bytes = decode_text(offset_bytes, bytes_left, buf, &chars);

		for(ULONG i = 0; i < chars; i++)
		{
			if(is_linebreak(buf[i]))
				return offset_chars + i;
		}

		if(bytes == 0)
			break;

		offset_chars += chars;
		offset_bytes += bytes;

		if(bytes > bytes_left)
			break;

		bytes_left -= bytes;
	}

	return offset_chars;
}

bool TextDocument::previous_lineinfo_from_offset(ULONG offset_chars, ULONG num_lines, TextLineInfo *lineinfo)
{
	RawLineInfo rawinfo;

	if(lineinfo == 0)
		return false;

	if(!previous_raw_lineinfo_from_offset(offset_chars, num_lines, &rawinfo))
		return false;

	lineinfo->lineno = rawinfo.lineno;
	lineinfo->lineoff_chars = rawinfo.lineoff_chars;
	lineinfo->linelen_chars = rawinfo.linelen_chars;

	return true;
}

bool TextDocument::previous_raw_lineinfo_from_offset(ULONG offset_chars, ULONG num_lines, RawLineInfo *lineinfo)
{
	ULONG docLength = text_length();
	RawLineInfo target;

	if(lineinfo == 0 || docLength == 0)
		return false;

	if(offset_chars > docLength)
		offset_chars = docLength;

	if(!raw_lineinfo_from_offset(offset_chars, &target))
		return false;

	// Walk using physical line starts, so navigation does not depend on exact global line numbers.
	while(num_lines-- > 0 && target.lineoff_chars > 0)
	{
		RawLineInfo prev;
		ULONG probe = target.lineoff_chars - 1;

		if(!raw_lineinfo_from_offset(probe, &prev))
			break;

		// A line-boundary lookup can resolve back to the current line; step inside the previous line.
		if(prev.lineoff_chars >= target.lineoff_chars && target.lineoff_chars > 1)
		{
			if(!raw_lineinfo_from_offset(target.lineoff_chars - 2, &prev))
				break;
		}

		if(prev.lineoff_chars >= target.lineoff_chars)
			break;

		target = prev;
	}

	*lineinfo = target;
	return true;
}

bool TextDocument::next_lineinfo_from_offset(ULONG offset_chars, ULONG num_lines, TextLineInfo *lineinfo)
{
	RawLineInfo rawinfo;

	if(lineinfo == 0)
		return false;

	if(!next_raw_lineinfo_from_offset(offset_chars, num_lines, &rawinfo))
		return false;

	lineinfo->lineno = rawinfo.lineno;
	lineinfo->lineoff_chars = rawinfo.lineoff_chars;
	lineinfo->linelen_chars = rawinfo.linelen_chars;

	return true;
}

bool TextDocument::next_raw_lineinfo_from_offset(ULONG offset_chars, ULONG num_lines, RawLineInfo *lineinfo)
{
	ULONG docLength = text_length();
	RawLineInfo target;

	if(lineinfo == 0 || docLength == 0)
		return false;

	if(offset_chars > docLength)
		offset_chars = docLength;

	if(!raw_lineinfo_from_offset(offset_chars, &target))
		return false;

	// Walk by line length to reach the next physical line, stopping cleanly at EOF.
	while(num_lines-- > 0)
	{
		RawLineInfo next;
		ULONG lineEnd;

		if(target.lineoff_chars >= docLength || target.linelen_chars > docLength - target.lineoff_chars)
			lineEnd = docLength;
		else
			lineEnd = target.lineoff_chars + target.linelen_chars;

		if(lineEnd >= docLength)
		{
			if(target.linelen_chars == 0 || !raw_lineinfo_from_offset(docLength, &next))
				break;

			if(next.lineoff_chars != docLength || next.lineno == target.lineno)
				break;
		}
		else if(!raw_lineinfo_from_offset(lineEnd, &next))
		{
			break;
		}

		if(next.lineoff_chars <= target.lineoff_chars)
			break;

		target = next;
	}

	*lineinfo = target;
	return true;
}

ULONG TextDocument::lineno_from_offset(ULONG offset)
{
	ULONG lineno = 0;
	lineinfo_from_offset(offset, &lineno, 0, 0);
	return lineno;
}

ULONG TextDocument::offset_from_lineno(ULONG lineno)
{
	ULONG lineoff = 0;
	lineinfo_from_lineno(lineno, &lineoff, 0);
	return lineoff;
}
