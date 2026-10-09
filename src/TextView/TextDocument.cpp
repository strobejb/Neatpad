//
//	MODULE:		TextDocument.cpp
//
//	PURPOSE:	Basic implementation of a text data-sequence class
//
//	NOTES:		www.catch22.net
//

#include <limits.h>
#include <string.h>
#include "TextDocument.h"
#include "Unicode.h"

//
//	The byte-order marks that identify a file's encoding, as the bytes that
//	start the file
//
struct BOM_LOOKUP
{
	uint8_t		  bom[4];
	uint32_t	  len;
	TEXT_ENCODING encoding;
};

static const BOM_LOOKUP BOMLOOK[] =
{
	// define longest headers first
	{ { 0xFF, 0xFE, 0x00, 0x00 }, 4, NCP_UTF32   },
	{ { 0x00, 0x00, 0xFE, 0xFF }, 4, NCP_UTF32BE },
	{ { 0xEF, 0xBB, 0xBF },       3, NCP_UTF8    },
	{ { 0xFE, 0xFF },             2, NCP_UTF16BE },
	{ { 0xFF, 0xFE },             2, NCP_UTF16   },
	{ { 0 },                      0, NCP_ASCII   },
};

//
//	What TextDocument needs to know about each encoding
//
struct TEXT_ENCODING_INFO
{
	TEXT_ENCODING encoding;
	uint32_t	  code_unit_bits;
	bool		  fixed_width;

	// TextDocument character offsets are UTF-16 code units.
	bool		  byte_offset_equals_char_offset;
};

static const TEXT_ENCODING_INFO ENCODINGINFO[] =
{
	{ NCP_ASCII,   8,  true,  true  },
	{ NCP_UTF8,    8,  false, false },
	{ NCP_UTF16,  16, true,  false },
	{ NCP_UTF16BE,16, true,  false },
	{ NCP_UTF32,  32, true,  false },
	{ NCP_UTF32BE,32, true,  false },
};

static const TEXT_ENCODING_INFO * encoding_info(TEXT_ENCODING encoding)
{
	for(size_t i = 0; i < sizeof(ENCODINGINFO) / sizeof(ENCODINGINFO[0]); i++)
	{
		if(ENCODINGINFO[i].encoding == encoding)
			return &ENCODINGINFO[i];
	}

	return &ENCODINGINFO[0];
}

static sequence::line_scan_mode line_scan_mode_from_encoding(TEXT_ENCODING encoding)
{
	switch(encoding)
	{
	case NCP_UTF16:
		return sequence::line_scan_utf16le;

	case NCP_UTF16BE:
		return sequence::line_scan_utf16be;

	case NCP_UTF32:
		return sequence::line_scan_utf32le;

	case NCP_UTF32BE:
		return sequence::line_scan_utf32be;

	case NCP_ASCII:
	case NCP_UTF8:
	default:
		return sequence::line_scan_bytes;
	}
}

//
//	TextDocument constructor
//
TextDocument::TextDocument()
{
//	buffer			= 0;

	m_nFileFormat		= NCP_ASCII;
	m_nHeaderSize		= 0;
}

//
//	TextDocument destructor
//
TextDocument::~TextDocument()
{
	clear();
}

//
//	Initialize the TextDocument with the specified file
//
bool TextDocument::init(const seqfilechar *filename)
{
	clear();

	if(!m_seq.open(filename, false))
		return false;

	// try to detect if this is an ascii/unicode/utf8 file
	m_nFileFormat = detect_file_format(&m_nHeaderSize);
	m_seq.set_line_scan_mode(line_scan_mode_from_encoding(m_nFileFormat));
	return true;
}

//	Initialize the TextDocument with the specified file
//
/*bool TextDocument::save(textchar *filename)
{
	HANDLE hFile;
	
	hFile = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);

	if(hFile == INVALID_HANDLE_VALUE)
		return false;



	CloseHandle(hFile);
	return true;
}*/


