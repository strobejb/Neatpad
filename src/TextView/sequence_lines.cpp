/*
	sequence_lines.cpp

	Line indexing for the data-sequence class.
*/

#include <windows.h>
#include "sequence.h"

//
//	estimate_muldiv
//
//  Estimate (numerator * multiplier) / denominator without overflowing size_w.
//
static size_w estimate_muldiv(size_w numerator, size_w multiplier, size_w denominator)
{
	size_w quotient;
	size_w remainder;
	size_w result;
	size_w term_quotient;
	size_w term_remainder;
	size_w accumulated_remainder;

	if (denominator == 0 || numerator == 0 || multiplier == 0)
		return 0;

	quotient = numerator / denominator;
	remainder = numerator % denominator;

	if (quotient > MAX_SEQUENCE_LENGTH / multiplier)
		return MAX_SEQUENCE_LENGTH;

	result = quotient * multiplier;

	term_quotient = 0;
	term_remainder = remainder;
	accumulated_remainder = 0;

	// Add the remainder contribution one binary term at a time:
	// (remainder * multiplier) / denominator, without ever forming
	// remainder * multiplier as a single potentially overflowing product.
	for (size_w bits = multiplier; bits; bits >>= 1)
	{
		if (bits & 1)
		{
			if (term_quotient > MAX_SEQUENCE_LENGTH - result)
				return MAX_SEQUENCE_LENGTH;

			result += term_quotient;

			if (term_remainder != 0)
			{
				if (accumulated_remainder >= denominator - term_remainder)
				{
					accumulated_remainder -= denominator - term_remainder;

					if (result == MAX_SEQUENCE_LENGTH)
						return MAX_SEQUENCE_LENGTH;

					result++;
				}
				else
				{
					accumulated_remainder += term_remainder;
				}
			}
		}

		if (bits > 1)
		{
			size_w carry = 0;

			if (term_remainder >= denominator - term_remainder)
			{
				term_remainder -= denominator - term_remainder;
				carry = 1;
			}
			else
			{
				term_remainder += term_remainder;
			}

			if (term_quotient > (MAX_SEQUENCE_LENGTH - carry) / 2)
				term_quotient = MAX_SEQUENCE_LENGTH;
			else
				term_quotient = term_quotient * 2 + carry;
		}
	}

	return result;
}


//
//	buffer_control::scan_lines
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

	// Only the contiguous prefix has trustworthy absolute line numbers.
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

//
//	buffer_control::first_line_after
//
//  Find the first recorded line start after the specified buffer offset.
//
size_w sequence::buffer_control::first_line_after(size_w offset) const
{
	size_w lo = 0;
	size_w hi = this->line_count;

	while (lo < hi)
	{
		size_w mid = lo + (hi - lo) / 2;

		if (this->line_offsets[mid] <= offset)
			lo = mid + 1;
		else
			hi = mid;
	}

	return lo;
}

void sequence::update_buffer_lines(buffer_control *bc)
{
	bc->update_lines();
}

//
//	sequence::next_lineoffset
//
//  Find the next physical line start after lineoff, scanning forward only as far as needed.
//
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

size_w sequence::linecount() const
{
	size_w breaks = 0;
	bool prev_ends_with_cr = false;

	if (sequence_length == 0)
		return 0;

	if (!linecount_known())
	{
		size_w indexed_bytes = 0;
		size_w indexed_breaks = 0;

		// Estimate total lines from whatever lazy pages have already been indexed.
		for (size_t i = 0; i < buffer_list.size(); i++)
		{
			buffer_control* bc = buffer_list[i];

			if (bc->line_count_known)
			{
				indexed_bytes += bc->length;
				indexed_breaks += bc->line_count > 0 ? bc->line_count - 1 : 0;
				continue;
			}

			for (size_w page_index = 0; page_index < bc->line_page_count; page_index++)
			{
				buffer_control::line_page* page = &bc->line_pages[page_index];

				if (!page->indexed)
					continue;

				indexed_bytes += page->length;
				indexed_breaks += page->line_count;
			}
		}

		if (indexed_bytes == 0 || indexed_breaks == 0)
			return 1;

		return estimate_muldiv(indexed_breaks, sequence_length, indexed_bytes) + 1;
	}

	for (span* sptr = head->next; sptr != tail; sptr = sptr->next)
	{
		breaks += sptr->line_count;

		if (prev_ends_with_cr && sptr->starts_with_lf)
			breaks--;

		prev_ends_with_cr = sptr->ends_with_cr ? true : false;
	}

	return breaks + 1;
}

