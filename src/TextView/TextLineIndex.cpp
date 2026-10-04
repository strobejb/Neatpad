//
//	MODULE:		TextLineIndex.cpp
//
//	PURPOSE:	Lazy document line index used by TextDocument
//

#define STRICT
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include "TextDocument.h"

static ULONG estimate_muldiv(ULONG numerator, ULONG multiplier, ULONG divisor)
{
	if(divisor == 0)
		return 0;

	unsigned __int64 result = ((unsigned __int64)(numerator / divisor) * multiplier) +
							  ((unsigned __int64)(numerator % divisor) * multiplier) / divisor;

	if(result > 0xffffffff)
		return 0xffffffff;

	return (ULONG)result;
}

static ULONG read_codeunit(TEXT_ENCODING encoding, BYTE *data, ULONG index, ULONG unit)
{
	if(unit == 1)
		return data[index];

	if(encoding == NCP_UTF16BE)
		return ((ULONG)data[index] << 8) | data[index + 1];

	return data[index] | ((ULONG)data[index + 1] << 8);
}

TextLineIndex::TextLineIndex()
{
	m_pTextDoc = 0;
	m_pLinePages = 0;
	m_nLinePageCount = 0;
	m_nLineCount = 0;
	m_fLineCountKnown = false;
}

TextLineIndex::~TextLineIndex()
{
	clear();
}

bool TextLineIndex::init(TextDocument *doc)
{
	ULONG bytes;

	clear();

	m_pTextDoc = doc;
	bytes = raw_length();

	if(bytes == 0)
	{
		m_nLineCount = 0;
		m_fLineCountKnown = true;
		return true;
	}

	m_nLinePageCount = (bytes + MEM_BLOCK_SIZE - 1) / MEM_BLOCK_SIZE;
	m_pLinePages = new LinePage[m_nLinePageCount];

	if(m_pLinePages == 0)
		return false;

	for(ULONG i = 0; i < m_nLinePageCount; i++)
	{
		LinePage *page = &m_pLinePages[i];

		page->offset_bytes = i * MEM_BLOCK_SIZE;
		page->length_bytes = min((ULONG)MEM_BLOCK_SIZE, bytes - page->offset_bytes);
		page->offset_chars = direct_offset_mapping() || i == 0 ? char_from_byte(page->offset_bytes) : 0;
		page->length_chars = direct_offset_mapping() ? char_from_byte(page->offset_bytes + page->length_bytes) - page->offset_chars : 0;
		page->line_offsets_bytes = 0;
		page->line_offsets_chars = 0;
		page->line_count = 0;
		page->line_base = 0;
		page->indexed = false;
		page->offset_known = direct_offset_mapping() || i == 0;
		page->chars_known = direct_offset_mapping();
		page->line_base_known = false;
		page->starts_with_lf = false;
		page->ends_with_cr = false;
	}

	index_lines(0, min(text_length(), char_from_byte(min(bytes, (ULONG)MEM_BLOCK_SIZE))));
	return true;
}

void TextLineIndex::clear()
{
	if(m_pLinePages)
	{
		for(ULONG i = 0; i < m_nLinePageCount; i++)
		{
			delete[] m_pLinePages[i].line_offsets_bytes;
			delete[] m_pLinePages[i].line_offsets_chars;
		}

		delete[] m_pLinePages;
	}

	m_pTextDoc = 0;
	m_pLinePages = 0;
	m_nLinePageCount = 0;
	m_nLineCount = 0;
	m_fLineCountKnown = false;
}

ULONG TextLineIndex::raw_length() const
{
	return m_pTextDoc ? m_pTextDoc->m_seq.size() - m_pTextDoc->m_nHeaderSize : 0;
}

ULONG TextLineIndex::text_length() const
{
	return m_pTextDoc ? m_pTextDoc->text_length() : 0;
}

