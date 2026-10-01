/*
	sequence_lines.cpp

	Line indexing for the data-sequence class.
*/

#include <windows.h>
#include "sequence.h"

//
//	sequence::buffer_control::scan_lines
//
//  Count line breaks in a buffer range, optionally recording each line start.
//
size_w sequence::buffer_control::scan_lines(size_w offset, size_w length, size_w *line_offsets)
{
	size_w line                 = 0;
	bool   pending_cr           = false;
	size_w pending_line_offset  = 0;
	const size_w scan_size      = MEM_BLOCK_SIZE;
	size_w end                  = offset + length;

	// Scan the requested range in page-sized chunks.
	for(; offset < end; )
	{
		size_w chunk_length = min(scan_size, end - offset);
		seqchar *source = getptr(offset, chunk_length);

		if(source == 0)
			return line;

		// Walk each byte in the chunk and detect CR, LF and CRLF line breaks.
		for(size_w i = 0; i < chunk_length; i++)
		{
			size_w absolute = offset + i;
			seqchar ch = source[i];

			if(pending_cr)
			{
				if(ch == '\n')
				{
					if(line_offsets)
						line_offsets[line] = absolute + 1;

					line++;
					pending_cr = false;
					continue;
				}

				if(line_offsets)
					line_offsets[line] = pending_line_offset;

				line++;
				pending_cr = false;
			}

			if(ch == '\r')
			{
				pending_cr = true;
				pending_line_offset = absolute + 1;
			}
			else if(ch == '\n')
			{
				if(line_offsets)
					line_offsets[line] = absolute + 1;

				line++;
			}
		}

		offset += chunk_length;
	}

	if(pending_cr)
	{
		if(line_offsets)
			line_offsets[line] = pending_line_offset;

		line++;
	}

	return line;
}

size_w sequence::buffer_control::known_line_count() const
{
	size_w breaks = 0;
	bool prev_ends_with_cr = false;
	bool found_indexed_page = false;

	if(line_count_known)
		return line_count;

	if(line_pages == 0 || line_page_count == 0)
		return 0;

	for(size_w page_index = 0; page_index < line_page_count; page_index++)
	{
		const line_page *page = &line_pages[page_index];

		if(!page->indexed || !page->line_base_known)
			break;

		breaks += page->line_count;

		if(prev_ends_with_cr && page->starts_with_lf && breaks > 0)
			breaks--;

		prev_ends_with_cr = page->ends_with_cr;
		found_indexed_page = true;
	}

	return found_indexed_page ? breaks + 1 : 0;
}

//
//	sequence::buffer_control::build_line_index
//
//  Build lazy line metadata for each page covered by the requested range.
//
void sequence::buffer_control::build_line_index(size_w offset, size_w length)
{
	if(line_pages == 0 || line_page_count == 0 || length == 0)
		return;

	size_w first_page = offset / MEM_BLOCK_SIZE;
	size_w last_page = (offset + length - 1) / MEM_BLOCK_SIZE;

	if(last_page >= line_page_count)
		last_page = line_page_count - 1;

	// Scan any covered pages that do not already have line metadata.
	for(size_w page_index = first_page; page_index <= last_page; page_index++)
	{
		line_page *page = &line_pages[page_index];

		if(page->indexed)
			continue;

		size_w count = scan_lines(page->offset, page->length, 0);

		page->line_offsets = count ? new size_w[count] : 0;
		page->line_count = count;

		if(count)
			scan_lines(page->offset, page->length, page->line_offsets);

		if(page->length)
		{
			seqchar *first = getptr(page->offset, 1);
			seqchar *last = getptr(page->offset + page->length - 1, 1);

			page->starts_with_lf = first && *first == '\n';
			page->ends_with_cr = last && *last == '\r';
		}

		page->indexed = true;
	}

	size_w line_base = 0;
	bool prev_ends_with_cr = false;
	bool complete = true;

	// Assign absolute line numbers to the contiguous indexed prefix.
	for(size_w page_index = 0; page_index < line_page_count; page_index++)
	{
		line_page *page = &line_pages[page_index];

		if(!page->indexed)
		{
			complete = false;
			break;
		}

		// Record the absolute line number of this page's first line.
		page->line_base = line_base;
		page->line_base_known = true;

		line_base += page->line_count;

		if(prev_ends_with_cr && page->starts_with_lf && line_base > 0)
			line_base--;

		prev_ends_with_cr = page->ends_with_cr;
	}

	if(complete)
	{
		line_count = line_base + 1;
		line_count_known = true;
	}
}

//
//	sequence::buffer_control::lines_known
//
//  Return true when a range's page line metadata and line bases are known.
//
bool sequence::buffer_control::lines_known(size_w offset, size_w length) const
{
	if(line_count_known)
		return true;

	if(line_pages == 0 || line_page_count == 0 || length == 0)
		return false;

	size_w first_page = offset / MEM_BLOCK_SIZE;
	size_w last_page = (offset + length - 1) / MEM_BLOCK_SIZE;

	if(last_page >= line_page_count)
		return false;

	// Check every page touched by the requested byte range.
	for(size_w page_index = first_page; page_index <= last_page; page_index++)
	{
		const line_page *page = &line_pages[page_index];

		if(!page->indexed || !page->line_base_known)
			return false;
	}

	return true;
}

void sequence::buffer_control::update_lines()
{
	size_w count = 0;

	delete[] line_offsets;
	line_offsets = 0;
	line_count = 0;

	if(length == 0)
		return;

	count = scan_lines(0, length, 0) + 1;

	line_offsets = new size_w[count];
	line_count = count;
	line_count_known = true;
	line_offsets[0] = 0;

	scan_lines(0, length, line_offsets + 1);
}

void sequence::update_buffer_lines(buffer_control *bc)
{
	bc->update_lines();
}

bool sequence::next_lineoffset(size_w lineoff, size_w *nextoff) const
{
	const size_w scan_size = 0x1000;
	seqchar buffer[scan_size];
	size_w offset = lineoff;
	bool pending_cr = false;
	size_w cr_offset = 0;

	if(nextoff == 0 || lineoff > sequence_length)
		return false;

	while(offset < sequence_length)
	{
		size_w length = min(scan_size, sequence_length - offset);
		size_w rendered = render(offset, buffer, length);

		if(rendered == 0)
			return false;

		for(size_w i = 0; i < rendered; i++)
		{
			size_w pos = offset + i;
			seqchar ch = buffer[i];

			if(pending_cr)
			{
				*nextoff = ch == '\n' ? pos + 1 : cr_offset + 1;
				return true;
			}

			if(ch == '\r')
			{
				pending_cr = true;
				cr_offset = pos;
			}
			else if(ch == '\n')
			{
				*nextoff = pos + 1;
				return true;
			}
		}

		offset += rendered;
	}

	*nextoff = pending_cr ? cr_offset + 1 : sequence_length;
	return true;
}
