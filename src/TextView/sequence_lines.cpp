/*
	sequence_lines.cpp

	Line engine for the data-sequence class.

	Navigation: line_start_at() and next_line_start() scan the sequence locally
	around an offset. They never need the line index, so they work anywhere in
	a file that has not been counted.

	Numbering: each buffer caches a break count for every LINE_PAGE_SIZE page it
	has scanned. Buffers never change, so a page count, and a span's count (which
	depends only on the span's own bytes), stays valid until the line scan mode
	changes. A line number is exact when every span before it has a known count.
	Memory-backed text can always be counted; file pages are only read when asked
	to by index_lines(), by the first block counted on open, or by lineoffset()
	scanning a little way past the exact prefix.

	Line breaks are CR, LF and CRLF in the code units of the line scan mode. A
	range's break count is a pure function of its own units: CR always counts and
	LF counts unless the unit before it in the same range is CR. Joining two
	ranges subtracts one when the first ends with CR and the second starts with LF.
*/

#include <windows.h>
#include "sequence.h"

static const unsigned long CR = '\r';
static const unsigned long LF = '\n';

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

static size_w line_scan_unit_size(sequence::line_scan_mode mode)
{
	switch(mode)
	{
	case sequence::line_scan_utf16le:
	case sequence::line_scan_utf16be:
		return 2;

	case sequence::line_scan_utf32le:
	case sequence::line_scan_utf32be:
		return 4;

	case sequence::line_scan_bytes:
	default:
		return 1;
	}
}

static unsigned long decode_unit(const seqchar *raw, sequence::line_scan_mode mode)
{
	switch(mode)
	{
	case sequence::line_scan_utf16le:
		return (unsigned long)raw[0] | ((unsigned long)raw[1] << 8);

	case sequence::line_scan_utf16be:
		return ((unsigned long)raw[0] << 8) | (unsigned long)raw[1];

	case sequence::line_scan_utf32le:
		return (unsigned long)raw[0] |
			((unsigned long)raw[1] << 8) |
			((unsigned long)raw[2] << 16) |
			((unsigned long)raw[3] << 24);

	case sequence::line_scan_utf32be:
		return ((unsigned long)raw[0] << 24) |
			((unsigned long)raw[1] << 16) |
			((unsigned long)raw[2] << 8) |
			(unsigned long)raw[3];

	case sequence::line_scan_bytes:
	default:
		return raw[0];
	}
}

//
//	break_counter
//
//	Counts line breaks over a run of code units, carrying CR state between chunks.
//
struct break_counter
{
	size_w	breaks;
	bool	any;
	bool	starts_with_lf;
	bool	prev_cr;

	break_counter() : breaks(0), any(false), starts_with_lf(false), prev_cr(false)
	{
	}

	void unit(unsigned long ch)
	{
		if(!any)
		{
			starts_with_lf = ch == LF;
			any = true;
		}

		if(ch == CR)
		{
			breaks++;
			prev_cr = true;
		}
		else
		{
			if(ch == LF && !prev_cr)
				breaks++;

			prev_cr = false;
		}
	}

	void units(const seqchar *raw, size_w length, sequence::line_scan_mode mode, size_w unit_size)
	{
		size_w i = 0;

		if(unit_size == 1)
		{
			if(!any && length > 0)
				unit(raw[i++]);

			// Fast path: anything above CR cannot be a line break.
			for( ; i < length; i++)
			{
				if(raw[i] > CR)
					prev_cr = false;
				else
					unit(raw[i]);
			}

			return;
		}

		for( ; i + unit_size <= length; i += unit_size)
			unit(decode_unit(raw + i, mode));
	}
};

//
//	sequence::span_cursor
//
//	Walks code units forwards or backwards across spans, reading the backing
//	buffers a chunk at a time instead of rendering each unit separately.
//	A chunk points into a buffer view, so nothing else may read the sequence
//	while a cursor is in use.
//
class sequence::span_cursor
{
public:
	span_cursor(const sequence *owner)
		: seq(owner), sptr(0), span_start(0), chunk(0), chunk_start(0), chunk_end(0), pos(0)
	{
		unit = owner->line_scan_unit_size();
		mode = owner->line_mode;
	}

	void seek(size_w offset)
	{
		pos = offset;
	}

	size_w position() const
	{
		return pos;
	}