bool sequence::linecount_known() const
{
	for (size_t i = 0; i < buffer_list.size(); i++)
	{
		if (!buffer_list[i]->line_count_known)
			return false;
	}

	return true;
}

//
//	sequence::line_number_known
//
//	Return true if this line number is part of the indexed prefix.
//
bool sequence::line_number_known(size_w line) const
{
	size_w current_line = 0;
	bool prev_ends_with_cr = false;

	if (sequence_length == 0)
		return false;

	if (linecount_known())
		return line < linecount();

	// With lazy buffers, only the indexed prefix can claim exact line numbers.
	for (span* sptr = head->next; sptr != tail; sptr = sptr->next)
	{
		buffer_control* bc = buffer_list[sptr->buffer];

		if (!bc->line_count_known)
		{
			size_w known_lines;

			if (sptr->offset != 0)
				return false;

			known_lines = bc->known_line_count();

			return line < current_line + known_lines;
		}

		size_w effective_line_count = sptr->line_count;

		if (prev_ends_with_cr && sptr->starts_with_lf && effective_line_count > 0)
			effective_line_count--;

		if (line <= current_line + effective_line_count)
			return true;

		current_line += effective_line_count;
		prev_ends_with_cr = sptr->ends_with_cr ? true : false;
	}

	return false;
}

//
//	sequence::line_numbers_known
//
//	Return true if every page touched by this sequence range has exact line metadata.
//
bool sequence::line_numbers_known(size_w offset, size_w length) const
{
	size_w spanindex = 0;
	size_w end;

	if (length == 0)
		return true;

	if (offset > sequence_length || length > sequence_length - offset)
		return false;

	end = offset + length;

	for (span* sptr = head->next; sptr != tail && spanindex < end; sptr = sptr->next)
	{
		size_w check_start = 0;
		size_w check_length = sptr->length;
		buffer_control* bc = buffer_list[sptr->buffer];

		if (spanindex + check_length <= offset)
		{
			if (!bc->lines_known(sptr->offset, sptr->length))
				return false;

			spanindex += sptr->length;
			continue;
		}

		if (offset > spanindex)
		{
			check_start = offset - spanindex;
			check_length -= check_start;
		}

		if (spanindex + check_start + check_length > end)
			check_length = end - spanindex - check_start;

		if (!bc->lines_known(sptr->offset + check_start, check_length))
			return false;

		spanindex += sptr->length;
	}

	return true;
}

//
//	sequence::index_lines
//
//	Build lazy line indexes for the backing buffers covered by this sequence range.
//
void sequence::index_lines(size_w offset, size_w length)
{
	size_w spanoffset = 0;
	span* sptr;

	if (length == 0 || offset > sequence_length || length > sequence_length - offset)
		return;

	if ((sptr = spanfromindex(offset, &spanoffset)) == 0)
		return;

	spanoffset = offset - spanoffset;

	// Index each backing buffer range touched by the requested sequence range.
	while (length && sptr != tail)
	{
		size_w index_length = min(sptr->length - spanoffset, length);
		buffer_control* bc = buffer_list[sptr->buffer];

		bc->build_line_index(sptr->offset + spanoffset, index_length);

		spanoffset = 0;
		length -= index_length;
		sptr = sptr->next;
	}
}



