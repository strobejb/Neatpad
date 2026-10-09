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
#include "SaveFile.h"
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

	m_nFileFormat		= NCP_UTF8;
	m_nHeaderSize		= 0;
	m_fBom				= false;
}

//
//	TextDocument destructor
//
TextDocument::~TextDocument()
{
	clear();
}

//
//	The byte-order mark of an encoding, or the empty entry that ends BOMLOOK
//	for an encoding without one
//
static const BOM_LOOKUP * bom_for(TEXT_ENCODING encoding)
{
	const BOM_LOOKUP *bom = BOMLOOK;

	while(bom->len && bom->encoding != encoding)
		bom++;

	return bom;
}

static bool known_encoding(TEXT_ENCODING encoding)
{
	return encoding_info(encoding)->encoding == encoding;
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
	m_fBom = m_nHeaderSize > 0;
	m_seq.set_line_scan_mode(line_scan_mode_from_encoding(m_nFileFormat));
	return true;
}

//
//	Initialize the TextDocument with a file in the given encoding, whatever its
//	byte-order mark says. The encoding's own byte-order mark is skipped.
//
bool TextDocument::init(const seqfilechar *filename, TEXT_ENCODING encoding)
{
	const BOM_LOOKUP *bom = bom_for(encoding);
	uint8_t header[4] = { 0 };

	if(!known_encoding(encoding) || !init(filename))
		return false;

	if(m_nFileFormat != encoding)
	{
		m_seq.render(0, header, 4);

		m_nFileFormat = encoding;
		m_nHeaderSize = m_seq.size() >= bom->len && memcmp(header, bom->bom, bom->len) == 0 ? bom->len : 0;
		m_fBom = m_nHeaderSize > 0;
		m_seq.set_line_scan_mode(line_scan_mode_from_encoding(m_nFileFormat));
	}

	return true;
}

//
//	Save the document to a file. Kept in the encoding it was read in, with its
//	line breaks, its bytes are written as they are - after the encoding's own
//	byte-order mark, if wanted, rather than the one they were read with - and
//	the undo history carries on. Otherwise the text is converted as it's written.
//
bool TextDocument::save(const seqfilechar *filename, TEXT_ENCODING encoding, bool bom, uint32_t linebreak)
{
	const BOM_LOOKUP *header = bom_for(bom ? encoding : NCP_ASCII);	// ASCII has none
	bool  same_encoding = encoding == m_nFileFormat && linebreak == 0;
	SaveFile file;

	if(!known_encoding(encoding) || !file.open(filename) || !file.write(header->bom, header->len))
		return false;

	// does the destination encoding match this open file's encoding?
	if(same_encoding)
	{
		size_w length = m_seq.size() - m_nHeaderSize;

		if(m_seq.render(m_nHeaderSize, [&file](const seqchar *block, size_t blocklen) { return file.write(block, blocklen); }, length) != length)
			return false;
	}
	// otherwise convert
	else if(!write_converted(file, encoding, linebreak))
	{
		return false;
	}

	if(!file.commit())
		return false;

	if(same_encoding)
	{
		m_seq.mark_saved();
		m_fBom = header->len > 0;
	}

	return true;
}

bool TextDocument::has_bom()
{
	return m_fBom;
}


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
//  Byte-Order-Mark (BOM) lookup table. Without one, the file is UTF-8 if its
//	start is valid UTF-8 - which plain ASCII is - and ANSI otherwise.
//
TEXT_ENCODING TextDocument::detect_file_format(int *headersize)
{
	uint8_t header[4] = { 0 };
	m_seq.render(0, header, 4);

	for(int i = 0; BOMLOOK[i].len; i++)
	{
		if(m_seq.size() >= BOMLOOK[i].len &&
		   memcmp(header, BOMLOOK[i].bom, BOMLOOK[i].len) == 0)
		{
			*headersize = BOMLOOK[i].len;
			return BOMLOOK[i].encoding;
		}
	}

	*headersize = 0;

	// The start is the first block, which opening the file has already read
	// to count its lines. A character the block cuts short isn't an error:
	// the rest of it follows.
	size_w sample = min_of(m_seq.size(), MEM_BLOCK_SIZE);
	std::vector<uint8_t> start((size_t)sample);

	m_seq.render(0, start.data(), sample);

	size_t whole = sample < m_seq.size() ? utf8_complete_length(start.data(), (size_t)sample) : (size_t)sample;

	return utf8_is_valid(start.data(), whole) ? NCP_UTF8 : NCP_ASCII;
}