	// Read the unit at the cursor and move past it.
	bool next(unsigned long *ch)
	{
		if(pos > seq->sequence_length || unit > seq->sequence_length - pos)
			return false;

		if(chunk == 0 || pos < chunk_start || pos >= chunk_end || unit > chunk_end - pos)
		{
			if(!locate(pos) || !load(pos, sptr->length - (pos - span_start) > LINE_PAGE_SIZE ? pos + LINE_PAGE_SIZE : span_start + sptr->length) || unit > chunk_end - pos)
				return read_slow(pos, ch) && (pos += unit, true);
		}

		*ch = decode_unit(chunk + (pos - chunk_start), mode);
		pos += unit;
		return true;
	}

	// Read the unit before the cursor and move back over it.
	bool prev(unsigned long *ch)
	{
		size_w at;

		if(pos < unit || pos > seq->sequence_length)
			return false;

		at = pos - unit;

		if(chunk == 0 || at < chunk_start || pos > chunk_end)
		{
			if(!locate(pos - 1) || !load(pos - span_start > LINE_PAGE_SIZE ? pos - LINE_PAGE_SIZE : span_start, pos) || at < chunk_start)
				return read_slow(at, ch) && (pos = at, true);
		}

		*ch = decode_unit(chunk + (at - chunk_start), mode);
		pos = at;
		return true;
	}

private:

	// Make sptr the span containing offset (offset < sequence length).
	bool locate(size_w offset)
	{
		size_w index;
		span *found;

		if(sptr)
		{
			if(offset >= span_start && offset < span_start + sptr->length)
				return true;

			if(offset >= span_start + sptr->length && sptr->next != seq->tail)
			{
				span_start += sptr->length;
				sptr = sptr->next;

				if(offset < span_start + sptr->length)
					return true;
			}
			else if(offset < span_start && sptr->prev != seq->head)
			{
				sptr = sptr->prev;
				span_start -= sptr->length;

				if(offset >= span_start)
					return true;
			}
		}

		if((found = seq->spanfromindex(offset, &index)) == 0 || found == seq->tail)
		{
			sptr = 0;
			return false;
		}

		sptr = found;
		span_start = index;
		return true;
	}

	// Map [from, to) of the current span.
	bool load(size_w from, size_w to)
	{
		buffer_control *bc = seq->buffer_list[sptr->buffer];
		seqchar *ptr = bc->getptr(sptr->offset + (from - span_start), to - from);

		if(ptr == 0)
		{
			chunk = 0;
			return false;
		}

		chunk = ptr;
		chunk_start = from;
		chunk_end = to;
		return true;
	}

	// A unit split across spans: render it, which may move the buffer views.
	bool read_slow(size_w offset, unsigned long *ch)
	{
		chunk = 0;
		return seq->unit_at(offset, ch);
	}

	const sequence *seq;
	span	*sptr;
	size_w	 span_start;
	const seqchar *chunk;
	size_w	 chunk_start;
	size_w	 chunk_end;
	size_w	 pos;
	size_w	 unit;
	line_scan_mode mode;
};

//
//	buffer_control line counting
//

size_w sequence::buffer_control::line_scan_unit_size() const
{
	return ::line_scan_unit_size(line_mode);
}

void sequence::buffer_control::set_line_scan_mode(line_scan_mode mode)
{
	line_mode = mode;
	reset_line_pages();
}

void sequence::buffer_control::reset_line_pages()
{
	for(size_w page = 0; page < line_page_count; page++)
	{
		line_pages[page].breaks = 0;
		line_pages[page].scanned_length = 0;
		line_pages[page].starts_with_lf = false;
		line_pages[page].ends_with_cr = false;
	}

	scanned_bytes = 0;
	scanned_breaks = 0;
}

bool sequence::buffer_control::read_unit(size_w offset, unsigned long *ch)
{
	size_w unit_size = line_scan_unit_size();
	seqchar *raw;

	if(offset > length || unit_size > length - offset || (raw = getptr(offset, unit_size)) == 0)
		return false;

	*ch = decode_unit(raw, line_mode);
	return true;
}

size_w sequence::buffer_control::page_length(size_w page) const
{
	size_w start = page * LINE_PAGE_SIZE;

	if(page >= line_page_count || start >= length)
		return 0;

	return length - start < LINE_PAGE_SIZE ? length - start : LINE_PAGE_SIZE;
}

