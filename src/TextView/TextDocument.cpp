//
//	MODULE:		TextDocument.cpp
//
//	PURPOSE:	Basic implementation of a text data-sequence class
//
//	NOTES:		www.catch22.net
//

#define STRICT
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include "TextDocument.h"
#include "TextView.h"
#include "Unicode.h"

static BOM_LOOKUP BOMLOOK[] =
{
	// define longest headers first
	{ 0x0000FEFF, 4, NCP_UTF32    },
	{ 0xFFFE0000, 4, NCP_UTF32BE  },
	{ 0xBFBBEF,	  3, NCP_UTF8	  },
	{ 0xFFFE,	  2, NCP_UTF16BE  },
	{ 0xFEFF,	  2, NCP_UTF16    },
	{ 0,          0, NCP_ASCII	  },
};

static TEXT_ENCODING_INFO ENCODINGINFO[] =
{
	{ NCP_ASCII,   8,  TRUE,  TRUE  },
	{ NCP_UTF8,    8,  FALSE, FALSE },
	{ NCP_UTF16,  16, TRUE,  FALSE },
	{ NCP_UTF16BE,16, TRUE,  FALSE },
	{ NCP_UTF32,  32, TRUE,  FALSE },
	{ NCP_UTF32BE,32, TRUE,  FALSE },
};