bool TextLineIndex::direct_offset_mapping() const
{
	return m_pTextDoc->m_nFileFormat == NCP_ASCII ||
		   m_pTextDoc->m_nFileFormat == NCP_UTF16 ||
		   m_pTextDoc->m_nFileFormat == NCP_UTF16BE;
}

ULONG TextLineIndex::codeunit_size() const
{
	switch(m_pTextDoc->m_nFileFormat)
	{
	case NCP_UTF16:
	case NCP_UTF16BE:
		return sizeof(WCHAR);

	case NCP_ASCII:
	case NCP_UTF8:
	default:
		return 1;
	}
}

ULONG TextLineIndex::byte_from_char(ULONG offset_chars) const
{
	return offset_chars * codeunit_size();
}

ULONG TextLineIndex::char_from_byte(ULONG offset_bytes) const
{
	return offset_bytes / codeunit_size();
}

void TextLineIndex::ensure_page_offset(LinePage *page)
{
	if(page->offset_known)
		return;

	// Variable-width pages learn their UTF-16 base offset when first indexed.
	page->offset_chars = char_from_byte(page->offset_bytes);
	page->offset_known = true;
	page->chars_known = direct_offset_mapping();
}

ULONG TextLineIndex::scan_lines(LinePage *page, ULONG *line_offsets_bytes, ULONG *line_offsets_chars)
{
	ULONG count = 0;
	ULONG pos_bytes;
	ULONG end = page->offset_bytes + page->length_bytes;
	ULONG unit = codeunit_size();
	bool first_char = true;
	BYTE *data;
	ULONG bytes_read;

	page->starts_with_lf = false;
	page->ends_with_cr = false;
	page->length_chars = page->length_bytes / unit;
	ensure_page_offset(page);

	data = new BYTE[page->length_bytes];
	if(data == 0)
		return 0;

	bytes_read = (ULONG)m_pTextDoc->m_seq.render(m_pTextDoc->m_nHeaderSize + page->offset_bytes, data, page->length_bytes);
	bytes_read -= bytes_read % unit;
	end = page->offset_bytes + bytes_read;

	for(pos_bytes = page->offset_bytes; pos_bytes + unit <= end; pos_bytes += unit)
	{
		ULONG index = pos_bytes - page->offset_bytes;
		ULONG ch;

		ch = read_codeunit(m_pTextDoc->m_nFileFormat, data, index, unit);

		if(first_char)
		{
			page->starts_with_lf = ch == '\n';
			first_char = false;
		}

		page->ends_with_cr = ch == '\r';

		if(ch == '\r')
		{
			ULONG next_bytes = pos_bytes + unit;
			ULONG next_chars = char_from_byte(next_bytes);

			if(next_bytes + unit <= end)
			{
				ULONG next_index = next_bytes - page->offset_bytes;
				ULONG next_ch;

				next_ch = read_codeunit(m_pTextDoc->m_nFileFormat, data, next_index, unit);

				if(next_ch == '\n')
				{
					next_bytes += unit;
					next_chars++;
					pos_bytes += unit;
					page->ends_with_cr = false;
				}
			}

			if(line_offsets_bytes)
				line_offsets_bytes[count] = next_bytes;

			if(line_offsets_chars)
				line_offsets_chars[count] = next_chars;

			count++;
		}
		else if(ch == '\n')
		{
			ULONG next_bytes = pos_bytes + unit;
			ULONG next_chars = char_from_byte(next_bytes);

			if(line_offsets_bytes)
				line_offsets_bytes[count] = next_bytes;

			if(line_offsets_chars)
				line_offsets_chars[count] = next_chars;

			count++;
		}
	}

	delete[] data;
	return count;
}