//
//	Parse the file lo
//
//
//	From the unicode.org FAQ:
//
//	00 00 FE FF			UTF-32, big-endian 
//	FF FE 00 00			UTF-32, little-endian 
//	FE FF				UTF-16, big-endian 
//	FF FE				UTF-16, little-endian 
//	EF BB BF			UTF-8 
//
//	Match the first x bytes of the file against the
//  Byte-Order-Mark (BOM) lookup table
//
TEXT_ENCODING TextDocument::detect_file_format(int *m_nHeaderSize)
{
	uint8_t header[4] = { 0 };
	m_seq.render(0, header, 4);

	for(int i = 0; BOMLOOK[i].len; i++)
	{
		if(m_seq.size() >= BOMLOOK[i].len &&
		   memcmp(header, BOMLOOK[i].bom, BOMLOOK[i].len) == 0)
		{
			*m_nHeaderSize = BOMLOOK[i].len;
			return BOMLOOK[i].encoding;
		}
	}

	*m_nHeaderSize = 0;
	return NCP_ASCII;	// default to ASCII
}


//
//	Empty the data-TextDocument
//
bool TextDocument::clear()
{
	m_seq.clear();
	m_seq.init();

	m_nFileFormat = NCP_ASCII;
	m_nHeaderSize = 0;
	return true;
}

//
//	Return a UTF-32 character value
//
size_w TextDocument::decode_char(size_w offset, size_t lenbytes, uint32_t *pch32)
{
//	uint8_t	*rawdata   = (uint8_t *)(buffer + offset + m_nHeaderSize);
	uint8_t	rawdata[16];
	size_w	rendered;

	lenbytes = min_of(sizeof(rawdata), lenbytes);
	rendered = m_seq.render(offset + m_nHeaderSize, rawdata, lenbytes);
	lenbytes = min_of(lenbytes, (size_t)rendered);

	if(lenbytes == 0 || pch32 == 0)
		return 0;

	switch(m_nFileFormat)
	{
	case NCP_ASCII:
		{
			UTF16  ch16;
			size_t len16 = 1;

			ascii_to_utf16(rawdata, 1, &ch16, &len16);
			*pch32 = ch16;
			return 1;
		}

	case NCP_UTF16:
		return utf16_to_utf32_char(rawdata, lenbytes, pch32);
		
	case NCP_UTF16BE:
		return utf16be_to_utf32_char(rawdata, lenbytes, pch32);

	case NCP_UTF8:
		return utf8_to_utf32(rawdata, lenbytes, pch32);

	case NCP_UTF32:
		return utf32_to_utf32_char(rawdata, lenbytes, pch32);

	case NCP_UTF32BE:
		return utf32be_to_utf32_char(rawdata, lenbytes, pch32);

	default:
		return 0;
	}
}

//
//	Return the UTF-32 character value that ends at the specified offset,
//	without reading back past lower_bound
//
//	returns the number of bytes the character occupies
//
size_w TextDocument::decode_prev_char(size_w offset, size_w lower_bound, uint32_t *pch32)
{
	uint8_t	rawdata[4];
	size_t	lenbytes;

	if(offset <= lower_bound || pch32 == 0)
		return 0;

	// no character is longer than 4 bytes in any encoding
	lenbytes = (size_t)min_of(offset - lower_bound, (size_w)sizeof(rawdata));

	if(m_seq.render(offset - lenbytes + m_nHeaderSize, rawdata, lenbytes) != lenbytes)
		return 0;

	switch(m_nFileFormat)
	{
	case NCP_ASCII:
		return decode_char(offset - 1, 1, pch32);

	case NCP_UTF8:
		return utf8_to_utf32_prev(rawdata, lenbytes, pch32);

	case NCP_UTF16:
		return utf16_to_utf32_prev(rawdata, lenbytes, pch32);

	case NCP_UTF16BE:
		return utf16be_to_utf32_prev(rawdata, lenbytes, pch32);

	// fixed-width, so only a whole unit will do
	case NCP_UTF32:
		return lenbytes == 4 ? utf32_to_utf32_char(rawdata, lenbytes, pch32) : 0;

	case NCP_UTF32BE:
		return lenbytes == 4 ? utf32be_to_utf32_char(rawdata, lenbytes, pch32) : 0;

	default:
		return 0;
	}
}