bool sequence::buffer_control::page_current(size_w page) const
{
	size_w page_len = page_length(page);

	return page_len != 0 && line_pages[page].scanned_length == page_len;
}

//
//	Count the breaks in [offset, end) by reading the buffer.
//
bool sequence::buffer_control::scan_range(size_w offset, size_w end, line_range *range)
{
	break_counter counter;
	size_w unit_size = line_scan_unit_size();

	while(offset < end)
	{
		size_w chunk = end - offset < LINE_PAGE_SIZE ? end - offset : LINE_PAGE_SIZE;
		seqchar *raw = getptr(offset, chunk);

		if(raw == 0)
			return false;

		counter.units(raw, chunk, line_mode, unit_size);
		offset += chunk;
	}

	range->breaks = counter.breaks;
	range->starts_with_lf = counter.starts_with_lf;
	range->ends_with_cr = counter.prev_cr;
	return true;
}

bool sequence::buffer_control::scan_page(size_w page)
{
	size_w page_len = page_length(page);
	line_range range;
	line_page *lp;

	if(page_len == 0 || !scan_range(page * LINE_PAGE_SIZE, page * LINE_PAGE_SIZE + page_len, &range))
		return false;

	lp = &line_pages[page];

	// A modify buffer's last page grows, so replace its previous contribution.
	scanned_bytes -= lp->scanned_length;
	scanned_breaks -= lp->breaks;

	lp->breaks = range.breaks;
	lp->scanned_length = page_len;
	lp->starts_with_lf = range.starts_with_lf;
	lp->ends_with_cr = range.ends_with_cr;

	scanned_bytes += page_len;
	scanned_breaks += range.breaks;
	return true;
}

//
//	The end of the leading part of [offset, end) that can be counted without
//	reading an unscanned file page. Partial pages are always countable.
//
size_w sequence::buffer_control::countable_end(size_w offset, size_w end) const
{
	if(fp == 0)
		return end;

	while(offset < end)
	{
		size_w page = offset / LINE_PAGE_SIZE;
		size_w page_start = page * LINE_PAGE_SIZE;
		size_w page_end = page_start + page_length(page);
		size_w seg_end = page_end < end ? page_end : end;

		if(offset == page_start && seg_end == page_end && !page_current(page))
			return offset;

		offset = seg_end;
	}

	return end;
}

//
//	Count the breaks in [offset, end): whole pages come from the page cache and
//	partial pages are scanned. Unscanned whole file pages are only read when
//	scan_file_pages is set.
//
bool sequence::buffer_control::count_breaks(size_w offset, size_w end, bool scan_file_pages, line_range *range)
{
	bool first = true;
	bool prev_cr = false;

	range->breaks = 0;
	range->starts_with_lf = false;
	range->ends_with_cr = false;

	while(offset < end)
	{
		size_w page = offset / LINE_PAGE_SIZE;
		size_w page_start = page * LINE_PAGE_SIZE;
		size_w page_end = page_start + page_length(page);
		size_w seg_end = page_end < end ? page_end : end;
		line_range seg;

		if(offset == page_start && seg_end == page_end)
		{
			if(!page_current(page))
			{
				if(fp != 0 && !scan_file_pages)
					return false;

				if(!scan_page(page))
					return false;
			}

			seg.breaks = line_pages[page].breaks;
			seg.starts_with_lf = line_pages[page].starts_with_lf;
			seg.ends_with_cr = line_pages[page].ends_with_cr;
		}
		else if(!scan_range(offset, seg_end, &seg))
		{
			return false;
		}

		if(first)
			range->starts_with_lf = seg.starts_with_lf;

		range->breaks += seg.breaks;

		if(!first && prev_cr && seg.starts_with_lf)
			range->breaks--;

		prev_cr = seg.ends_with_cr;
		first = false;
		offset = seg_end;
	}

	range->ends_with_cr = prev_cr;
	return true;
}

//
//	sequence line engine
//

size_w sequence::line_scan_unit_size() const
{
	return ::line_scan_unit_size(line_mode);
}

void sequence::lines_changed() const
{
	if(++line_generation == 0)
		++line_generation;
}

bool sequence::unit_at(size_w offset, unsigned long *ch) const
{
	seqchar raw[4];
	size_w unit_size = line_scan_unit_size();

	if(offset > sequence_length || unit_size > sequence_length - offset)
		return false;

	if(render(offset, raw, unit_size) != unit_size)
		return false;

	*ch = decode_unit(raw, line_mode);
	return true;
}