//
//	sequence::exact_scan_offset_from_line
//
//	Find a line's offset by scanning from the start, stopping at max_offset.
//
bool sequence::exact_scan_offset_from_line(size_w line, size_w* offset, size_w max_offset) const
{
	const size_w scan_size = MEM_BLOCK_SIZE;
	size_w current_line = 0;
	size_w current_offset = 0;
	bool pending_cr = false;

	if (offset == 0 || sequence_length == 0)
		return false;

	if (line == 0)
	{
		*offset = 0;
		return true;
	}

	// This is exact but bounded by max_offset so lazy files do not get fully scanned.
	for (span* sptr = head->next; sptr != tail; sptr = sptr->next)
	{
		buffer_control* bc = buffer_list[sptr->buffer];
		size_w spanoffset = 0;

		while (spanoffset < sptr->length && current_offset < max_offset)
		{
			size_w chunk_length = min(scan_size, sptr->length - spanoffset);

			if (chunk_length > max_offset - current_offset)
				chunk_length = max_offset - current_offset;

			seqchar* source = bc->getptr(sptr->offset + spanoffset, chunk_length);

			if (source == 0)
				return false;

			for (size_w i = 0; i < chunk_length; i++)
			{
				seqchar ch = source[i];
				size_w next_offset = current_offset + 1;

				if (pending_cr)
				{
					current_line++;

					if (ch == '\n')
					{
						if (current_line == line)
						{
							*offset = next_offset;
							return true;
						}

						pending_cr = false;
						current_offset = next_offset;
						continue;
					}

					if (current_line == line)
					{
						*offset = current_offset;
						return true;
					}

					pending_cr = false;
				}

				if (ch == '\r')
				{
					pending_cr = true;
				}
				else if (ch == '\n')
				{
					current_line++;

					if (current_line == line)
					{
						*offset = next_offset;
						return true;
					}
				}

				current_offset = next_offset;
			}

			spanoffset += chunk_length;
		}

	}

	if (pending_cr)
	{
		current_line++;

		if (current_line == line)
		{
			*offset = current_offset;
			return true;
		}
	}

	return false;
}

//
//	sequence::exact_scan_line_from_offset
//
//	Find the line containing an offset by scanning from the start of the sequence.
//
bool sequence::exact_scan_line_from_offset(size_w offset, size_w* line, size_w* lineoffset) const
{
	const size_w scan_size = MEM_BLOCK_SIZE;
	size_w current_line = 0;
	size_w current_line_offset = 0;
	size_w current_offset = 0;
	bool pending_cr = false;

	if (sequence_length == 0 || offset > sequence_length)
		return false;

	// Used near the start of lazy files, where a bounded scan is cheap and exact.
	for (span* sptr = head->next; sptr != tail; sptr = sptr->next)
	{
		buffer_control* bc = buffer_list[sptr->buffer];
		size_w spanoffset = 0;

		while (spanoffset < sptr->length && current_offset <= offset)
		{
			size_w chunk_length = min(scan_size, sptr->length - spanoffset);
			seqchar* source = bc->getptr(sptr->offset + spanoffset, chunk_length);

			if (source == 0)
				return false;

			// Walk until the requested offset, remembering the start of the current line.
			for (size_w i = 0; i < chunk_length && current_offset <= offset; i++)
			{
				seqchar ch = source[i];
				size_w next_offset = current_offset + 1;

				if (pending_cr)
				{
					current_line++;

					if (ch == '\n')
					{
						current_line_offset = next_offset;
						pending_cr = false;
						current_offset = next_offset;
						continue;
					}

					current_line_offset = current_offset;
					pending_cr = false;
				}

				if (ch == '\r')
				{
					pending_cr = true;
				}
				else if (ch == '\n')
				{
					current_line++;
					current_line_offset = next_offset;
				}

				current_offset = next_offset;
			}

			spanoffset += chunk_length;
		}

		if (current_offset > offset)
			break;

	}

	if (pending_cr && current_offset <= offset)
	{
		current_line++;
		current_line_offset = current_offset;
	}

	if (line)
		*line = current_line;

	if (lineoffset)
		*lineoffset = current_line_offset;

	return true;
}

