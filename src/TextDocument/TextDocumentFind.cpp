// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

//
//	MODULE:		TextDocumentFind.cpp
//
//	PURPOSE:	TextDocument text search
//
//	NOTES:		www.catch22.net
//

#include <limits.h>
#include <string.h>
#include <memory>
#include <vector>
#include "TextDocument.h"
#include "Unicode.h"

//
//	Case-insensitive search, one character at a time. Each character is
//	decoded to UTF-16 and added to a window holding the last 'length' units,
//	which is compared with the pattern every time it moves. UTF-8 text is
//	searched for as UTF-16 too.
//
//	The window is a ring buffer stored twice over, so the 'length' units
//	starting at any slot are contiguous and nothing has to be shifted.
//	Each slot also records one edge of the character its unit came from:
//	the start when searching forwards, the end when searching backwards -
//	the edge a match needs once that unit is the oldest in the window.
//
bool TextDocument::find_text_nocase(const tchar_t *text, uint32_t length, size_w from, size_w to, bool backwards, size_w *match, size_w *match_length)
{
	size_w    docLength = byte_length();
	size_w    offset;
	size_w    bytes;

	uint32_t  ch32;
	UTF16     units[2];
	size_t    unitCount;
	
	uint32_t  slot = 0;
	uint32_t  used = 0;
	bool   matched = false;

	UTF16    *window = 0;
	size_w *edges  = 0;

	const UTF16       *pattern = (const UTF16 *)text;
	std::vector<UTF16> pattern16;

	if(text == 0 || length == 0 || match == 0 || match_length == 0)
		return false;

	// UTF-8 is never fewer bytes than its UTF-16 has units
	if(TCHAR_UTF8)
	{
		size_t len16 = length;

		pattern16.resize(length);
		utf8_to_utf16((const UTF8 *)text, length, pattern16.data(), &len16);

		pattern = pattern16.data();
		length  = (uint32_t)len16;
	}

	from = min_of(from, docLength);
	to   = min_of(to, docLength);

	if(from > to) { size_w tmp = from; from = to; to = tmp; }
	if(from == to || length > INT_MAX)
		return false;

	window = new UTF16[length * 2];
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
			utf32_to_utf16(&ch32, 1, units, &unitCount);

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

				if(used == length && utf16_equal_nocase(window + slot, pattern, length))
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
			utf32_to_utf16(&ch32, 1, units, &unitCount);

			// add to the back of the window, leaving slot at the oldest unit
			for(size_t i = 0; i < unitCount && !matched; i++)
			{
				window[slot] = window[slot + length] = units[i];
				edges[slot]  = offset;

				slot++;
				if(slot == length)
					slot = 0;

				used = min_of(used + 1, length);

				if(used == length && utf16_equal_nocase(window + slot, pattern, length))
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

//
//	Case-insensitive search forwards for ASCII text in UTF-8 or Latin-1, as
//	logs mostly are, where an ASCII byte is always a whole character. Where
//	the text is ASCII it can only match by ASCII case, so it's compared a byte
//	at a time, about as fast as a search matching case. Where it reaches any
//	other character, find_text_nocase folds that character's case, so the
//	matches are the same.
//
bool TextDocument::find_ascii_nocase(const tchar_t *text, uint32_t length, size_w from, size_w to, size_w *match,
									 size_w *match_length)
{
	const size_w chunkSize = 0x10000;

	size_w				 docLength = byte_length();
	size_w				 startLimit;
	size_w				 chunkStart;
	size_w				 chunk = 0x1000;
	std::vector<uint8_t> pattern(length);
	std::unique_ptr<uint8_t[]> block;

	auto lower = [](uint32_t ch)
	{
		return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
	};

	for(uint32_t i = 0; i < length; i++)
		pattern[i] = (uint8_t)lower((uint32_t)text[i]);

	from = min_of(from, docLength);
	to	 = min_of(to, docLength);

	if(from > to || length > to - from)
		return false;

	startLimit = to - length + 1;
	chunkStart = from;
	block.reset(new uint8_t[(size_t)min_of(chunkSize + length - 1, to - from)]);

	// small blocks first, as the next match is often near (the filter looks
	// again after each one), then larger ones
	while(chunkStart < startLimit)
	{
		size_w candidates = min_of(chunk, startLimit - chunkStart);
		size_w rendered	  = m_seq.render(chunkStart + m_nHeaderSize, block.get(), candidates + length - 1);

		if(rendered < length)
			break;

		for(size_w pos = 0; pos < candidates && pos + length <= rendered; pos++)
		{
			const uint8_t *at = block.get() + (size_t)pos;
			size_w		   offset;
			uint32_t	   i = 0;

			// not a character's start in UTF-8, and no ASCII letter's case in Latin-1
			if(at[0] >= 0x80 && at[0] < 0xC0)
				continue;

			while(i < length && at[i] < 0x80 && lower(at[i]) == pattern[i])
				i++;

			if(i == length)
			{
				*match		  = chunkStart + pos;
				*match_length = length;
				return true;
			}

			// another character, which may fold to an ASCII one: compared as
			// find_text_nocase compares it (a match can be longer in bytes)
			if(i < length && at[i] >= 0x80)
			{
				offset = chunkStart + pos;

				if(find_text_nocase(text, length, offset, min_of(to, offset + (size_w)length * 4), false, match,
									match_length) &&
				   *match == offset)
					return true;
			}
		}

		chunkStart += candidates;
		chunk = min_of(chunk * 2, chunkSize);
	}

	return false;
}

TextPos TextDocument::filter_lines(const tchar_t *text, uint32_t length, bool matchcase, TextPos from, size_w bytes,
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

bool TextDocument::find_text(const tchar_t *text, uint32_t length, TextPos from_pos, TextPos to_pos, bool backwards, bool matchcase, TextPos *match_begin, TextPos *match_end)
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
		// UTF-8, or Latin-1 (Windows' ANSI code page can have two-byte characters)
		bool ascii = !backwards && (m_nFileFormat == NCP_UTF8 || (m_nFileFormat == NCP_ASCII && TCHAR_UTF8));

		for(uint32_t i = 0; i < length && ascii; i++)
			ascii = (uint32_t)text[i] < 0x80;

		if(ascii ? !find_ascii_nocase(text, length, from, to, &found, &foundLength)
				 : !find_text_nocase(text, length, from, to, backwards, &found, &foundLength))
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
	copied = text_to_rawdata(text, length, pattern, &rawLength);

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
		size_w chunk = 0x1000;

		// small blocks first, as the next match is often near
		while(chunkStart < startLimit && !matched)
		{
			size_w candidates = min_of(chunk, startLimit - chunkStart);
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
			chunk = min_of(chunk * 2, chunkSize);
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
