/*
	sequence_chars.cpp

	Character index for the data-sequence class.

	A character is a UTF-16 unit, what the text decodes to for display, so a
	character from U+10000 up is two. chars_in() counts them in a run of bytes
	in each line scan mode: one per byte for ASCII, one per two bytes for
	UTF-16, one or two per four bytes for UTF-32, and for UTF-8 one per lead
	byte, or two for a 4-byte lead (see utf8_count_utf16).

	Counting UTF-8 by lead bytes makes counts add up: trailing bytes count
	nothing, so wherever a page or span boundary cuts a character, the counts
	either side add up to the count of both. It is exact for valid UTF-8. Broken
	UTF-8 can count differently from the U+FFFD shown for it: a stray trailing
	byte counts nothing, and a cut-off 4-byte sequence counts two.

	The counts ride the line index (sequence_lines.cpp): wherever a page, a span
	or the exact prefix keeps a break count, it keeps a character count beside
	it, filled by the same scan. The lookups here walk those counts as the line
	lookups walk break counts, and only answer inside the exact prefix.
*/

#include "sequence.h"
#include "Unicode.h"

//
//	sequence::chars_in
//
//	The characters in a run of bytes in the line scan mode. A unit cut short by
//	the end of the run isn't counted.
//
size_w sequence::chars_in(const seqchar *raw, size_w length, line_scan_mode mode)
{
	size_w chars = 0;

	switch(mode)
	{
	case line_scan_utf8:
		return utf8_count_utf16(raw, (size_t)length);

	case line_scan_utf16le:
	case line_scan_utf16be:
		return length / 2;

	case line_scan_utf32le:
	case line_scan_utf32be:
		for(size_w i = 0; i + 4 <= length; i += 4)
		{
			UTF32 ch;

			if(mode == line_scan_utf32le)
				utf32_to_utf32_char(raw + i, 4, &ch);
			else
				utf32be_to_utf32_char(raw + i, 4, &ch);

			// U+10000 and up take two
			chars += ch >= 0x10000 && ch <= UNI_MAX_UTF16 ? 2 : 1;
		}

		return chars;

	case line_scan_bytes:
	default:
		return length;
	}
}

//
//	sequence::count_chars_to
//
//	Count characters exactly in [0, limit). Stops early at the start of the span
//	or page holding unit target, reporting the characters before it. Returns
//	false if part of the range cannot be counted.
//
bool sequence::count_chars_to(size_w limit, size_w target, size_w *stop_offset, size_w *stop_chars) const
{
	size_w offset = 0;
	size_w chars  = 0;

	for(span *sptr = head->next; sptr != tail && offset < limit; sptr = sptr->next)
	{
		buffer_control *bc = buffer_list[sptr->buffer];
		size_w take = sptr->length < limit - offset ? sptr->length : limit - offset;

		// Whole spans with a known count are skipped without reading anything.
		if(take == sptr->length && span_line_count(sptr) && chars + sptr->chars <= target)
		{
			chars  += sptr->chars;
			offset += sptr->length;
			continue;
		}

		for(size_w seg = sptr->offset, end = sptr->offset + take; seg < end; )
		{
			size_w page_end = (seg / LINE_PAGE_SIZE + 1) * LINE_PAGE_SIZE;
			size_w seg_end  = page_end < end ? page_end : end;
			buffer_control::line_range range;

			if(!bc->count_breaks(seg, seg_end, false, &range))
				return false;

			if(chars + range.chars > target)
			{
				*stop_offset = offset + (seg - sptr->offset);
				*stop_chars  = chars;
				return true;
			}

			chars += range.chars;
			seg = seg_end;
		}

		offset += take;
	}

	*stop_offset = offset;
	*stop_chars  = chars;
	return true;
}

//
//	sequence::scan_to_char
//
//	Scan forward from offset, which has 'chars' characters before it, to the
//	start of the character holding unit target. A target in the middle of a
//	character that takes two units gives that character's start.
//
bool sequence::scan_to_char(size_w offset, size_w chars, size_w target, size_w *result) const
{
	size_w unit_size = line_scan_unit_size();
	bool found = false;

	render(offset, [&](const seqchar *block, size_t length)
	{
		for(size_w i = 0; i + unit_size <= length; i += unit_size)
		{
			size_w add = chars_in(block + i, unit_size, line_mode);

			if(chars + add > target)
			{
				*result = offset + i;
				found = true;
				return false;
			}

			chars += add;
		}

		offset += length;
		return true;
	}, sequence_length - offset);

	// the end of the text, after its last character
	if(!found && chars == target)
	{
		*result = offset;
		found = true;
	}

	return found;
}

//
//	Public character interface
//

size_w sequence::indexed_chars() const
{
	update_line_prefix();
	return prefix_chars;
}

//
//	sequence::chars_from_offset
//
//	The characters before offset. An offset inside a character counts it, as
//	its lead byte comes before the offset.
//
bool sequence::chars_from_offset(size_w offset, size_w *chars) const
{
	size_w stop;

	if(chars == 0 || offset > indexed_length())
		return false;

	return count_chars_to(offset, MAX_SEQUENCE_LENGTH, &stop, chars);
}

//
//	sequence::offset_from_chars
//
//	Where the character holding unit chars starts: the end of the text when
//	chars counts them all.
//
bool sequence::offset_from_chars(size_w chars, size_w *offset) const
{
	size_w stop;
	size_w before;

	if(offset == 0)
		return false;

	update_line_prefix();

	// past the indexed part, or at its end with more of the text to come
	if(chars > prefix_chars || (chars == prefix_chars && !prefix_complete))
		return false;

	return count_chars_to(sequence_length, chars, &stop, &before) && scan_to_char(stop, before, chars, offset);
}