//
//	sequence::find_exact_line_start_near_offset
//
//	Snap an approximate offset back to the real start of its physical line.
//
bool sequence::find_exact_line_start_near_offset(size_w near_offset, size_w* offset) const
{
	const size_w scan_size = MEM_BLOCK_SIZE;
	size_w start;
	size_w length;
	seqchar* source;
	size_w line_start;
	bool ends_with_cr;

	if (offset == 0 || sequence_length == 0 || near_offset > sequence_length)
		return false;

	if (near_offset == 0)
	{
		*offset = 0;
		return true;
	}

	// Search backwards over one page and return the last line start before near_offset.
	start = near_offset > scan_size ? near_offset - scan_size : 0;
	length = near_offset - start;

	if (length == 0)
	{
		*offset = start;
		return true;
	}

	source = new seqchar[length];

	if (source == 0)
		return false;

	if (render(start, source, length) != length)
	{
		delete[] source;
		return false;
	}

	line_start = start;
	ends_with_cr = false;

	// Record the last line break seen in the local search window.
	for (size_w i = 0; i < length; i++)
	{
		ends_with_cr = false;

		if (source[i] == '\r')
		{
			if (i + 1 < length && source[i + 1] == '\n')
			{
				line_start = start + i + 2;
				i++;
			}
			else
			{
				line_start = start + i + 1;
				ends_with_cr = true;
			}
		}
		else if (source[i] == '\n')
		{
			line_start = start + i + 1;
		}
	}

	delete[] source;

	if (ends_with_cr && line_start == near_offset && near_offset < sequence_length)
	{
		seqchar nextch;

		if (render(near_offset, &nextch, 1) == 1 && nextch == '\n')
			line_start++;
	}

	*offset = line_start;
	return true;
}

//
//	sequence::estimate_offset_from_line
//
//	Estimate a line's location, then return the nearest real line start.
//
bool sequence::estimate_offset_from_line(size_w line, size_w* offset) const
{
	size_w numlines = linecount();
	size_w near_offset;

	if (offset == 0 || sequence_length == 0 || numlines == 0)
		return false;

	if (line == 0)
	{
		*offset = 0;
		return true;
	}

	if (line >= numlines)
		return false;

	// Choose an approximate byte position, then snap it back to a real line start.
	if (line + 1 >= numlines)
		near_offset = sequence_length;
	else
		near_offset = estimate_muldiv(line, sequence_length, numlines);

	return find_exact_line_start_near_offset(near_offset, offset);
}

//
//	sequence::estimate_line_from_offset
//
//	Estimate a line number for an offset while still returning an exact line start.
//
bool sequence::estimate_line_from_offset(size_w offset, size_w* line, size_w* lineoffset) const
{
	size_w numlines = linecount();
	size_w estimated_line;

	if (sequence_length == 0 || offset > sequence_length || numlines == 0)
		return false;

	if (offset == sequence_length)
		estimated_line = numlines - 1;
	else
		estimated_line = estimate_muldiv(offset, numlines, sequence_length);

	if (estimated_line >= numlines)
		estimated_line = numlines - 1;

	if (line)
		*line = estimated_line;

	if (lineoffset && !find_exact_line_start_near_offset(offset, lineoffset))
		return false;

	return true;
}