//
//	Estimate a whole-file line count from the contiguous indexed prefix.
//	This is only suitable for scrollbar scale until linecount_known() is true.
//
ULONG TextLineIndex::estimate_line_count() const
{
	ULONG indexed_bytes = 0;
	ULONG indexed_breaks = 0;
	ULONG bytes = raw_length();

	if(m_fLineCountKnown || bytes == 0)
		return m_nLineCount;

	for(ULONG i = 0; i < m_nLinePageCount; i++)
	{
		const LinePage *page = &m_pLinePages[i];

		if(!page->indexed || !page->line_base_known)
			break;

		indexed_bytes = page->offset_bytes + page->length_bytes;
		indexed_breaks = page->line_base + page->line_count;
	}

	if(indexed_bytes == 0 || indexed_breaks == 0)
		return 1;

	return estimate_muldiv(indexed_breaks, bytes, indexed_bytes) + 1;
}

//
//	Index the requested range of file pages.
//	Line bases are exact only for the contiguous indexed prefix; pages indexed
//	beyond that prefix keep provisional bases until the gaps are filled.
//
void TextLineIndex::index_lines(ULONG offset_chars, ULONG length_chars)
{
	ULONG bytes = raw_length();
	ULONG offset_bytes = byte_from_char(offset_chars);
	ULONG length_bytes = byte_from_char(offset_chars + length_chars) - offset_bytes;
	ULONG first_page;
	ULONG last_page;
	ULONG line_base = 0;
	bool prev_ends_with_cr = false;
	bool complete = true;

	if(m_pLinePages == 0 || m_nLinePageCount == 0 || length_bytes == 0)
		return;

	if(offset_bytes >= bytes)
		offset_bytes = bytes - 1;

	first_page = offset_bytes / MEM_BLOCK_SIZE;
	last_page = (offset_bytes + length_bytes - 1) / MEM_BLOCK_SIZE;

	if(last_page >= m_nLinePageCount)
		last_page = m_nLinePageCount - 1;

	for(ULONG page_index = first_page; page_index <= last_page; page_index++)
	{
		LinePage *page = &m_pLinePages[page_index];

		if(page->indexed)
			continue;

		page->line_count = scan_lines(page, 0, 0);
		page->line_offsets_bytes = page->line_count ? new ULONG[page->line_count] : 0;
		page->line_offsets_chars = page->line_count ? new ULONG[page->line_count] : 0;

		if(page->line_count)
			scan_lines(page, page->line_offsets_bytes, page->line_offsets_chars);

		page->indexed = true;

		if(!page->line_base_known)
			page->line_base = estimate_muldiv(page->offset_bytes, estimate_line_count(), bytes);

		if(page_index + 1 < m_nLinePageCount)
		{
			LinePage *next = &m_pLinePages[page_index + 1];

			if(!next->offset_known)
			{
				next->offset_chars = page->offset_chars + page->length_chars;
				next->offset_known = true;
			
				if(page->chars_known)
					next->chars_known = true;
			}
		}
	}

	for(ULONG page_index = 0; page_index < m_nLinePageCount; page_index++)
	{
		LinePage *page = &m_pLinePages[page_index];

		if(!page->indexed)
		{
			complete = false;
			break;
		}

		page->line_base = line_base;
		page->line_base_known = true;

		line_base += page->line_count;

		if(prev_ends_with_cr && page->starts_with_lf && line_base > 0)
			line_base--;

		prev_ends_with_cr = page->ends_with_cr;
	}

	if(complete)
	{
		m_nLineCount = line_base + 1;
		m_fLineCountKnown = true;
	}
	else
	{
		m_nLineCount = estimate_line_count();
		m_fLineCountKnown = false;
	}
}

ULONG TextLineIndex::linecount() const
{
	return estimate_line_count();
}

bool TextLineIndex::linecount_known() const
{
	return m_fLineCountKnown;
}

bool TextLineIndex::lineno_known(ULONG lineno) const
{
	if(m_fLineCountKnown)
		return lineno < m_nLineCount;

	for(ULONG i = 0; i < m_nLinePageCount; i++)
	{
		const LinePage *page = &m_pLinePages[i];

		if(!page->indexed || !page->line_base_known)
			break;

		if(lineno >= page->line_base && lineno <= page->line_base + page->line_count)
			return true;
	}

	return false;
}