//
//	True when offset sits between the CR and LF of a CRLF pair.
//
bool sequence::crlf_straddles(size_w offset) const
{
	size_w unit_size = line_scan_unit_size();
	unsigned long before;
	unsigned long after;

	return offset >= unit_size &&
		unit_at(offset - unit_size, &before) && before == CR &&
		unit_at(offset, &after) && after == LF;
}

//
//	sequence::line_start_at
//
//	Find the start of the physical line containing offset by scanning back.
//
bool sequence::line_start_at(size_w offset, size_w *start) const
{
	span_cursor cursor(this);
	size_w unit_size = line_scan_unit_size();
	unsigned long ch;
	unsigned long after = 0;
	bool have_after;

	if(start == 0 || offset > sequence_length)
		return false;

	offset -= offset % unit_size;
	cursor.seek(offset);
	have_after = cursor.next(&after);
	cursor.seek(offset);

	if(cursor.prev(&ch))
	{
		// Between CR and LF the offset belongs to the line ending with that CRLF;
		// any other break just before the offset means the line starts here.
		if((ch == CR || ch == LF) && !(ch == CR && have_after && after == LF))
		{
			*start = offset;
			return true;
		}

		while(cursor.prev(&ch))
		{
			if(ch == CR || ch == LF)
			{
				*start = cursor.position() + unit_size;
				return true;
			}
		}
	}

	*start = 0;
	return true;
}

//
//	sequence::next_line_start
//
//	Find the start of the line after the one containing offset by scanning forward.
//
bool sequence::next_line_start(size_w offset, size_w *next) const
{
	span_cursor cursor(this);
	unsigned long ch;

	if(next == 0 || offset > sequence_length)
		return false;

	cursor.seek(offset - offset % line_scan_unit_size());

	while(cursor.next(&ch))
	{
		if(ch == LF)
		{
			*next = cursor.position();
			return true;
		}

		if(ch == CR)
		{
			size_w after_cr = cursor.position();

			*next = cursor.next(&ch) && ch == LF ? cursor.position() : after_cr;
			return true;
		}
	}

	*next = sequence_length;
	return true;
}

//
//	sequence::span_line_count
//
//	Make sure the span's break count is known, without reading unscanned file pages.
//
bool sequence::span_line_count(span *sptr) const
{
	buffer_control *bc;
	buffer_control::line_range range;
	size_w end = sptr->offset + sptr->length;

	if(sptr->line_count_known)
		return true;

	bc = buffer_list[sptr->buffer];

	if(bc->countable_end(sptr->offset, end) != end || !bc->count_breaks(sptr->offset, end, false, &range))
		return false;

	sptr->line_count = range.breaks;
	sptr->line_count_known = 1;
	return true;
}

//
//	sequence::update_line_prefix
//
//	Recompute the exact prefix: every span with a known count from the start,
//	plus the countable leading part of the first span without one.
//
void sequence::update_line_prefix() const
{
	size_w offset = 0;
	size_w breaks = 0;
	bool prev_cr = false;

	if(prefix_generation == line_generation)
		return;

	prefix_complete = true;

	for(span *sptr = head->next; sptr != tail; sptr = sptr->next)
	{
		buffer_control *bc;
		buffer_control::line_range range;
		size_w known_end;

		if(span_line_count(sptr))
		{
			breaks += sptr->line_count;

			if(prev_cr && sptr->starts_with_lf)
				breaks--;

			prev_cr = sptr->ends_with_cr ? true : false;
			offset += sptr->length;
			continue;
		}

		bc = buffer_list[sptr->buffer];
		known_end = bc->countable_end(sptr->offset, sptr->offset + sptr->length);

		if(known_end > sptr->offset && bc->count_breaks(sptr->offset, known_end, false, &range))
		{
			breaks += range.breaks;

			if(prev_cr && range.starts_with_lf)
				breaks--;

			offset += known_end - sptr->offset;
		}

		prefix_complete = false;
		break;
	}

	prefix_end = offset;
	prefix_breaks = breaks;
	prefix_generation = line_generation;
}

