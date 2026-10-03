//
//	MODULE:		TextLineBuffer.cpp
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

TextLineBuffer::TextLineBuffer()
{
	m_pTextDoc = 0;
	m_pLinePages = 0;
	m_nLinePageCount = 0;
	m_nLineCount = 0;
	m_fLineCountKnown = false;
}

TextLineBuffer::~TextLineBuffer()
{
	clear();
}

bool TextLineBuffer::init(TextDocument *doc)
{
	ULONG bytes;

	clear();

	m_pTextDoc = doc;
	bytes = text_length();

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
		page->line_offsets = 0;
		page->line_count = 0;
		page->line_base = 0;
		page->indexed = false;
		page->line_base_known = false;
		page->starts_with_lf = false;
		page->ends_with_cr = false;
	}

	index_lines(0, min(bytes, (ULONG)MEM_BLOCK_SIZE));
	return true;
}

void TextLineBuffer::clear()
{
	if(m_pLinePages)
	{
		for(ULONG i = 0; i < m_nLinePageCount; i++)
			delete[] m_pLinePages[i].line_offsets;

		delete[] m_pLinePages;
	}

	m_pTextDoc = 0;
	m_pLinePages = 0;
	m_nLinePageCount = 0;
	m_nLineCount = 0;
	m_fLineCountKnown = false;
}

ULONG TextLineBuffer::text_length() const
{
	return m_pTextDoc ? m_pTextDoc->text_length() : 0;
}

ULONG TextLineBuffer::scan_lines(ULONG offset_bytes, ULONG length_bytes, ULONG *line_offsets)
{
	BYTE buf[4096];
	ULONG count = 0;
	ULONG pos = offset_bytes;
	ULONG end = offset_bytes + length_bytes;

	while(pos < end)
	{
		ULONG buflen = min((ULONG)sizeof(buf), end - pos);
		ULONG rendered = (ULONG)m_pTextDoc->m_seq.render(m_pTextDoc->m_nHeaderSize + pos, buf, buflen);

		if(rendered == 0)
			break;

		for(ULONG i = 0; i < rendered; i++)
		{
			BYTE ch = buf[i];
			ULONG chpos = pos + i;

			if(ch == '\r')
			{
				ULONG next = chpos + 1;

				if(next < end)
				{
					BYTE nextch = 0;

					if(i + 1 < rendered)
						nextch = buf[i + 1];
					else
						m_pTextDoc->m_seq.render(m_pTextDoc->m_nHeaderSize + next, &nextch, 1);

					if(nextch == '\n')
					{
						next++;

						if(i + 1 < rendered)
							i++;
					}
				}

				if(line_offsets)
					line_offsets[count] = next;

				count++;
			}
			else if(ch == '\n')
			{
				if(line_offsets)
					line_offsets[count] = chpos + 1;

				count++;
			}
		}

		pos += rendered;
	}

	return count;
}