//
//	Fetch a buffer of UTF-16 text from the specified byte offset - 
//  returns the number of characters stored in buf
//
//	Depending on how Neatpad was compiled (UNICODE vs ANSI) this function
//  will always return text in the "native" format - i.e. Unicode or Ansi -
//  so the necessary conversions will take place here.
//
//  TODO: make sure the CR/LF is always fetched in one go
//        make sure utf-16 surrogates kept together
//		  make sure that combining chars kept together
//		  make sure that bidirectional text kep together (will be *hard*) 
//
//	offset   - uint8_t offset within underlying data sequence
//	lenbytes - max number of bytes to process (i.e. to limit to a line)
//  buf		 - UTF16/ASCII output buffer
//	plen	 - [in] - length of buffer, [out] - number of code-units stored
//
//	returns  - number of bytes processed
//
size_w TextDocument::decode_text(size_w offset, size_w lenbytes, textchar *buf, uint32_t *buflen)
{
//	uint8_t	*rawdata = (uint8_t *)(buffer + offset + m_nHeaderSize);

	uint32_t chars_copied = 0;
	size_w bytes_processed = 0;

	if(offset >= m_seq.size())
	{
		*buflen = 0;
		return 0;
	}

	while(lenbytes > 0 && *buflen > 0)
	{
		uint8_t   rawdata[0x100];
		size_w rawlen = min_of(lenbytes, (size_w)sizeof(rawdata));
		size_w rendered;

		// get next block of data from the piece-table
		rendered = m_seq.render(offset + m_nHeaderSize, rawdata, rawlen);

		if(rendered == 0)
			break;

		// with more of the range to come, end the block on a whole character
		// rather than decode one cut short by the block
		if(rendered == rawlen && rawlen < lenbytes)
		{
			size_t whole = rawdata_complete_length(rawdata, (size_t)rendered);

			if(whole > 0)
				rendered = whole;
		}

		// convert to UTF-16 
		size_t tmplen = *buflen;
		rawlen = rawdata_to_utf16(rawdata, (size_t)rendered, buf, &tmplen);

		// Stop before consuming a raw character that will not fit in the UTF-16 output buffer.
		if(rawlen == 0 && tmplen == 0)
			break;

		lenbytes		-= rawlen;
		offset			+= rawlen;
		bytes_processed += rawlen;

		buf				+= tmplen;
		*buflen			-= (uint32_t)tmplen;
		chars_copied	+= (uint32_t)tmplen;
	}

	*buflen = chars_copied;
	return bytes_processed;
}

TEXT_ENCODING TextDocument::getformat()
{
	return m_nFileFormat;
}

//
//	Length of the document in bytes, excluding any BOM/header
//
size_w TextDocument::byte_length()
{
	return m_seq.size() - m_nHeaderSize;
}

size_w TextDocument::file_length()
{
	return m_seq.size();
}

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
	size_w unitSize = encoding_info(m_nFileFormat)->code_unit_bits / 8;

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

TextReader TextDocument::text_from_range(TextPos from, TextPos to)
{
	TextPos begin = min_of(from, to);

	return TextReader(begin.byte_offset, max_of(from, to) - begin, this);
}

TextReader::TextReader()
	: text_doc(0), off_bytes(0), len_bytes(0)
{
}

TextReader::TextReader(const TextReader &reader)
	: text_doc(reader.text_doc), off_bytes(reader.off_bytes), len_bytes(reader.len_bytes)
{
}

TextReader & TextReader::operator= (const TextReader &reader)
{
	text_doc  = reader.text_doc;
	off_bytes = reader.off_bytes;
	len_bytes = reader.len_bytes;
	return *this;
}

