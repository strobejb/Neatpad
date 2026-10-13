//
//	MODULE:		TextDocumentFind.cpp
//
//	PURPOSE:	TextDocument text search
//
//	NOTES:		www.catch22.net
//

#include <limits.h>
#include <string.h>
#include "TextDocument.h"
#include "Unicode.h"

//
//	Case-insensitive search, one character at a time. Each character is
//	decoded to UTF-16 and added to a window holding the last 'length' units,
//	which is compared with the pattern every time it moves.
//
//	The window is a ring buffer stored twice over, so the 'length' units
//	starting at any slot are contiguous and nothing has to be shifted.
//	Each slot also records one edge of the character its unit came from:
//	the start when searching forwards, the end when searching backwards -
//	the edge a match needs once that unit is the oldest in the window.
//
bool TextDocument::find_text_nocase(const textchar *text, uint32_t length, size_w from, size_w to, bool backwards, size_w *match, size_w *match_length)
{
	size_w    docLength = byte_length();
	size_w    offset;
	size_w    bytes;

	uint32_t  ch32;
	textchar  units[2];
	size_t    unitCount;
	
	uint32_t  slot = 0;
	uint32_t  used = 0;
	bool   matched = false;

	textchar  *window = 0;
	size_w *edges  = 0;

	if(text == 0 || length == 0 || match == 0 || match_length == 0)
		return false;

	from = min_of(from, docLength);
	to   = min_of(to, docLength);

	if(from > to) { size_w tmp = from; from = to; to = tmp; }
	if(from == to || length > INT_MAX)
		return false;

	window = new textchar[length * 2];
	edges  = new size_w[length];

	if(window == 0 || edges == 0)
	{
		delete[] edges;
		delete[] window;
		return false;
	}

	if(backwards)
	{
		offset = to;

		while(offset > from && !matched)
		{
			bytes = decode_prev_char(offset, from, &ch32);
			if(bytes == 0)
				break;

			unitCount = 2;
			utf32_to_utf16(&ch32, 1, (UTF16 *)units, &unitCount);

			// add to the front of the window, last unit first
			for(size_t i = unitCount; i > 0 && !matched; i--)
			{
				uint32_t lastSlot;

				if(slot == 0)
					slot = length - 1;
				else
					slot--;

				window[slot] = window[slot + length] = units[i - 1];
				edges[slot]  = offset;
				used         = min_of(used + 1, length);

				if(used == length && utf16_equal_nocase((const UTF16 *)(window + slot), (const UTF16 *)text, length))
				{
					lastSlot = slot == 0 ? length - 1 : slot - 1;

					*match = offset - bytes;
					*match_length = edges[lastSlot] - *match;
					matched = true;
				}
			}

			offset -= bytes;
		}
	}
	else
	{
		offset = from;

		while(offset < to && !matched)
		{
			bytes = decode_char(offset, (size_t)min_of(to - offset, (size_w)16), &ch32);
			if(bytes == 0)
			{
				offset++;
				continue;
			}

			unitCount = 2;
			utf32_to_utf16(&ch32, 1, (UTF16 *)units, &unitCount);

			// add to the back of the window, leaving slot at the oldest unit
			for(size_t i = 0; i < unitCount && !matched; i++)
			{
				window[slot] = window[slot + length] = units[i];
				edges[slot]  = offset;

				slot++;
				if(slot == length)
					slot = 0;

				used = min_of(used + 1, length);

				if(used == length && utf16_equal_nocase((const UTF16 *)(window + slot), (const UTF16 *)text, length))
				{
					*match        = edges[slot];
					*match_length = offset + bytes - *match;
					matched       = true;
				}
			}

			offset += bytes;
		}
	}

	delete[] edges;
	delete[] window;
	return matched;
}

TextPos TextDocument::filter_lines(const textchar *text, uint32_t length, bool matchcase, TextPos from, size_w bytes,
								   const std::function<void(TextPos line_begin, TextPos line_next)> &found)
{
	size_w    length_left = byte_length() - min_of(from.byte_offset, byte_length());
	TextCoord last;
	TextCoord line;
	TextPos   begin;
	TextPos   end;

	// whole lines: to the end of the one 'bytes' on reaches
	if(!coord_from_pos(TextPos(from.byte_offset + min_of(bytes, length_left)), &last))
		return from;

	while(from < last.line_next && find_text(text, length, from, last.line_next, false, matchcase, &begin, &end))
	{
		coord_from_pos(begin, &line);
		found(line.line_begin, line.line_next);
		from = line.line_next;
	}

	return last.line_next;
}