ULONG TextLineBuffer::estimate_line_count() const
{
	ULONG indexed_bytes = 0;
	ULONG indexed_breaks = 0;
	ULONG bytes = text_length();

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

void TextLineBuffer::index_lines(ULONG offset_chars, ULONG length_chars)
{
	ULONG bytes = text_length();
	ULONG first_page;
	ULONG last_page;
	ULONG line_base = 0;
	bool prev_ends_with_cr = false;
	bool complete = true;

	if(m_pLinePages == 0 || m_nLinePageCount == 0 || length_chars == 0)
		return;

	if(offset_chars >= bytes)
		offset_chars = bytes - 1;

	first_page = offset_chars / MEM_BLOCK_SIZE;
	last_page = (offset_chars + length_chars - 1) / MEM_BLOCK_SIZE;

	if(last_page >= m_nLinePageCount)
		last_page = m_nLinePageCount - 1;

	for(ULONG page_index = first_page; page_index <= last_page; page_index++)
	{
		LinePage *page = &m_pLinePages[page_index];

		if(page->indexed)
			continue;

		page->line_count = scan_lines(page->offset_bytes, page->length_bytes, 0);
		page->line_offsets = page->line_count ? new ULONG[page->line_count] : 0;

		if(page->line_count)
			scan_lines(page->offset_bytes, page->length_bytes, page->line_offsets);

		if(page->length_bytes)
		{
			BYTE first = 0;
			BYTE last = 0;

			m_pTextDoc->m_seq.render(m_pTextDoc->m_nHeaderSize + page->offset_bytes, &first, 1);
			m_pTextDoc->m_seq.render(m_pTextDoc->m_nHeaderSize + page->offset_bytes + page->length_bytes - 1, &last, 1);

			page->starts_with_lf = first == '\n';
			page->ends_with_cr = last == '\r';
		}

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

ULONG TextLineBuffer::linecount() const
{
	return estimate_line_count();
}

bool TextLineBuffer::linecount_known() const
{
	return m_fLineCountKnown;
}

bool TextLineBuffer::lineno_known(ULONG lineno) const
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

bool TextLineBuffer::line_number_range_known(ULONG offset_chars, ULONG length_chars) const
{
	if(m_fLineCountKnown)
		return true;

	if(m_pLinePages == 0 || m_nLinePageCount == 0 || length_chars == 0)
		return false;

	ULONG first_page = offset_chars / MEM_BLOCK_SIZE;
	ULONG last_page = (offset_chars + length_chars - 1) / MEM_BLOCK_SIZE;

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

bool TextLineBuffer::find_line_start_near_offset(ULONG near_offset_chars, ULONG *offset_chars)
{
	ULONG docLength = text_length();
	ULONG start = near_offset_chars > MEM_BLOCK_SIZE ? near_offset_chars - MEM_BLOCK_SIZE : 0;
	ULONG end = min(docLength, near_offset_chars + (ULONG)MEM_BLOCK_SIZE);
	ULONG last_line_start = start;
	ULONG pos = start;

	if(offset_chars == 0)
		return false;

	if(near_offset_chars > docLength)
		near_offset_chars = docLength;

	while(pos < end)
	{
		BYTE ch = 0;

		if(m_pTextDoc->m_seq.render(m_pTextDoc->m_nHeaderSize + pos, &ch, 1) != 1)
			break;

		if(ch == '\r')
		{
			pos++;

			if(pos < end)
			{
				BYTE nextch = 0;

				if(m_pTextDoc->m_seq.render(m_pTextDoc->m_nHeaderSize + pos, &nextch, 1) == 1 && nextch == '\n')
					pos++;
			}

			if(pos <= near_offset_chars)
				last_line_start = pos;
			else
				break;
		}
		else if(ch == '\n')
		{
			pos++;

			if(pos <= near_offset_chars)
				last_line_start = pos;
			else
				break;
		}
		else
		{
			pos++;
		}
	}

	*offset_chars = last_line_start;
	return true;
}

bool TextLineBuffer::next_lineoffset(ULONG lineoff_chars, ULONG *nextoff_chars)
{
	ULONG docLength = text_length();
	ULONG pos = lineoff_chars;

	if(nextoff_chars == 0 || lineoff_chars >= docLength)
		return false;

	while(pos < docLength)
	{
		BYTE ch = 0;

		if(m_pTextDoc->m_seq.render(m_pTextDoc->m_nHeaderSize + pos, &ch, 1) != 1)
			break;

		if(ch == '\r')
		{
			pos++;

			if(pos < docLength)
			{
				BYTE nextch = 0;

				if(m_pTextDoc->m_seq.render(m_pTextDoc->m_nHeaderSize + pos, &nextch, 1) == 1 && nextch == '\n')
					pos++;
			}

			*nextoff_chars = pos;
			return true;
		}
		else if(ch == '\n')
		{
			*nextoff_chars = pos + 1;
			return true;
		}

		pos++;
	}

	return false;
}

bool TextLineBuffer::lineoffset_from_lineno(ULONG lineno, ULONG *offset_chars)
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
			return find_line_start_near_offset(page->offset_bytes, offset_chars);

		if(local_line - 1 < page->line_count)
		{
			ULONG lineoff = page->line_offsets[local_line - 1];

			// A CR at the end of one page and LF at the start of the next are one line break.
			if(lineoff == page->offset_bytes + page->length_bytes && page->ends_with_cr)
			{
				BYTE nextch = 0;

				if(m_pTextDoc->m_seq.render(m_pTextDoc->m_nHeaderSize + lineoff, &nextch, 1) == 1 && nextch == '\n')
					lineoff++;
			}

			*offset_chars = lineoff;
			return true;
		}
	}

	ULONG estimate = estimate_muldiv(lineno, docLength, estimate_line_count());
	return find_line_start_near_offset(estimate, offset_chars);
}

bool TextLineBuffer::lineinfo_from_lineno(ULONG lineno, TextLineBufferInfo *lineinfo)
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
	lineinfo->lineoff_bytes = lineoff;
	lineinfo->linelen_bytes = nextoff - lineoff;

	return true;
}

bool TextLineBuffer::lineinfo_from_offset(ULONG offset_chars, TextLineBufferInfo *lineinfo)
{
	ULONG docLength = text_length();
	ULONG line = 0;
	ULONG lineoff = 0;
	ULONG nextoff = 0;

	if(lineinfo == 0 || docLength == 0)
		return false;

	if(offset_chars > docLength)
		offset_chars = docLength;

	index_lines(offset_chars < docLength ? offset_chars : docLength - 1, 1);

	ULONG page_index = offset_chars == docLength ? (docLength - 1) / MEM_BLOCK_SIZE : offset_chars / MEM_BLOCK_SIZE;
	LinePage *page = &m_pLinePages[page_index];

	if(page->indexed && page->line_base_known)
	{
		line = page->line_base;
		lineoff = page->offset_bytes;

		for(ULONG i = 0; i < page->line_count && page->line_offsets[i] <= offset_chars; i++)
		{
			line++;
			lineoff = page->line_offsets[i];
		}

		if(lineoff == page->offset_bytes && page->offset_bytes > 0)
			find_line_start_near_offset(page->offset_bytes, &lineoff);
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
	lineinfo->lineoff_bytes = lineoff;
	lineinfo->linelen_bytes = nextoff - lineoff;

	return true;
}