//
//	sequence::exact_indexed_line_from_offset
//
//	Use an already-indexed lazy page to answer offset-to-line queries exactly.
//
bool sequence::exact_indexed_line_from_offset(size_w offset, size_w* line, size_w* lineoffset) const
{
	span* sptr;
	size_w spanindex = 0;
	size_w lookup_offset;
	size_w span_offset;
	size_w buffer_offset;
	buffer_control* bc;
	buffer_control::line_page* page;
	size_w page_index;
	size_w page_line;
	size_w lo;
	size_w hi;
	size_w line_start;

	if (sequence_length == 0 || offset > sequence_length)
		return false;

	// EOF belongs to the final span for lookup, even though the requested offset is unchanged.
	lookup_offset = offset;

	if (lookup_offset == sequence_length && lookup_offset > 0)
		lookup_offset--;

	if ((sptr = spanfromindex(lookup_offset, &spanindex)) == 0 || sptr == tail)
		return false;

	span_offset = offset - spanindex;

	if (span_offset > sptr->length)
		return false;

	bc = buffer_list[sptr->buffer];

	if (bc->line_pages == 0 || bc->line_page_count == 0)
		return false;

	buffer_offset = sptr->offset + span_offset;
	page_index = buffer_offset / MEM_BLOCK_SIZE;

	if (page_index >= bc->line_page_count)
	{
		if (buffer_offset != bc->length || bc->line_page_count == 0)
			return false;

		page_index = bc->line_page_count - 1;
	}

	page = &bc->line_pages[page_index];

	if (!page->indexed)
		return false;

	// Find the first recorded line start after buffer_offset.
	lo = 0;
	hi = page->line_count;

	while (lo < hi)
	{
		size_w mid = lo + (hi - lo) / 2;

		if (page->line_offsets[mid] <= buffer_offset)
			lo = mid + 1;
		else
			hi = mid;
	}

	page_line = lo;
	line_start = 0;

	// Convert the page-local line start back into a sequence offset.
	if (page_line > 0)
	{
		size_w buffer_line_start = page->line_offsets[page_line - 1];

		if (buffer_line_start >= sptr->offset && buffer_line_start <= sptr->offset + sptr->length)
			line_start = spanindex + (buffer_line_start - sptr->offset);
		else if (!find_exact_line_start_near_offset(offset, &line_start))
			return false;
	}
	else if (!find_exact_line_start_near_offset(offset, &line_start))
	{
		return false;
	}

	if (line)
	{
		if (page->line_base_known)
		{
			// Prefix pages have exact absolute line numbers.
			*line = page->line_base + page_line;

			if (page_index > 0 && bc->line_pages[page_index - 1].ends_with_cr && page->starts_with_lf && page_line > 0)
				(*line)--;
		}
		else
		{
			size_w numlines = linecount();
			size_w lines_after = page->line_count - page_line;
			bool prev_ends_with_cr = page->ends_with_cr;

			if (numlines == 0)
				return false;

			// Tail pages have exact local line starts but only an estimated absolute base.
			for (size_w i = page_index + 1; i < bc->line_page_count; i++)
			{
				buffer_control::line_page* next_page = &bc->line_pages[i];

				if (!next_page->indexed)
					return false;

				lines_after += next_page->line_count;

				if (prev_ends_with_cr && next_page->starts_with_lf && lines_after > 0)
					lines_after--;

				prev_ends_with_cr = next_page->ends_with_cr;
			}

			if (lines_after >= numlines)
				return false;

			*line = numlines - lines_after - 1;
		}
	}

	if (lineoffset)
		*lineoffset = line_start;

	return true;
}

//
//	sequence::lineoffset
//
//	Return the character offset at the start of the requested line.
//
bool sequence::lineoffset(size_w line, size_w* offset) const
{
	size_w current_line = 0;
	size_w spanindex = 0;
	bool prev_ends_with_cr = false;

	if (offset == 0 || sequence_length == 0)
		return false;

	if (!linecount_known())
	{
		size_w numlines = linecount();
		size_w near_offset;

		if (numlines == 0)
			return false;

		// Unknown line counts use bounded exact scans near the start, otherwise estimates.
		if (line >= numlines)
			return exact_scan_offset_from_line(line, offset, min(sequence_length, MEM_BLOCK_SIZE * 2));

		if (line + 1 >= numlines)
			near_offset = sequence_length;
		else
			near_offset = estimate_muldiv(line, sequence_length, numlines);

		if (near_offset <= MEM_BLOCK_SIZE * 2)
			return exact_scan_offset_from_line(line, offset);

		return estimate_offset_from_line(line, offset);
	}

	if (line >= linecount())
		return false;

	if (line == 0)
	{
		*offset = 0;
		return true;
	}

	//
	// Find the span that contains the requested line.
	//
	for (span* sptr = head->next; sptr != tail; sptr = sptr->next)
	{
		size_w effective_line_count = sptr->line_count;

		if (prev_ends_with_cr && sptr->starts_with_lf && effective_line_count > 0)
			effective_line_count--;

		if (line > current_line + effective_line_count)
		{
			current_line += effective_line_count;
			spanindex += sptr->length;
			prev_ends_with_cr = sptr->ends_with_cr ? true : false;
			continue;
		}

		buffer_control* bc = buffer_list[sptr->buffer];
		size_w end = sptr->offset + sptr->length;
		size_w local_line = line - current_line;
		size_w line_index = sptr->line_index + local_line - 1;

		// Use the span's cached line metadata when the target line has a recorded start.
		if (line_index < bc->line_count && bc->line_offsets[line_index] <= end)
		{
			size_w line_offset = bc->line_offsets[line_index];

			if (line_offset == end && sptr->ends_with_cr && sptr->next != tail && sptr->next->starts_with_lf)
				*offset = spanindex + sptr->length + 1;
			else
				*offset = spanindex + (line_offset - sptr->offset);

			return true;
		}

		// Fall back to scanning within this span when no cached line start is available.
		for (size_w i = 0; i < sptr->length; i++)
		{
			seqchar* ch = bc->getptr(sptr->offset + i, 1);

			if (ch == 0)
				return false;

			if (*ch == '\r')
			{
				size_w nextoffset = spanindex + i + 1;
				current_line++;

				if (i + 1 < sptr->length)
				{
					seqchar* nextch = bc->getptr(sptr->offset + i + 1, 1);

					if (nextch == 0)
						return false;

					if (*nextch == '\n')
					{
						i++;
						nextoffset++;
					}
				}
				else if (sptr->next != tail && sptr->next->starts_with_lf)
				{
					nextoffset++;
				}

				if (current_line == line)
				{
					*offset = nextoffset;
					return true;
				}
			}
			else if (*ch == '\n')
			{
				if (i == 0 && prev_ends_with_cr)
					continue;

				current_line++;

				if (current_line == line)
				{
					*offset = spanindex + i + 1;
					return true;
				}
			}
		}

		spanindex += sptr->length;
		prev_ends_with_cr = sptr->ends_with_cr ? true : false;
	}

	return false;
}