bool TextDocument::find_text(const textchar *text, uint32_t length, TextPos from_pos, TextPos to_pos, bool backwards, bool matchcase, TextPos *match_begin, TextPos *match_end)
{
	const size_w chunkSize = 0x10000;

	size_w from = from_pos.byte_offset;
	size_w to   = to_pos.byte_offset;
	size_w docLength = byte_length();
	size_w patternLength;
	size_w chunkStart;
	size_w startLimit;
	size_w found = 0;
	size_w foundLength = 0;
	size_t rawCapacity;
	size_t rawLength;
	size_t copied;
	size_w blockLength;

	// a match must start on a code unit, not part way through one
	size_w unitSize = code_unit_size();

	uint8_t * pattern = 0;
	uint8_t * block = 0;

	bool   matched = false;

	if(text == 0 || length == 0 || match_begin == 0 || match_end == 0)
		return false;

	if(!matchcase)
	{
		if(!find_text_nocase(text, length, from, to, backwards, &found, &foundLength))
			return false;

		*match_begin = TextPos(found);
		*match_end   = TextPos(found + foundLength);
		return true;
	}

	rawCapacity = (size_t)length * 4;
	if(rawCapacity < length || rawCapacity == 0)
		return false;

	pattern = new uint8_t[rawCapacity];
	if(pattern == 0)
		return false;

	rawLength = rawCapacity;
	copied = utf16_to_rawdata(text, length, pattern, &rawLength);

	if(copied != length || rawLength == 0 || rawLength > (size_t)MAX_SEQUENCE_LENGTH)
	{
		delete[] pattern;
		return false;
	}

	patternLength = (size_w)rawLength;
	from = min_of(from, docLength);
	to   = min_of(to, docLength);

	if(from > to)      { size_w tmp = from; from = to; to = tmp; }
	if(patternLength > to - from)
	{
		delete[] pattern;
		return false;
	}

	blockLength = min_of(chunkSize + patternLength - 1, to - from);
	block = new uint8_t[(size_t)blockLength];
	if(block == 0)
	{
		delete[] pattern;
		return false;
	}

	startLimit = to - patternLength + 1;
	chunkStart = from;

	if(backwards)
	{
		size_w chunkEnd = startLimit;

		while(chunkEnd > from && !matched)
		{
			size_w candidates = min_of(chunkSize, chunkEnd - from);
			chunkStart = chunkEnd - candidates;

			size_w rendered = m_seq.render(chunkStart + m_nHeaderSize, block, candidates + patternLength - 1);
			if(rendered < patternLength)
				break;

			while(candidates > 0)
			{
				candidates--;

				if(candidates + patternLength <= rendered &&
					(chunkStart + candidates) % unitSize == 0 &&
					memcmp(block + candidates, pattern, (size_t)patternLength) == 0)
				{
					found = chunkStart + candidates;
					matched = true;
					break;
				}
			}

			chunkEnd = chunkStart;
		}
	}
	else
	{
		while(chunkStart < startLimit && !matched)
		{
			size_w candidates = min_of(chunkSize, startLimit - chunkStart);
			size_w rendered = m_seq.render(chunkStart + m_nHeaderSize, block, candidates + patternLength - 1);

			if(rendered < patternLength)
				break;

			for(size_w pos = 0; pos < candidates && pos + patternLength <= rendered; pos++)
			{
				if((chunkStart + pos) % unitSize == 0 &&
					memcmp(block + pos, pattern, (size_t)patternLength) == 0)
				{
					found = chunkStart + pos;
					matched = true;
					break;
				}
			}

			chunkStart += candidates;
		}
	}

	if(matched)
	{
		*match_begin = TextPos(found);
		*match_end   = TextPos(found + patternLength);
	}

	delete[] block;
	delete[] pattern;
	return matched;
}