//
//	sequence::count_breaks_to
//
//	Count breaks exactly in [0, limit). Stops early at the start of the span or
//	page in which the count would reach target, reporting the breaks before it.
//	Returns false if part of the range cannot be counted.
//
bool sequence::count_breaks_to(size_w limit, size_w target, size_w *stop_offset, size_w *stop_breaks) const
{
	size_w offset = 0;
	size_w breaks = 0;
	bool prev_cr = false;

	for(span *sptr = head->next; sptr != tail && offset < limit; sptr = sptr->next)
	{
		buffer_control *bc = buffer_list[sptr->buffer];
		size_w take = sptr->length < limit - offset ? sptr->length : limit - offset;
		size_w join;

		// Whole spans with a known count are skipped without reading anything.
		if(take == sptr->length && span_line_count(sptr))
		{
			join = prev_cr && sptr->starts_with_lf ? 1 : 0;

			if(breaks + sptr->line_count - join < target)
			{
				breaks += sptr->line_count - join;
				prev_cr = sptr->ends_with_cr ? true : false;
				offset += sptr->length;
				continue;
			}
		}

		for(size_w seg = sptr->offset, end = sptr->offset + take; seg < end; )
		{
			size_w page_end = (seg / LINE_PAGE_SIZE + 1) * LINE_PAGE_SIZE;
			size_w seg_end = page_end < end ? page_end : end;
			buffer_control::line_range range;

			if(!bc->count_breaks(seg, seg_end, false, &range))
				return false;

			join = prev_cr && range.starts_with_lf ? 1 : 0;

			if(breaks + range.breaks - join >= target)
			{
				*stop_offset = offset + (seg - sptr->offset);
				*stop_breaks = breaks;
				return true;
			}

			breaks += range.breaks - join;
			prev_cr = range.ends_with_cr;
			seg = seg_end;
		}

		offset += take;
	}

	*stop_offset = offset;
	*stop_breaks = breaks;
	return true;
}

//
//	sequence::scan_to_line
//
//	Scan forward from offset, which has 'breaks' breaks before it, to the start
//	of the requested line.
//
bool sequence::scan_to_line(size_w offset, size_w breaks, size_w line, size_w *lineoffset) const
{
	span_cursor cursor(this);
	unsigned long ch;
	bool prev_cr = false;

	cursor.seek(offset);

	if(cursor.prev(&ch))
	{
		prev_cr = ch == CR;
		cursor.seek(offset);
	}

	while(cursor.next(&ch))
	{
		if(ch == CR)
		{
			prev_cr = true;

			if(++breaks == line)
			{
				size_w after_cr = cursor.position();

				*lineoffset = cursor.next(&ch) && ch == LF ? cursor.position() : after_cr;
				return true;
			}
		}
		else
		{
			if(ch == LF && !prev_cr && ++breaks == line)
			{
				*lineoffset = cursor.position();
				return true;
			}

			prev_cr = false;
		}
	}

	return false;
}

//
//	sequence::exact_line_from_offset
//
//	The exact line number of offset, which must be within the exact prefix.
//
bool sequence::exact_line_from_offset(size_w offset, size_w *line) const
{
	size_w stop;
	size_w breaks;

	if(!count_breaks_to(offset, MAX_SEQUENCE_LENGTH, &stop, &breaks))
		return false;

	// A CRLF straddling the offset only starts its line after the LF.
	if(breaks > 0 && crlf_straddles(offset))
		breaks--;

	*line = breaks;
	return true;
}

//
//	sequence::line_density
//
//	Breaks per byte of scanned file text, used to estimate unscanned regions.
//
void sequence::line_density(size_w *breaks, size_w *bytes) const
{
	*breaks = 0;
	*bytes = 0;

	for(size_t i = 0; i < buffer_list.size(); i++)
	{
		buffer_control *bc = buffer_list[i];

		if(bc->fp == 0)
			continue;

		*breaks += bc->scanned_breaks;
		*bytes += bc->scanned_bytes;
	}

	if(*bytes == 0)
	{
		*breaks = prefix_breaks;
		*bytes = prefix_end;
	}
}