uint32_t TextReader::read(textchar *buf, uint32_t buflen)
{
	if(text_doc)
	{
		// get text from the TextDocument at the specified byte-offset
		size_w len = text_doc->decode_text(off_bytes, len_bytes, buf, &buflen);

		// adjust the reader's internal position
		off_bytes += len;
		len_bytes -= len;

		return buflen;
	}
	else
	{
		return 0;
	}
}

TextReader::operator bool()
{
	return text_doc ? true : false;
}

TextReader::TextReader(size_w off, size_w len, TextDocument *td)
	: text_doc(td), off_bytes(off), len_bytes(len)
{
}

//
//	Retrieve the whole of a coordinate's line
//
uint32_t TextDocument::getline(const TextCoord &line, textchar *buf, uint32_t buflen)
{
	size_w begin     = line.line_begin.byte_offset;
	size_w len_bytes = line.line_next >= line.line_begin ? line.line_next - line.line_begin : 0;

	if(begin + len_bytes > byte_length())
		len_bytes = byte_length() - begin;

	decode_text(begin, len_bytes, buf, &buflen);
	return buflen;
}

//
//	Convert the RAW buffer in underlying file-format to UTF-16
//
//	
//	utf16len	- [in/out]	on input holds size of utf16str buffer, 
//							on output holds number of utf16 characters stored
//
//	returns bytes processed from rawdata
//
size_t TextDocument::rawdata_to_utf16(uint8_t *rawdata, size_t rawlen, textchar *utf16str, size_t *utf16len)
{
	switch(m_nFileFormat)
	{
	// convert from ANSI->UNICODE
	case NCP_ASCII:
		return ascii_to_utf16(rawdata, rawlen, (UTF16 *)utf16str, utf16len);
		
	case NCP_UTF8:
		return utf8_to_utf16(rawdata, rawlen, (UTF16 *)utf16str, utf16len);

	// already unicode, do a straight memory copy
	case NCP_UTF16:
		rawlen /= sizeof(UTF16);
		return copy_utf16((UTF16 *)rawdata, rawlen, (UTF16 *)utf16str, utf16len) * sizeof(UTF16);

	// need to convert from big-endian to little-endian
	case NCP_UTF16BE:
		rawlen /= sizeof(UTF16);
		return swap_utf16((UTF16 *)rawdata, rawlen, (UTF16 *)utf16str, utf16len) * sizeof(UTF16);

	case NCP_UTF32:
		return utf32le_to_utf16(rawdata, rawlen, (UTF16 *)utf16str, utf16len);

	case NCP_UTF32BE:
		return utf32be_to_utf16(rawdata, rawlen, (UTF16 *)utf16str, utf16len);

	// error! we should *never* reach this point
	default:
		*utf16len = 0;
		return 0;
	}
}

//
//	The length of the RAW buffer up to the end of its last whole character, so
//	that text can be converted a block at a time without splitting a character
//	between two blocks
//
size_t TextDocument::rawdata_complete_length(uint8_t *rawdata, size_t rawlen)
{
	switch(m_nFileFormat)
	{
	case NCP_UTF8:
		return utf8_complete_length(rawdata, rawlen);

	// whole code units: the halves of a surrogate pair convert separately
	case NCP_UTF16:
	case NCP_UTF16BE:
		return rawlen & ~(size_t)1;

	case NCP_UTF32:
	case NCP_UTF32BE:
		return rawlen & ~(size_t)3;

	case NCP_ASCII:
	default:
		return rawlen;
	}
}