//
//	Empty the data-TextDocument
//
bool TextDocument::clear()
{
	m_seq.clear();
	m_seq.init();

	// a new document is UTF-8, without a byte-order mark
	m_nFileFormat = NCP_UTF8;
	m_nHeaderSize = 0;
	m_fBom		  = false;
	m_seq.set_line_scan_mode(line_scan_mode_from_encoding(m_nFileFormat));
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

	// A UTF-16 file is copied a unit at a time, so a full buffer can end between
	// the halves of a pair: leave the first half for next time. (The other
	// encodings only ever convert whole characters.)
	if((m_nFileFormat == NCP_UTF16 || m_nFileFormat == NCP_UTF16BE) && chars_copied > 0 && lenbytes >= sizeof(UTF16))
	{
		uint32_t ch32;

		if(decode_char(offset - sizeof(UTF16), 2 * sizeof(UTF16), &ch32) == 2 * sizeof(UTF16))
		{
			chars_copied	-= 1;
			bytes_processed -= sizeof(UTF16);
		}
	}

	*buflen = chars_copied;
	return bytes_processed;
}

TEXT_ENCODING TextDocument::getformat()
{
	return m_nFileFormat;
}

//
//	The bytes in one code unit of the file's encoding
//
size_w TextDocument::code_unit_size()
{
	return encoding_info(m_nFileFormat)->code_unit_bits / 8;
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

TextReader TextDocument::text_from_range(TextPos from, TextPos to)
{
	return TextReader(this, min_of(from, to), max_of(from, to));
}

TextReader::TextReader()
	: m_doc(0)
{
}

TextReader::TextReader(TextDocument *doc, TextPos from, TextPos to)
	: m_doc(doc), m_pos(from), m_end(to)
{
}

uint32_t TextReader::read(textchar *buf, uint32_t buflen)
{
	size_w bytes;

	if(at_end())
		return 0;

	bytes = m_doc->decode_text(m_pos.byte_offset, m_end - m_pos, buf, &buflen);
	m_pos = TextPos(m_pos.byte_offset + bytes);

	return buflen;
}

TextPos TextReader::pos() const
{
	return m_pos;
}

bool TextReader::at_end() const
{
	return m_doc == 0 || m_pos >= m_end;
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
//	Converts specified UTF16 string to an encoding's RAW format
//	(i.e. UTF-16 -> UTF-8
//		  UTF-16 -> UTF-32 etc)
//
//	returns number of WCHARs processed from utf16str
//
static size_t encode_utf16(TEXT_ENCODING encoding, const textchar *utf16str, size_t utf16len, uint8_t *rawdata, size_t *rawlen)
{
	switch(encoding)
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
//	Converts specified UTF16 string to the underlying RAW format of the text-document
//
size_t TextDocument::utf16_to_rawdata(const textchar *utf16str, size_t utf16len, uint8_t *rawdata, size_t *rawlen)
{
	return encode_utf16(m_nFileFormat, utf16str, utf16len, rawdata, rawlen);
}

//
//	Make every line break in text the same: linebreak, which is TXL_CR, TXL_LF
//	or TXL_CRLF. dest holds twice length. A CR that ends one piece of text and
//	an LF that starts the next are one CRLF; *after_cr carries that between them.
//
static size_t convert_linebreaks(const textchar *text, size_t length, uint32_t linebreak, textchar *dest, bool *after_cr)
{
	size_t n = 0;

	for(size_t i = 0; i < length; i++)
	{
		textchar ch = text[i];
		bool     lf_of_crlf = ch == '\n' && *after_cr;

		*after_cr = ch == '\r';

		if(lf_of_crlf)
			continue;

		if(ch == '\r' || ch == '\n')
		{
			if(linebreak != TXL_LF)
				dest[n++] = '\r';

			if(linebreak != TXL_CR)
				dest[n++] = '\n';
		}
		else
		{
			dest[n++] = ch;
		}
	}

	return n;
}

//
//	Write the text in another encoding, and with every line break made
//	linebreak (unless that is 0). It goes through UTF-16, as it's displayed.
//
bool TextDocument::write_converted(SaveFile &file, TEXT_ENCODING encoding, uint32_t linebreak)
{
	const uint32_t CHUNK = 0x4000;

	std::vector<textchar> text(CHUNK);
	std::vector<textchar> lines(CHUNK * 2);		// each line break can become two
	std::vector<uint8_t>  raw(CHUNK * 2 * 4);	// four bytes a UTF-16 unit at most, as UTF-32
	TextReader reader = text_from_range(TextPos(0), TextPos(byte_length()));
	bool       after_cr = false;

	while(!reader.at_end())
	{
		size_t          len    = reader.read(&text[0], CHUNK);
		const textchar *src    = &text[0];
		size_t          rawlen = raw.size();

		// nothing read short of the end: the text can't be read
		if(len == 0)
			return false;

		if(linebreak)
		{
			len = convert_linebreaks(&text[0], len, linebreak, &lines[0], &after_cr);
			src = &lines[0];
		}

		// raw holds the most any encoding makes of it, so it all goes at once
		if(encode_utf16(encoding, src, len, &raw[0], &rawlen) != len || !file.write(&raw[0], rawlen))
			return false;
	}

	return true;
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

bool TextDocument::modified()
{
	return m_seq.modified();
}