bool TextLineIndex::line_number_range_known(ULONG offset_chars, ULONG length_chars) const
{
	ULONG offset_bytes = byte_from_char(offset_chars);
	ULONG length_bytes = byte_from_char(offset_chars + length_chars) - offset_bytes;

	if(m_fLineCountKnown)
		return true;

	if(m_pLinePages == 0 || m_nLinePageCount == 0 || length_bytes == 0)
		return false;

	ULONG first_page = offset_bytes / MEM_BLOCK_SIZE;
	ULONG last_page = (offset_bytes + length_bytes - 1) / MEM_BLOCK_SIZE;

	if(last_page >= m_nLinePageCount)
		return false;

	for(ULONG page_index = first_page; page_index <= last_page; page_index++)
	{
		const LinePage *page = &m_pLinePages[page_index];

		if(!page->indexed || !page->line_base_known)
			return false;
	}

	return true;
}

bool TextLineIndex::find_line_start_near_offset(ULONG near_offset_chars, ULONG *offset_chars)
{
	ULONG docLength = text_length();
	ULONG near_offset_bytes = byte_from_char(near_offset_chars);
	ULONG start = near_offset_bytes > MEM_BLOCK_SIZE ? near_offset_bytes - MEM_BLOCK_SIZE : 0;
	ULONG end = min(raw_length(), near_offset_bytes + (ULONG)MEM_BLOCK_SIZE);
	ULONG unit = codeunit_size();
	ULONG last_line_start = char_from_byte(start);
	BYTE *data;
	ULONG bytes_read;

	if(offset_chars == 0)
		return false;

	if(near_offset_chars > docLength)
		near_offset_chars = docLength;

	start -= start % unit;
	data = new BYTE[end - start];
	if(data == 0)
		return false;

	bytes_read = (ULONG)m_pTextDoc->m_seq.render(m_pTextDoc->m_nHeaderSize + start, data, end - start);
	bytes_read -= bytes_read % unit;

	for(ULONG pos_bytes = start; pos_bytes + unit <= start + bytes_read; pos_bytes += unit)
	{
		ULONG index = pos_bytes - start;
		ULONG ch = read_codeunit(m_pTextDoc->m_nFileFormat, data, index, unit);

		if(ch == '\r' || ch == '\n')
		{
			ULONG next_bytes = pos_bytes + unit;
			ULONG next_chars;

			if(ch == '\r' && next_bytes + unit <= start + bytes_read)
			{
				ULONG next_index = next_bytes - start;
				ULONG next_ch = read_codeunit(m_pTextDoc->m_nFileFormat, data, next_index, unit);

				if(next_ch == '\n')
					next_bytes += unit;
			}
			else if(ch == '\r' && next_bytes + unit <= raw_length())
			{
				BYTE nextbuf[2];

				if(m_pTextDoc->m_seq.render(m_pTextDoc->m_nHeaderSize + next_bytes, nextbuf, unit) == unit &&
				   read_codeunit(m_pTextDoc->m_nFileFormat, nextbuf, 0, unit) == '\n')
				{
					next_bytes += unit;
				}
			}

			next_chars = char_from_byte(next_bytes);

			if(next_chars <= near_offset_chars)
				last_line_start = next_chars;
			else
				break;
		}
	}

	*offset_chars = last_line_start;
	delete[] data;
	return true;
}