static const TEXT_ENCODING_INFO * encoding_info(TEXT_ENCODING encoding)
{
	for(int i = 0; i < sizeof(ENCODINGINFO) / sizeof(ENCODINGINFO[0]); i++)
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
	
	m_nDocLength_chars  = 0;

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
bool TextDocument::init(TCHAR *filename)
{
	clear();

	if(!m_seq.open(filename, false))
		return false;

	// try to detect if this is an ascii/unicode/utf8 file
	m_nFileFormat = detect_file_format(&m_nHeaderSize);
	m_seq.set_line_scan_mode(line_scan_mode_from_encoding(m_nFileFormat));

	update_text_length();
	return true;
}

//	Initialize the TextDocument with the specified file
//
/*bool TextDocument::save(TCHAR *filename)
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
	BYTE header[4] = { 0 };
	m_seq.render(0, header, 4);

	for(int i = 0; BOMLOOK[i].len; i++)
	{
		if(m_seq.size() >= BOMLOOK[i].len &&
		   memcmp(header, &BOMLOOK[i].bom, BOMLOOK[i].len) == 0)
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

	m_nDocLength_chars = 0;
	m_nFileFormat = NCP_ASCII;
	m_nHeaderSize = 0;
	return true;
}

//
//	Return a UTF-32 character value
//
int TextDocument::decode_char(ULONG offset, ULONG lenbytes, ULONG *pch32)
{
//	BYTE	*rawdata   = (BYTE *)(buffer + offset + m_nHeaderSize);
	BYTE	rawdata[16];
	ULONG   rendered;

	lenbytes = min(16, lenbytes);
	rendered = (ULONG)m_seq.render(offset + m_nHeaderSize, rawdata, lenbytes);
	lenbytes = min(lenbytes, rendered);

	if(lenbytes == 0 || pch32 == 0)
		return 0;

#ifdef UNICODE

	WCHAR     ch16;

	switch(m_nFileFormat)
	{
	case NCP_ASCII:
		MultiByteToWideChar(CP_ACP, 0, (CCHAR*)rawdata, 1, &ch16, 1);
		*pch32 = ch16;
		return 1;

	case NCP_UTF16:
		return utf16_to_utf32_char(rawdata, lenbytes, pch32);
		
	case NCP_UTF16BE:
		return utf16be_to_utf32_char(rawdata, lenbytes, pch32);

	case NCP_UTF8:
		return utf8_to_utf32(rawdata, lenbytes, pch32);

	default:
		return 0;
	}

#else

	*pch32 = (ULONG)(BYTE)rawdata[0];
	return 1;

#endif
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
//	offset   - BYTE offset within underlying data sequence
//	lenbytes - max number of bytes to process (i.e. to limit to a line)
//  buf		 - UTF16/ASCII output buffer
//	plen	 - [in] - length of buffer, [out] - number of code-units stored
//
//	returns  - number of bytes processed
//
ULONG TextDocument::decode_text(ULONG offset, ULONG lenbytes, TCHAR *buf, ULONG *buflen)
{
//	BYTE	*rawdata = (BYTE *)(buffer + offset + m_nHeaderSize);

	ULONG chars_copied = 0;
	ULONG bytes_processed = 0;

	if(offset >= m_seq.size())
	{
		*buflen = 0;
		return 0;
	}

	while(lenbytes > 0 && *buflen > 0)
	{
		BYTE   rawdata[0x100];
		size_t rawlen = min(lenbytes, 0x100);
		size_t rendered;

		// get next block of data from the piece-table
		rendered = m_seq.render(offset + m_nHeaderSize, rawdata, rawlen);

		if(rendered == 0)
			break;

		// convert to UTF-16 
		size_t tmplen = *buflen;
		rawlen = rawdata_to_utf16(rawdata, rendered, buf, &tmplen);

		// Stop before consuming a raw character that will not fit in the UTF-16 output buffer.
		if(rawlen == 0 && tmplen == 0)
			break;

		lenbytes		-= rawlen;
		offset			+= rawlen;
		bytes_processed += rawlen;

		buf				+= tmplen;
		*buflen			-= tmplen;
		chars_copied	+= tmplen;
	}

	*buflen = chars_copied;
	return bytes_processed;
}

void TextDocument::update_text_length()
{
	ULONG buflen  = m_seq.size() - m_nHeaderSize;

	if(m_nFileFormat == NCP_UTF8 && buflen > MEM_BLOCK_SIZE)
	{
		m_nDocLength_chars = buflen;
	}
	else
	{
		m_nDocLength_chars = byteoffset_to_charoffset(buflen);
	}
}

TEXT_ENCODING TextDocument::getformat()
{
	return m_nFileFormat;
}

ULONG TextDocument::text_length()
{
	return m_nDocLength_chars;
}

TextReader TextDocument::text_from_offset(ULONG offset_chars)
{
	ULONG off_bytes = charoffset_to_byteoffset(offset_chars);
	ULONG len_bytes = m_seq.size() - off_bytes;

	return TextReader(off_bytes, len_bytes, this);
}


//
//
//
TextReader TextDocument::text_from_line(ULONG lineno, ULONG *linestart, ULONG *linelen)
{
	RawLineInfo lineinfo;

	if(!raw_lineinfo_from_lineno(lineno, &lineinfo))
		return TextReader();

	if(linestart) *linestart = lineinfo.lineoff_chars;
	if(linelen) *linelen = lineinfo.linelen_chars;
	
	return TextReader(lineinfo.lineoff_bytes, lineinfo.linelen_bytes, this);
}

TextReader TextDocument::text_from_range(const TextCoord *from, const TextCoord *to)
{
	ULONG offset = min(from->byte_anchor, to->byte_anchor);

	return TextReader(offset, max(from->byte_anchor, to->byte_anchor) - offset, this);
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

ULONG TextReader::read(TCHAR *buf, ULONG buflen)
{
	if(text_doc)
	{
		// get text from the TextDocument at the specified byte-offset
		ULONG len = text_doc->decode_text(off_bytes, len_bytes, buf, &buflen);

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

TextReader::TextReader(ULONG off, ULONG len, TextDocument *td)
	: text_doc(td), off_bytes(off), len_bytes(len)
{
}

//
//	Retrieve an entire line of text
//	
ULONG TextDocument::getline(ULONG nLineNo, TCHAR *buf, ULONG buflen, ULONG *off_chars)
{
	RawLineInfo lineinfo;

	if(!raw_lineinfo_from_lineno(nLineNo, &lineinfo))
	{
		*off_chars = 0;	
		return 0;
	}

	decode_text(lineinfo.lineoff_bytes, lineinfo.linelen_bytes, buf, &buflen);
	
	*off_chars = lineinfo.lineoff_chars;
	return buflen;
}

ULONG TextDocument::getline(DocLine &line, TCHAR *buf, ULONG buflen, ULONG *off_chars)
{
	RawLineInfo lineinfo;

	if(!raw_lineinfo_from_offset(line.offset_chars, &lineinfo))
	{
		if(off_chars)
			*off_chars = 0;

		return 0;
	}

	decode_text(lineinfo.lineoff_bytes, lineinfo.linelen_bytes, buf, &buflen);

	if(off_chars)
		*off_chars = lineinfo.lineoff_chars;

	return buflen;
}

ULONG TextDocument::getline(TextCoord &coord, TCHAR *buf, ULONG buflen, ULONG *off_chars)
{
	ULONG len_bytes = coord.line_next >= coord.line_begin ? coord.line_next - coord.line_begin : 0;

	if(coord.line_begin + len_bytes > m_seq.size() - m_nHeaderSize)
		len_bytes = m_seq.size() - m_nHeaderSize - coord.line_begin;

	decode_text(coord.line_begin, len_bytes, buf, &buflen);

	if(off_chars)
		*off_chars = coord.line.offset_chars;

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
size_t TextDocument::rawdata_to_utf16(BYTE *rawdata, size_t rawlen, TCHAR *utf16str, size_t *utf16len)
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
		rawlen /= sizeof(TCHAR);
		return copy_utf16((UTF16 *)rawdata, rawlen, (UTF16 *)utf16str, utf16len) * sizeof(TCHAR);

	// need to convert from big-endian to little-endian
	case NCP_UTF16BE:
		rawlen /= sizeof(TCHAR);
		return swap_utf16((UTF16 *)rawdata, rawlen, (UTF16 *)utf16str, utf16len) * sizeof(TCHAR);

	// error! we should *never* reach this point
	default:
		*utf16len = 0;
		return 0;	
	}
}

//
//	Converts specified UTF16 string to the underlying RAW format of the text-document
//	(i.e. UTF-16 -> UTF-8
//		  UTF-16 -> UTF-32 etc)
//
//	returns number of WCHARs processed from utf16str
//
size_t TextDocument::utf16_to_rawdata(TCHAR *utf16str, size_t utf16len, BYTE *rawdata, size_t *rawlen)
{
	switch(m_nFileFormat)
	{
	// convert from UTF16 -> ASCII
	case NCP_ASCII:
		return utf16_to_ascii((UTF16 *)utf16str, utf16len, rawdata, rawlen);
		
	// convert from UTF16 -> UTF8
	case NCP_UTF8:
		return utf16_to_utf8((UTF16 *)utf16str, utf16len, rawdata, rawlen);

	// already unicode, do a straight memory copy
	case NCP_UTF16:
		*rawlen /= sizeof(TCHAR);
		utf16len = copy_utf16((UTF16 *)utf16str, utf16len, (UTF16 *)rawdata, rawlen);
		*rawlen *= sizeof(TCHAR);
		return utf16len;

	// need to convert from big-endian to little-endian
	case NCP_UTF16BE:
		*rawlen /= sizeof(TCHAR);
		utf16len = swap_utf16((UTF16 *)utf16str, utf16len, (UTF16 *)rawdata, rawlen);
		*rawlen *= sizeof(TCHAR);
		return utf16len;

	// error! we should *never* reach this point
	default:
		*rawlen = 0;
		return 0;	
	}

}

//
//	Insert UTF-16 text at specified BYTE offset
//
//	returns number of BYTEs stored
//
ULONG TextDocument::insert_raw(ULONG offset_bytes, TCHAR *text, ULONG length)
{
	BYTE  buf[0x100];
	ULONG buflen;
	ULONG copied;
	ULONG rawlen = 0;
	ULONG offset = offset_bytes+ m_nHeaderSize;

	while(length)
	{
		buflen = 0x100;
		copied = utf16_to_rawdata(text, length, buf, (size_t *)&buflen);

		// do the piece-table insertion!
		if(!m_seq.insert(offset, buf, buflen))
			break;

		text   += copied;
		length -= copied;
		rawlen += buflen;
		offset += buflen;
	}

	return rawlen;
}

ULONG TextDocument::replace_raw(ULONG offset_bytes, TCHAR *text, ULONG length, ULONG erase_bytes)
{
	BYTE  buf[0x100];
	ULONG buflen;
	ULONG copied;
	ULONG rawlen = 0;
	ULONG offset = offset_bytes + m_nHeaderSize;

	while(length)
	{
		buflen = 0x100;
		copied = utf16_to_rawdata(text, length, buf, (size_t *)&buflen);

		// do the piece-table replacement!
		if(!m_seq.replace(offset, buf, buflen, erase_bytes))
			break;

		text   += copied;
		length -= copied;
		rawlen += buflen;
		offset += buflen;

		erase_bytes = 0;
	}

	return rawlen;
}

//
//	Erase is a little different. Need to work out how many
//  bytes the specified number of UTF16 characters takes up
//
ULONG TextDocument::erase_raw(ULONG offset_bytes, ULONG length)
{
	ULONG erase_bytes  = count_chars(offset_bytes, length);
	
	if(m_seq.erase(offset_bytes + m_nHeaderSize, erase_bytes))
	{
		return length;
	}
		
	return 0;
}

//
//	return number of bytes comprising 'length_chars' characters
//	in the underlying raw file
//
ULONG TextDocument::count_chars(ULONG offset_bytes, ULONG length_chars)
{
	ULONG rawLength = m_seq.size() - m_nHeaderSize;

	switch(m_nFileFormat)
	{
	case NCP_ASCII:
		return length_chars;

	case NCP_UTF16:
	case NCP_UTF16BE:
		return length_chars * sizeof(WCHAR);

	default:
		break;
	}

	ULONG offset_start = offset_bytes;

	while(length_chars && offset_bytes < rawLength)
	{
		TCHAR buf[0x100];
		ULONG charlen = min(length_chars, 0x100);
		ULONG bytelen;

		bytelen = decode_text(offset_bytes, rawLength - offset_bytes, buf, &charlen);

		if(bytelen == 0 || charlen == 0)
		{
			ULONG ch32 = 0;
			ULONG chlen = decode_char(offset_bytes, rawLength - offset_bytes, &ch32);
			ULONG units = ch32 > 0xffff ? 2 : 1;

			if(chlen == 0)
				break;

			// A UTF-16 offset can point inside a surrogate pair encoded as one raw character.
			offset_bytes += chlen;

			if(length_chars > units)
				length_chars -= units;
			else
				length_chars = 0;

			continue;
		}

		length_chars -= charlen;
		offset_bytes += bytelen;
	}

	return offset_bytes - offset_start;
}

ULONG TextDocument::count_code_units(ULONG offset_bytes, ULONG length_bytes)
{
	ULONG chars = 0;
	ULONG rawLength = m_seq.size() - m_nHeaderSize;

	while(length_bytes && offset_bytes < rawLength)
	{
		TCHAR buf[0x100];
		ULONG charlen = 0x100;
		ULONG bytelen = decode_text(offset_bytes, min(length_bytes, rawLength - offset_bytes), buf, &charlen);

		if(bytelen == 0)
			break;

		chars += charlen;
		offset_bytes += bytelen;
		length_bytes -= bytelen;
	}

	return chars;
}

ULONG TextDocument::byteoffset_to_charoffset(ULONG offset_bytes)
{
	switch(m_nFileFormat)
	{
	case NCP_ASCII:
		return offset_bytes;

	case NCP_UTF16:
	case NCP_UTF16BE:
		return offset_bytes / sizeof(WCHAR);

	case NCP_UTF8:
	case NCP_UTF32:
	case NCP_UTF32BE:
		return count_code_units(0, offset_bytes);

	default:
		break;
	}

	return 0;
}

ULONG TextDocument::charoffset_to_byteoffset(ULONG offset_chars)
{
	switch(m_nFileFormat)
	{
	case NCP_ASCII:
		return offset_chars;

	case NCP_UTF16:
	case NCP_UTF16BE:
		return offset_chars * sizeof(WCHAR);

	case NCP_UTF8:
	case NCP_UTF32:
	case NCP_UTF32BE:
	default:
		break;
	}

	RawLineInfo lineinfo;

	if(raw_lineinfo_from_offset(offset_chars, &lineinfo))
	{
		return count_chars(lineinfo.lineoff_bytes, offset_chars - lineinfo.lineoff_chars)
				+ lineinfo.lineoff_bytes;
	}
	else
	{
		return 0;
	}
}

//
//	Insert text at specified character-offset
//
ULONG TextDocument::insert_text(ULONG offset_chars, TCHAR *text, ULONG length, TextChange *change)
{
	ULONG offset_bytes = charoffset_to_byteoffset(offset_chars);
	ULONG old_size = m_seq.size();
	ULONG rawlen = insert_raw(offset_bytes, text, length);

	if(rawlen)
	{
		m_nDocLength_chars += length;
		update_text_length();
	}

	if(change)
	{
		change->offset = offset_bytes;
		change->erased = 0;
		change->inserted = m_seq.size() - old_size;
	}

	return rawlen;
}

//
//	Overwrite text at specified character-offset
//
ULONG TextDocument::replace_text(ULONG offset_chars, TCHAR *text, ULONG length, ULONG erase_len, TextChange *change)
{
	ULONG offset_bytes = charoffset_to_byteoffset(offset_chars);
	ULONG old_size = m_seq.size();
	ULONG rawlen = replace_raw(offset_bytes, text, length, count_chars(offset_bytes, erase_len));

	if(rawlen)
	{
		m_nDocLength_chars = m_nDocLength_chars - erase_len + length;
		update_text_length();
	}

	// The sequence clamps a replace that runs past the end, so measure what it erased.
	if(change)
	{
		change->offset = offset_bytes;
		change->erased = old_size + rawlen - m_seq.size();
		change->inserted = rawlen;
	}

	return rawlen;
}

//
//	Erase text at specified character-offset
//
ULONG TextDocument::erase_text(ULONG offset_chars, ULONG length, TextChange *change)
{
	ULONG offset_bytes = charoffset_to_byteoffset(offset_chars);
	ULONG old_size = m_seq.size();
	ULONG erased = erase_raw(offset_bytes, length);

	if(erased)
	{
		m_nDocLength_chars -= erased;
		update_text_length();
	}

	if(change)
	{
		change->offset = offset_bytes;
		change->erased = old_size - m_seq.size();
		change->inserted = 0;
	}

	return erased;
}

//
//	Insert text at a coordinate
//
ULONG TextDocument::insert_text(const TextCoord *at, TCHAR *text, ULONG length, TextChange *change)
{
	ULONG old_size = m_seq.size();
	ULONG rawlen = insert_raw(at->byte_anchor, text, length);

	if(rawlen)
		update_text_length();

	if(change)
	{
		change->offset = at->byte_anchor;
		change->erased = 0;
		change->inserted = m_seq.size() - old_size;
	}

	return rawlen;
}

//
//	Replace the text between two coordinates
//
ULONG TextDocument::replace_text(const TextCoord *from, const TextCoord *to, TCHAR *text, ULONG length, TextChange *change)
{
	ULONG offset = min(from->byte_anchor, to->byte_anchor);
	ULONG erase_bytes = max(from->byte_anchor, to->byte_anchor) - offset;
	ULONG old_size = m_seq.size();
	ULONG rawlen = replace_raw(offset, text, length, erase_bytes);

	if(rawlen)
		update_text_length();

	if(change)
	{
		change->offset = offset;
		change->erased = old_size + rawlen - m_seq.size();
		change->inserted = rawlen;
	}

	return rawlen;
}

//
//	Erase the text between two coordinates
//
ULONG TextDocument::erase_text(const TextCoord *from, const TextCoord *to, TextChange *change)
{
	ULONG offset = min(from->byte_anchor, to->byte_anchor);
	ULONG erase_bytes = max(from->byte_anchor, to->byte_anchor) - offset;
	ULONG erased = 0;

	if(erase_bytes && m_seq.erase(offset + m_nHeaderSize, erase_bytes))
	{
		erased = erase_bytes;
		update_text_length();
	}

	if(change)
	{
		change->offset = offset;
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

	change->offset = offset > (size_w)m_nHeaderSize ? (ULONG)(offset - m_nHeaderSize) : 0;
	change->erased = (ULONG)erased;
	change->inserted = (ULONG)inserted;
}

//
//	Undo/redo by coordinate: the change says which bytes came back
//
bool TextDocument::undo(TextChange *change)
{
	if(!m_seq.undo())
		return false;

	update_text_length();
	event_change(change);
	return true;
}

bool TextDocument::redo(TextChange *change)
{
	if(!m_seq.redo())
		return false;

	update_text_length();
	event_change(change);
	return true;
}

bool TextDocument::undo(ULONG *offset_start, ULONG *offset_end, TextChange *change)
{
	ULONG start, length;

	if(!m_seq.undo())
		return false;

	start  = m_seq.event_index() - m_nHeaderSize;
	length = m_seq.event_length();

	*offset_start = byteoffset_to_charoffset(start);
	*offset_end   = byteoffset_to_charoffset(start+length);

	m_nDocLength_chars = byteoffset_to_charoffset(m_seq.size() - m_nHeaderSize);
	update_text_length();
	event_change(change);

	return true;
}

bool TextDocument::redo(ULONG *offset_start, ULONG *offset_end, TextChange *change)
{
	ULONG start, length;

	if(!m_seq.redo())
		return false;

	start  = m_seq.event_index() - m_nHeaderSize;
	length = m_seq.event_length();

	*offset_start = byteoffset_to_charoffset(start);
	*offset_end   = byteoffset_to_charoffset(start+length);

	m_nDocLength_chars = byteoffset_to_charoffset(m_seq.size() - m_nHeaderSize);
	update_text_length();
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