//
//	sequence::linefromoffset
//
//	Return the line number and line start for the specified character offset.
//
bool sequence::linefromoffset(size_w offset, size_w* line, size_w* lineoffset) const
{
	size_w current_line = 0;
	size_w current_line_offset = 0;
	size_w spanindex = 0;
	bool prev_ends_with_cr = false;

	if (sequence_length == 0 || offset > sequence_length)
		return false;

	if (!linecount_known())
	{
		if (offset <= MEM_BLOCK_SIZE)
			return exact_scan_line_from_offset(offset, line, lineoffset);

		// Lazy indexed pages can answer exactly before the whole file's line count is known.
		if (exact_indexed_line_from_offset(offset, line, lineoffset))
			return true;

		return estimate_line_from_offset(offset, line, lineoffset);
	}

	// Walk spans, accumulating line counts until the span containing the offset.
	for (span* sptr = head->next; sptr != tail; sptr = sptr->next)
	{
		size_w span_end = spanindex + sptr->length;
		size_w effective_line_count = sptr->line_count;
		size_w skip_line_count = 0;

		if (prev_ends_with_cr && sptr->starts_with_lf && effective_line_count > 0)
		{
			effective_line_count--;
			skip_line_count = 1;
		}

		if (offset <= span_end)
		{
			buffer_control* bc = buffer_list[sptr->buffer];
			// Count the line starts in this span up to the requested offset.
			size_w line_end = bc->first_line_after(sptr->offset + (offset - spanindex));
			size_w line_count = 0;

			if (sptr->line_index < line_end)
				line_count = line_end - sptr->line_index;

			if (line_count > 0)
			{
				size_w last_line_index = sptr->line_index + line_count - 1;

				if (bc->line_offsets[last_line_index] == sptr->offset + sptr->length && sptr->ends_with_cr && sptr->next != tail && sptr->next->starts_with_lf)
					line_count--;
			}

			if (line_count > skip_line_count)
			{
				size_w last_line_index = sptr->line_index + line_count - 1;
				size_w buffer_line_offset = bc->line_offsets[last_line_index];

				current_line += line_count - skip_line_count;
				current_line_offset = spanindex + (buffer_line_offset - sptr->offset);
			}

			if (line)
				*line = current_line;

			if (lineoffset)
				*lineoffset = current_line_offset;

			return true;
		}

		// Accumulate this span's final known line start before moving to the next span.
		if (effective_line_count > 0)
		{
			size_w last_line_index = sptr->line_index + sptr->line_count - 1;
			size_w buffer_line_offset = buffer_list[sptr->buffer]->line_offsets[last_line_index];

			current_line += effective_line_count;
			current_line_offset = spanindex + (buffer_line_offset - sptr->offset);

			if (buffer_line_offset == sptr->offset + sptr->length && sptr->ends_with_cr && sptr->next != tail && sptr->next->starts_with_lf)
				current_line_offset++;
		}

		spanindex += sptr->length;
		prev_ends_with_cr = sptr->ends_with_cr ? true : false;
	}

	return false;
}