//
//	Converts specified UTF16 string to the underlying RAW format of the text-document
//	(i.e. UTF-16 -> UTF-8
//		  UTF-16 -> UTF-32 etc)
//
//	returns number of WCHARs processed from utf16str
//
size_t TextDocument::utf16_to_rawdata(const textchar *utf16str, size_t utf16len, uint8_t *rawdata, size_t *rawlen)
{
	switch(m_nFileFormat)
	{
	// convert from UTF16 -> ASCII
	case NCP_ASCII:
		return utf16_to_ascii((const UTF16 *)utf16str, utf16len, rawdata, rawlen);
		
	// convert from UTF16 -> UTF8
	case NCP_UTF8:
		return utf16_to_utf8((const UTF16 *)utf16str, utf16len, rawdata, rawlen);

	// already unicode, do a straight memory copy
	case NCP_UTF16:
		*rawlen /= sizeof(UTF16);
		utf16len = copy_utf16((const UTF16 *)utf16str, utf16len, (UTF16 *)rawdata, rawlen);
		*rawlen *= sizeof(UTF16);
		return utf16len;

	// need to convert from big-endian to little-endian
	case NCP_UTF16BE:
		*rawlen /= sizeof(UTF16);
		utf16len = swap_utf16((const UTF16 *)utf16str, utf16len, (UTF16 *)rawdata, rawlen);
		*rawlen *= sizeof(UTF16);
		return utf16len;

	case NCP_UTF32:
		return utf16_to_utf32le((const UTF16 *)utf16str, utf16len, rawdata, rawlen);

	case NCP_UTF32BE:
		return utf16_to_utf32be((const UTF16 *)utf16str, utf16len, rawdata, rawlen);

	// error! we should *never* reach this point
	default:
		*rawlen = 0;
		return 0;	
	}

}

//
//	Insert UTF-16 text at specified uint8_t offset
//
//	returns number of BYTEs stored
//
size_w TextDocument::insert_raw(size_w offset_bytes, const textchar *text, uint32_t length)
{
	uint8_t  buf[0x100];
	size_t buflen;
	size_t copied;
	size_w rawlen = 0;
	size_w offset = offset_bytes+ m_nHeaderSize;

	while(length)
	{
		buflen = 0x100;
		copied = utf16_to_rawdata(text, length, buf, &buflen);

		// do the piece-table insertion!
		if(!m_seq.insert(offset, buf, buflen))
			break;

		text   += copied;
		length -= (uint32_t)copied;
		rawlen += buflen;
		offset += buflen;
	}

	return rawlen;
}

size_w TextDocument::replace_raw(size_w offset_bytes, const textchar *text, uint32_t length, size_w erase_bytes)
{
	uint8_t  buf[0x100];
	size_t buflen;
	size_t copied;
	size_w rawlen = 0;
	size_w offset = offset_bytes + m_nHeaderSize;

	while(length)
	{
		buflen = 0x100;
		copied = utf16_to_rawdata(text, length, buf, &buflen);

		// do the piece-table replacement!
		if(!m_seq.replace(offset, buf, buflen, erase_bytes))
			break;

		text   += copied;
		length -= (uint32_t)copied;
		rawlen += buflen;
		offset += buflen;

		erase_bytes = 0;
	}

	return rawlen;
}

//
//	The number of bytes taken by up to *length_chars UTF-16 units of text from
//	offset_bytes. Only whole characters are counted: a surrogate pair the count
//	would split is left out. On return *length_chars holds the units counted.
//
size_w TextDocument::count_chars(size_w offset_bytes, size_w *length_chars)
{
	size_w rawLength = m_seq.size() - m_nHeaderSize;
	size_w wanted = *length_chars;
	size_w remaining = wanted;
	size_w offset_start = offset_bytes;
	uint32_t ch32;

	switch(m_nFileFormat)
	{
	case NCP_ASCII:
		return wanted;

	case NCP_UTF16:
	case NCP_UTF16BE:
		offset_bytes += wanted * sizeof(UTF16);

		// ending between the halves of a pair? leave the pair out
		if(wanted > 0 && decode_char(offset_bytes - sizeof(UTF16), 2 * sizeof(UTF16), &ch32) == 2 * sizeof(UTF16))
		{
			offset_bytes -= sizeof(UTF16);
			*length_chars = wanted - 1;
		}

		return offset_bytes - offset_start;

	default:
		break;
	}

	while(remaining && offset_bytes < rawLength)
	{
		textchar buf[0x100];
		uint32_t charlen = (uint32_t)min_of(remaining, (size_w)0x100);
		size_w   bytelen = decode_text(offset_bytes, rawLength - offset_bytes, buf, &charlen);

		// nothing fits: the next character is a surrogate pair and one unit is left
		if(bytelen == 0 || charlen == 0)
			break;

		remaining    -= charlen;
		offset_bytes += bytelen;
	}

	*length_chars = wanted - remaining;
	return offset_bytes - offset_start;
}