bool TextLineIndex::next_lineoffset(ULONG lineoff_chars, ULONG *nextoff_chars)
{
	ULONG docLength = text_length();
	ULONG pos_bytes = byte_from_char(lineoff_chars);
	ULONG rawLength = raw_length();
	ULONG unit = codeunit_size();
	BYTE buf[4096];

	if(nextoff_chars == 0 || lineoff_chars >= docLength)
		return false;

	pos_bytes -= pos_bytes % unit;

	while(pos_bytes < rawLength)
	{
		ULONG chunk = min((ULONG)sizeof(buf), rawLength - pos_bytes);
		ULONG got = (ULONG)m_pTextDoc->m_seq.render(m_pTextDoc->m_nHeaderSize + pos_bytes, buf, chunk);

		got -= got % unit;
		if(got == 0)
			break;

		for(ULONG i = 0; i + unit <= got; i += unit)
		{
			ULONG ch = read_codeunit(m_pTextDoc->m_nFileFormat, buf, i, unit);
			ULONG next_bytes;

			if(ch != '\r' && ch != '\n')
				continue;

			next_bytes = pos_bytes + i + unit;

			if(ch == '\r')
			{
				ULONG next_ch = 0;

				if(i + unit + unit <= got)
				{
					next_ch = read_codeunit(m_pTextDoc->m_nFileFormat, buf, i + unit, unit);
				}
				else if(next_bytes + unit <= rawLength)
				{
					BYTE nextbuf[2];
					if(m_pTextDoc->m_seq.render(m_pTextDoc->m_nHeaderSize + next_bytes, nextbuf, unit) == unit)
						next_ch = read_codeunit(m_pTextDoc->m_nFileFormat, nextbuf, 0, unit);
				}

				if(next_ch == '\n')
					next_bytes += unit;
			}

			*nextoff_chars = char_from_byte(next_bytes);
			return true;
		}

		pos_bytes += got;
	}

	return false;
}

//
//	Map a line number to an offset. Prefer exact indexed prefix data, then
//	locally indexed provisional pages, then the final-page EOF guard, and only
//	then fall back to an estimated nearby offset.
//
bool TextLineIndex::lineoffset_from_lineno(ULONG lineno, ULONG *offset_chars)
{
	ULONG docLength = text_length();

	if(offset_chars == 0 || docLength == 0)
		return false;

	if(m_fLineCountKnown && lineno >= m_nLineCount)
		return false;

	if(lineno == 0)
	{
		*offset_chars = 0;
		return true;
	}

	for(ULONG i = 0; i < m_nLinePageCount; i++)
	{
		LinePage *page = &m_pLinePages[i];

		if(!page->indexed || !page->line_base_known)
			break;

		if(lineno > page->line_base + page->line_count)
			continue;

		ULONG local_line = lineno - page->line_base;

		if(local_line == 0)
			return find_line_start_near_offset(page->offset_chars, offset_chars);

		if(local_line - 1 < page->line_count)
		{
			ULONG lineoff = page->line_offsets_chars[local_line - 1];

			// A CR at the end of one page and LF at the start of the next are one line break.
			if(page->line_offsets_bytes[local_line - 1] == page->offset_bytes + page->length_bytes && page->ends_with_cr)
			{
				ULONG nextch32 = 0;

				if(m_pTextDoc->decode_char(page->line_offsets_bytes[local_line - 1], raw_length() - page->line_offsets_bytes[local_line - 1], &nextch32) && nextch32 == '\n')
					lineoff++;
			}

			*offset_chars = lineoff;
			return true;
		}
	}

	for(ULONG i = 0; i < m_nLinePageCount; i++)
	{
		LinePage *page = &m_pLinePages[i];

		if(!page->indexed || page->line_base_known)
			continue;

		if(lineno <= page->line_base || lineno > page->line_base + page->line_count)
			continue;

		ULONG local_line = lineno - page->line_base;

		if(local_line - 1 < page->line_count)
		{
			*offset_chars = page->line_offsets_chars[local_line - 1];
			return true;
		}
	}

	if(m_nLinePageCount > 0)
	{
		LinePage *last_page = &m_pLinePages[m_nLinePageCount - 1];

		// The final indexed page proves where EOF is, even if its global line number is only estimated.
		if(last_page->indexed && !last_page->line_base_known && lineno > last_page->line_base + last_page->line_count)
			return false;
	}

	ULONG estimate = estimate_muldiv(lineno, docLength, estimate_line_count());
	return find_line_start_near_offset(estimate, offset_chars);
}