//
//	sequence::estimated_line_from_offset
//
//	Estimate the line number of an offset past the exact prefix: spans with a
//	known count contribute exactly, the rest by line density. Monotonic, and
//	at the end of the sequence it is the estimated last line.
//
size_w sequence::estimated_line_from_offset(size_w target) const
{
	size_w density_breaks;
	size_w density_bytes;
	size_w offset = 0;
	size_w breaks = prefix_breaks;

	line_density(&density_breaks, &density_bytes);

	for(span *sptr = head->next; sptr != tail && offset < target; sptr = sptr->next)
	{
		size_w span_start = offset;
		size_w span_end = offset + sptr->length;
		size_w from = span_start > prefix_end ? span_start : prefix_end;
		size_w to = span_end < target ? span_end : target;

		offset = span_end;

		if(from >= to)
			continue;

		if(span_line_count(sptr))
		{
			buffer_control::line_range range;

			if(from == span_start && to == span_end)
				breaks += sptr->line_count;
			else if(buffer_list[sptr->buffer]->count_breaks(sptr->offset + (from - span_start), sptr->offset + (to - span_start), false, &range))
				breaks += range.breaks;
		}
		else
		{
			breaks += estimate_muldiv(to - from, density_breaks, density_bytes);
		}
	}

	return breaks;
}

//
//	sequence::estimated_offset_from_line
//
//	Find a line past the exact prefix by the same model as
//	estimated_line_from_offset, returning a real line start.
//
bool sequence::estimated_offset_from_line(size_w line, size_w *result) const
{
	size_w density_breaks;
	size_w density_bytes;
	size_w offset = 0;
	size_w breaks = prefix_breaks;

	line_density(&density_breaks, &density_bytes);

	for(span *sptr = head->next; sptr != tail; sptr = sptr->next)
	{
		size_w span_start = offset;
		size_w span_end = offset + sptr->length;
		size_w from = span_start > prefix_end ? span_start : prefix_end;
		size_w count;

		offset = span_end;

		if(from >= span_end)
			continue;

		if(span_line_count(sptr))
		{
			buffer_control::line_range range;

			if(from == span_start)
				count = sptr->line_count;
			else if(buffer_list[sptr->buffer]->count_breaks(sptr->offset + (from - span_start), sptr->offset + sptr->length, false, &range))
				count = range.breaks;
			else
				count = 0;

			if(breaks + count >= line)
			{
				if(scan_to_line(from, breaks, line, result))
					return true;

				break;
			}
		}
		else
		{
			count = estimate_muldiv(span_end - from, density_breaks, density_bytes);

			if(breaks + count >= line)
			{
				size_w near_offset = from + estimate_muldiv(line - breaks, density_bytes, density_breaks);

				return line_start_at(near_offset < span_end ? near_offset : span_end, result);
			}
		}

		breaks += count;
	}

	return line_start_at(sequence_length, result);
}

//
//	sequence::index_range
//
//	Scan every buffer page under a sequence range so its breaks are cached.
//
void sequence::index_range(size_w offset, size_w length) const
{
	size_w span_start = 0;
	bool changed = false;

	if(length == 0 || offset >= sequence_length)
		return;

	if(length > sequence_length - offset)
		length = sequence_length - offset;

	for(span *sptr = head->next; sptr != tail && span_start < offset + length; sptr = sptr->next)
	{
		size_w span_end = span_start + sptr->length;

		if(span_end > offset && sptr->length > 0)
		{
			buffer_control *bc = buffer_list[sptr->buffer];
			size_w from = sptr->offset + ((offset > span_start ? offset : span_start) - span_start);
			size_w to = sptr->offset + ((offset + length < span_end ? offset + length : span_end) - span_start);

			for(size_w page = from / LINE_PAGE_SIZE; page <= (to - 1) / LINE_PAGE_SIZE; page++)
			{
				if(!bc->page_current(page) && bc->scan_page(page))
					changed = true;
			}
		}

		span_start = span_end;
	}

	if(changed)
		lines_changed();
}

//
//	sequence::extend_line_prefix
//
//	Scan forward page by page from the exact prefix until it covers the given
//	line and offset, reading at most LINE_SCAN_AHEAD bytes. Callers only ask
//	for this near the prefix, so jumps far into a huge file stay estimates.
//
void sequence::extend_line_prefix(size_w line, size_w offset) const
{
	size_w scanned = 0;

	update_line_prefix();

	while(!prefix_complete && (line > prefix_breaks || offset > prefix_end) && scanned < LINE_SCAN_AHEAD)
	{
		size_w before = prefix_end;

		index_range(prefix_end, 1);
		update_line_prefix();

		if(prefix_end <= before)
			break;

		scanned += prefix_end - before;
	}
}

//
//	Public line interface
//

size_w sequence::linecount() const
{
	if(sequence_length == 0)
		return 0;

	update_line_prefix();

	if(prefix_complete)
		return prefix_breaks + 1;

	return estimated_line_from_offset(sequence_length) + 1;
}