size_w TextDocument::count_code_units(size_w offset_bytes, size_w length_bytes)
{
	size_w chars = 0;
	size_w rawLength = m_seq.size() - m_nHeaderSize;

	while(length_bytes && offset_bytes < rawLength)
	{
		textchar buf[0x100];
		uint32_t charlen = 0x100;
		size_w bytelen = decode_text(offset_bytes, min_of(length_bytes, rawLength - offset_bytes), buf, &charlen);

		if(bytelen == 0)
			break;

		chars += charlen;
		offset_bytes += bytelen;
		length_bytes -= bytelen;
	}

	return chars;
}

//
//	Insert text at a position
//
size_w TextDocument::insert_text(TextPos at, const textchar *text, uint32_t length, TextChange *change)
{
	size_w old_size = m_seq.size();
	size_w rawlen   = insert_raw(at.byte_offset, text, length);

	if(change)
	{
		change->at = at;
		change->erased = 0;
		change->inserted = m_seq.size() - old_size;
	}

	return rawlen;
}

//
//	Replace the text between two positions
//
size_w TextDocument::replace_text(TextPos from, TextPos to, const textchar *text, uint32_t length, TextChange *change)
{
	TextPos begin      = min_of(from, to);
	size_w erase_bytes = max_of(from, to) - begin;
	size_w old_size    = m_seq.size();
	size_w rawlen      = replace_raw(begin.byte_offset, text, length, erase_bytes);

	// The sequence clamps a replace that runs past the end, so measure what it erased.
	if(change)
	{
		change->at = begin;
		change->erased = old_size + rawlen - m_seq.size();
		change->inserted = rawlen;
	}

	return rawlen;
}

//
//	Erase the text between two positions
//
size_w TextDocument::erase_text(TextPos from, TextPos to, TextChange *change)
{
	TextPos begin      = min_of(from, to);
	size_w erase_bytes = max_of(from, to) - begin;
	size_w erased      = 0;

	if(erase_bytes && m_seq.erase(begin.byte_offset + m_nHeaderSize, erase_bytes))
		erased = erase_bytes;

	if(change)
	{
		change->at = begin;
		change->erased = erased;
		change->inserted = 0;
	}

	return erased;
}

//
//	Report the bytes changed by the last undo/redo in document coordinates
//
void TextDocument::event_change(TextChange *change)
{
	size_w offset;
	size_w erased;
	size_w inserted;

	if(change == 0)
		return;

	m_seq.event_change(&offset, &erased, &inserted);

	change->at = TextPos(offset > (size_w)m_nHeaderSize ? offset - m_nHeaderSize : 0);
	change->erased = erased;
	change->inserted = inserted;
}

//
//	Undo/redo by coordinate: the change says which bytes came back
//
bool TextDocument::undo(TextChange *change)
{
	if(!m_seq.undo())
		return false;

	event_change(change);
	return true;
}

bool TextDocument::redo(TextChange *change)
{
	if(!m_seq.redo())
		return false;

	event_change(change);
	return true;
}

bool TextDocument::can_undo()
{
	return m_seq.canundo();
}

bool TextDocument::can_redo()
{
	return m_seq.canredo();
}

void TextDocument::undo_group_begin()
{
	m_seq.group();
}

void TextDocument::undo_group_end()
{
	m_seq.ungroup();
}

void TextDocument::undo_group_break()
{
	m_seq.breakopt();
}