bool TextLineIndex::lineinfo_from_lineno(ULONG lineno, TextLineIndexInfo *lineinfo)
{
	ULONG lineoff;
	ULONG nextoff;

	if(lineinfo == 0)
		return false;

	if(!lineoffset_from_lineno(lineno, &lineoff))
		return false;

	if(!next_lineoffset(lineoff, &nextoff))
		nextoff = text_length();

	index_lines(lineoff, max((ULONG)1, nextoff - lineoff));

	lineinfo->lineno = lineno;
	lineinfo->lineoff_chars = lineoff;
	lineinfo->linelen_chars = nextoff - lineoff;
	lineinfo->lineoff_bytes = byte_from_char(lineoff);
	lineinfo->linelen_bytes = byte_from_char(nextoff) - lineinfo->lineoff_bytes;

	lineinfo->chars_known = direct_offset_mapping();

	return true;
}

//
//	Map an exact offset to its containing line. The offset and returned line
//	range are usable immediately, but the line number may still be provisional.
//
bool TextLineIndex::lineinfo_from_offset(ULONG offset_chars, TextLineIndexInfo *lineinfo)
{
	ULONG docLength = text_length();
	ULONG offset_bytes;
	ULONG line = 0;
	ULONG lineoff = 0;
	ULONG nextoff = 0;
	ULONG unit = codeunit_size();

	if(lineinfo == 0 || docLength == 0)
		return false;

	if(offset_chars > docLength)
		offset_chars = docLength;

	index_lines(offset_chars < docLength ? offset_chars : docLength - 1, 1);

	offset_bytes = byte_from_char(offset_chars == docLength ? docLength - 1 : offset_chars);

	ULONG page_index = offset_bytes / MEM_BLOCK_SIZE;
	LinePage *page = &m_pLinePages[page_index];

	if(page->indexed)
	{
		line = page->line_base;
		lineoff = page->offset_chars;

		if(page->starts_with_lf && page->offset_bytes >= unit)
		{
			BYTE prevbuf[2];

			if(m_pTextDoc->m_seq.render(m_pTextDoc->m_nHeaderSize + page->offset_bytes - unit, prevbuf, unit) == unit &&
			   read_codeunit(m_pTextDoc->m_nFileFormat, prevbuf, 0, unit) == '\r')
			{
				if(line > 0)
					line--;

				if(lineoff > 0)
					find_line_start_near_offset(lineoff, &lineoff);
			}
		}

		for(ULONG i = 0; i < page->line_count; i++)
		{
			ULONG break_chars = page->line_offsets_chars[i];

			// A CR at the end of one page and LF at the start of the next are one line break.
			if(page->line_offsets_bytes[i] == page->offset_bytes + page->length_bytes && page->ends_with_cr)
			{
				ULONG nextch32 = 0;

				if(m_pTextDoc->decode_char(page->line_offsets_bytes[i], raw_length() - page->line_offsets_bytes[i], &nextch32) && nextch32 == '\n')
					break_chars++;
			}

			if(break_chars > offset_chars)
				break;

			line++;
			lineoff = break_chars;
		}

		if(lineoff == page->offset_chars && page->offset_chars > 0)
			find_line_start_near_offset(page->offset_chars, &lineoff);
	}
	else
	{
		line = estimate_muldiv(offset_chars, estimate_line_count(), docLength);
		find_line_start_near_offset(offset_chars, &lineoff);
	}

	if(!next_lineoffset(lineoff, &nextoff))
		nextoff = docLength;

	index_lines(lineoff, max((ULONG)1, nextoff - lineoff));

	lineinfo->lineno = line;
	lineinfo->lineoff_chars = lineoff;
	lineinfo->linelen_chars = nextoff - lineoff;
	lineinfo->lineoff_bytes = byte_from_char(lineoff);
	lineinfo->linelen_bytes = byte_from_char(nextoff) - lineinfo->lineoff_bytes;

	lineinfo->chars_known = direct_offset_mapping();

	return true;
}