bool sequence::linecount_known() const
{
	if(sequence_length == 0)
		return true;

	update_line_prefix();
	return prefix_complete;
}

//
//	sequence::line_number_known
//
//	Return true if this line number is exact.
//
bool sequence::line_number_known(size_w line) const
{
	if(sequence_length == 0)
		return false;

	update_line_prefix();
	return line <= prefix_breaks;
}

//
//	sequence::line_numbers_known
//
//	Return true if the line numbers of every offset in this range are exact.
//
bool sequence::line_numbers_known(size_w offset, size_w length) const
{
	if(length == 0)
		return true;

	if(offset > sequence_length || length > sequence_length - offset)
		return false;

	update_line_prefix();
	return prefix_complete || offset + length <= prefix_end;
}

//
//	sequence::index_lines
//
//	Count the line breaks under a sequence range so their line numbers can become exact.
//
void sequence::index_lines(size_w offset, size_w length)
{
	if(length == 0 || offset > sequence_length || length > sequence_length - offset)
		return;

	index_range(offset, length);
}

//
//	sequence::next_lineoffset
//
//  Find the next physical line start after lineoff, scanning forward only as far as needed.
//
bool sequence::next_lineoffset(size_w lineoff, size_w *nextoff) const
{
	return next_line_start(lineoff, nextoff);
}

//
//	sequence::linebounds_from_offset
//
//	Return the exact physical line bounds around an offset without requiring
//	an exact global line number.
//
bool sequence::linebounds_from_offset(size_w offset, size_w *lineoff, size_w *nextoff)
{
	if(lineoff == 0 || nextoff == 0 || sequence_length == 0 || offset > sequence_length)
		return false;

	return line_start_at(offset, lineoff) && next_line_start(offset, nextoff);
}

//
//	sequence::lineoffset
//
//	Return the offset at the start of the requested line. Exact within the
//	exact prefix; past it the line is located by estimate.
//
bool sequence::lineoffset(size_w line, size_w *offset) const
{
	size_w stop;
	size_w breaks;
	size_w numlines;

	if(offset == 0 || sequence_length == 0)
		return false;

	if(line == 0)
	{
		*offset = 0;
		return true;
	}

	update_line_prefix();

	// A line estimated to be near the exact prefix (or in a small remainder) is
	// cheap to make exact. With no breaks seen yet there is nothing to estimate from.
	if(!prefix_complete && line > prefix_breaks)
	{
		size_w density_breaks;
		size_w density_bytes;
		size_w distance;

		line_density(&density_breaks, &density_bytes);
		distance = density_breaks ? estimate_muldiv(line - prefix_breaks, density_bytes, density_breaks) : MAX_SEQUENCE_LENGTH;

		if(distance <= LINE_SCAN_AHEAD || sequence_length - prefix_end <= LINE_SCAN_AHEAD)
			extend_line_prefix(line, 0);
	}

	if(line <= prefix_breaks)
		return count_breaks_to(sequence_length, line, &stop, &breaks) && scan_to_line(stop, breaks, line, offset);

	if(prefix_complete)
		return false;

	numlines = estimated_line_from_offset(sequence_length) + 1;

	if(line >= numlines)
		return false;

	if(line + 1 == numlines)
		return line_start_at(sequence_length, offset);

	return estimated_offset_from_line(line, offset);
}

//
//	sequence::linefromoffset
//
//	Return the line number and line start for an offset. The line start is
//	always exact; the number is exact when line_number_known() says so.
//
bool sequence::linefromoffset(size_w offset, size_w *line, size_w *lineoffset) const
{
	if(sequence_length == 0 || offset > sequence_length)
		return false;

	update_line_prefix();

	// Stepping just past the exact prefix (moving down a line) extends it a page;
	// anything further away, such as the end of a huge file, stays an estimate.
	if(line && !prefix_complete && offset > prefix_end && offset - prefix_end <= LINE_PAGE_SIZE)
		extend_line_prefix(0, offset);

	if(line)
	{
		if(prefix_complete || offset <= prefix_end)
		{
			if(!exact_line_from_offset(offset, line))
				return false;
		}
		else
		{
			*line = estimated_line_from_offset(offset);
		}
	}

	if(lineoffset && !line_start_at(offset, lineoffset))
		return false;

	return true;
}
