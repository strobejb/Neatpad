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
		page->offset_chars = char_from_byte(page->offset_bytes);
		page->length_chars = char_from_byte(page->offset_bytes + page->length_bytes) - page->offset_chars;
		page->line_offsets_bytes = 0;
		page->line_offsets_chars = 0;
		page->line_count = 0;
		page->line_base = 0;
		page->indexed = false;
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

ULONG TextLineIndex::byte_from_char(ULONG offset_chars) const
{
	switch(m_pTextDoc->m_nFileFormat)
	{
	case NCP_UTF16:
	case NCP_UTF16BE:
		return offset_chars * sizeof(WCHAR);

	case NCP_ASCII:
	default:
		return offset_chars;
	}
}

ULONG TextLineIndex::char_from_byte(ULONG offset_bytes) const
{
	switch(m_pTextDoc->m_nFileFormat)
	{
	case NCP_UTF16:
	case NCP_UTF16BE:
		return offset_bytes / sizeof(WCHAR);

	case NCP_ASCII:
	default:
		return offset_bytes;
	}
}

ULONG TextLineIndex::utf16_length(ULONG ch32) const
{
	return ch32 > 0xffff ? 2 : 1;
}

bool TextLineIndex::is_linebreak_char(ULONG ch32) const
{
	return ch32 == '\r' || ch32 == '\n' ||
		ch32 == '\x0b' || ch32 == '\x0c' ||
		ch32 == 0x0085 || ch32 == 0x2028 ||
		ch32 == 0x2029;
}

ULONG TextLineIndex::scan_lines(LinePage *page, ULONG *line_offsets_bytes, ULONG *line_offsets_chars)
{
	ULONG count = 0;
	ULONG pos_bytes = page->offset_bytes;
	ULONG pos_chars = page->offset_chars;
	ULONG end = page->offset_bytes + page->length_bytes;
	bool first_char = true;

	page->starts_with_lf = false;
	page->ends_with_cr = false;
	page->length_chars = 0;

	while(pos_bytes < end)
	{
		ULONG ch32 = 0;
		ULONG len = m_pTextDoc->decode_char(pos_bytes, end - pos_bytes, &ch32);
		ULONG next_bytes;
		ULONG next_chars;

		if(len == 0)
			break;

		next_bytes = pos_bytes + len;
		next_chars = pos_chars + utf16_length(ch32);

		if(first_char)
		{
			page->starts_with_lf = ch32 == '\n';
			first_char = false;
		}

		page->ends_with_cr = ch32 == '\r';

		if(ch32 == '\r')
		{
			ULONG nextch32 = 0;
			ULONG nextlen = 0;

			if(next_bytes < end)
				nextlen = m_pTextDoc->decode_char(next_bytes, end - next_bytes, &nextch32);

			if(nextlen && nextch32 == '\n')
			{
				next_bytes += nextlen;
				next_chars += utf16_length(nextch32);
				page->ends_with_cr = false;
			}

			if(line_offsets_bytes)
				line_offsets_bytes[count] = next_bytes;

			if(line_offsets_chars)
				line_offsets_chars[count] = next_chars;

			count++;
		}
		else if(is_linebreak_char(ch32))
		{
			if(line_offsets_bytes)
				line_offsets_bytes[count] = next_bytes;

			if(line_offsets_chars)
				line_offsets_chars[count] = next_chars;

			count++;
		}

		pos_bytes = next_bytes;
		pos_chars = next_chars;
	}

	page->length_chars = pos_chars - page->offset_chars;
	return count;
}

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
	ULONG pos_bytes = start;
	ULONG pos_chars = char_from_byte(start);
	ULONG last_line_start = pos_chars;

	if(offset_chars == 0)
		return false;

	if(near_offset_chars > docLength)
		near_offset_chars = docLength;

	while(pos_bytes < end)
	{
		ULONG ch32 = 0;
		ULONG len = m_pTextDoc->decode_char(pos_bytes, end - pos_bytes, &ch32);
		ULONG next_bytes;
		ULONG next_chars;

		if(len == 0)
			break;

		next_bytes = pos_bytes + len;
		next_chars = pos_chars + utf16_length(ch32);

		if(ch32 == '\r')
		{
			ULONG nextch32 = 0;
			ULONG nextlen = 0;

			if(next_bytes < end)
				nextlen = m_pTextDoc->decode_char(next_bytes, end - next_bytes, &nextch32);

			if(nextlen && nextch32 == '\n')
			{
				next_bytes += nextlen;
				next_chars += utf16_length(nextch32);
			}

			if(next_chars <= near_offset_chars)
				last_line_start = next_chars;
			else
				break;
		}
		else if(is_linebreak_char(ch32))
		{
			if(next_chars <= near_offset_chars)
				last_line_start = next_chars;
			else
				break;
		}

		pos_bytes = next_bytes;
		pos_chars = next_chars;
	}

	*offset_chars = last_line_start;
	return true;
}

bool TextLineIndex::next_lineoffset(ULONG lineoff_chars, ULONG *nextoff_chars)
{
	ULONG docLength = text_length();
	ULONG pos_bytes = byte_from_char(lineoff_chars);
	ULONG pos_chars = lineoff_chars;
	ULONG rawLength = raw_length();

	if(nextoff_chars == 0 || lineoff_chars >= docLength)
		return false;

	while(pos_bytes < rawLength)
	{
		ULONG ch32 = 0;
		ULONG len = m_pTextDoc->decode_char(pos_bytes, rawLength - pos_bytes, &ch32);
		ULONG next_bytes;
		ULONG next_chars;

		if(len == 0)
			break;

		next_bytes = pos_bytes + len;
		next_chars = pos_chars + utf16_length(ch32);

		if(ch32 == '\r')
		{
			ULONG nextch32 = 0;
			ULONG nextlen = 0;

			if(next_bytes < rawLength)
				nextlen = m_pTextDoc->decode_char(next_bytes, rawLength - next_bytes, &nextch32);

			if(nextlen && nextch32 == '\n')
			{
				next_bytes += nextlen;
				next_chars += utf16_length(nextch32);
			}

			*nextoff_chars = next_chars;
			return true;
		}
		else if(is_linebreak_char(ch32))
		{
			*nextoff_chars = next_chars;
			return true;
		}

		pos_bytes = next_bytes;
		pos_chars = next_chars;
	}

	return false;
}

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

	return true;
}

bool TextLineIndex::lineinfo_from_offset(ULONG offset_chars, TextLineIndexInfo *lineinfo)
{
	ULONG docLength = text_length();
	ULONG offset_bytes;
	ULONG line = 0;
	ULONG lineoff = 0;
	ULONG nextoff = 0;

	if(lineinfo == 0 || docLength == 0)
		return false;

	if(offset_chars > docLength)
		offset_chars = docLength;

	index_lines(offset_chars < docLength ? offset_chars : docLength - 1, 1);

	offset_bytes = byte_from_char(offset_chars == docLength ? docLength - 1 : offset_chars);

	ULONG page_index = offset_bytes / MEM_BLOCK_SIZE;
	LinePage *page = &m_pLinePages[page_index];

	if(page->indexed && page->line_base_known)
	{
		line = page->line_base;
		lineoff = page->offset_chars;

		for(ULONG i = 0; i < page->line_count && page->line_offsets_chars[i] <= offset_chars; i++)
		{
			line++;
			lineoff = page->line_offsets_chars[i];
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

	return true;
}
