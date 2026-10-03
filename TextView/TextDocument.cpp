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

static bool is_linebreak(TCHAR ch)
{
	return ch == '\r' || ch == '\n' ||
		ch == '\x0b' || ch == '\x0c' ||
		ch == '\x85' || ch == 0x2028 ||
		ch == 0x2029;
}

//
//	TextDocument constructor
//
TextDocument::TextDocument()
{
//	buffer			= 0;
	
	m_nDocLength_chars  = 0;

	m_pLineBuf_byte		= 0;
	m_pLineBuf_char		= 0;
	m_nNumLines			= 0;

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

	// Initialize either the sequence-backed lazy index or the legacy line buffer.
	if(!init_line_index())
	{
		clear();
		return false;
	}

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
	m_lineBuffer.clear();

	if(m_pLineBuf_byte)
	{
		delete[] m_pLineBuf_byte;
		m_pLineBuf_byte = 0;
	}

	if(m_pLineBuf_char)
	{
		delete[] m_pLineBuf_char;
		m_pLineBuf_char = 0;
	}
		
	m_nNumLines = 0;
	return true;
}

//
//	Return a UTF-32 character value
//
int TextDocument::decode_char(ULONG offset, ULONG lenbytes, ULONG *pch32)
{
//	BYTE	*rawdata   = (BYTE *)(buffer + offset + m_nHeaderSize);
	BYTE	rawdata[16];

	lenbytes = min(16, lenbytes);
	m_seq.render(offset+ m_nHeaderSize, rawdata, lenbytes);

#ifdef UNICODE

	UTF16   *rawdata_w = (UTF16 *)rawdata;//(WCHAR*)(buffer + offset + m_nHeaderSize);
	WCHAR     ch16;
	size_t   ch32len = 1;

	switch(m_nFileFormat)
	{
	case NCP_ASCII:
		MultiByteToWideChar(CP_ACP, 0, (CCHAR*)rawdata, 1, &ch16, 1);
		*pch32 = ch16;
		return 1;

	case NCP_UTF16:
		return utf16_to_utf32(rawdata_w, lenbytes / 2, pch32, &ch32len) * sizeof(WCHAR);
		
	case NCP_UTF16BE:
		return utf16be_to_utf32(rawdata_w, lenbytes / 2, pch32, &ch32len) * sizeof(WCHAR);

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

		// get next block of data from the piece-table
		m_seq.render(offset + m_nHeaderSize, rawdata, rawlen);

		// convert to UTF-16 
		size_t tmplen = *buflen;
		rawlen = rawdata_to_utf16(rawdata, rawlen, buf, &tmplen);

		lenbytes		-= rawlen;
		offset			+= rawlen;
		bytes_processed += rawlen;

		buf				+= tmplen;
		*buflen			-= tmplen;
		chars_copied	+= tmplen;
	}

	*buflen = chars_copied;
	return bytes_processed;

	//ULONG remaining = lenbytes;
	//int   charbuflen = *buflen;

	//while(remaining)
/*	{
		lenbytes = min(lenbytes, sizeof(rawdata));
		m_seq.render(offset + m_nHeaderSize, rawdata, lenbytes);

#ifdef UNICODE

	switch(m_nFileFormat)
	{
	// convert from ANSI->UNICODE
	case NCP_ASCII:
		return ascii_to_utf16(rawdata, lenbytes, buf, (size_t*)buflen);
		
	case NCP_UTF8:
		return utf8_to_utf16(rawdata, lenbytes, buf, (size_t*)buflen);

	// already unicode, do a straight memory copy
	case NCP_UTF16:
		return copy_utf16((WCHAR*)rawdata, lenbytes/sizeof(WCHAR), buf, (size_t*)buflen);

	// need to convert from big-endian to little-endian
	case NCP_UTF16BE:
		return swap_utf16((WCHAR*)rawdata, lenbytes/sizeof(WCHAR), buf, (size_t*)buflen);

	// error! we should *never* reach this point
	default:
		*buflen = 0;
		return 0;	
	}

#else

	switch(m_nFileFormat)
	{
	// we are already an ASCII app, so do a straight memory copy
	case NCP_ASCII:

		int len;
		
		len = min(*buflen, lenbytes);
		memcpy(buf, rawdata, len);

		*buflen = len;
		return len;

	// anything else is an error - we cannot support Unicode or multibyte
	// character sets with a plain ASCII app.
	default:
		*buflen = 0;
		return 0;
	}

#endif

	//	remaining -= lenbytes;
	//	buf       += lenbytes;
	//	offset    += lenbytes;
	}*/
}

bool TextDocument::use_document_line_index() const
{
	return encoding_info(m_nFileFormat)->byte_offset_equals_char_offset && m_nHeaderSize == 0;
}

void TextDocument::copy_linebuffer_info(TextLineBufferInfo *source, RawLineInfo *dest)
{
	dest->lineno = source->lineno;
	dest->lineoff_chars = source->lineoff_chars;
	dest->linelen_chars = source->linelen_chars;
	dest->lineoff_bytes = source->lineoff_bytes;
	dest->linelen_bytes = source->linelen_bytes;
}

//
//	Initialize the legacy line-buffer used by non-document-indexed encodings.
//
//	With Unicode a newline sequence is defined as any of the following:
//
//	\u000A | \u000B | \u000C | \u000D | \u0085 | \u2028 | \u2029 | \u000D\u000A
//
bool TextDocument::init_line_index()
{
	ULONG offset_bytes		= 0;
	ULONG offset_chars		= 0;
	ULONG linestart_bytes	= 0;
	ULONG linestart_chars	= 0;

	ULONG buflen  = m_seq.size() - m_nHeaderSize;

	if(m_pLineBuf_byte)
	{
		delete[] m_pLineBuf_byte;
		m_pLineBuf_byte = 0;
	}

	if(m_pLineBuf_char)
	{
		delete[] m_pLineBuf_char;
		m_pLineBuf_char = 0;
	}

	m_lineBuffer.clear();

	if(use_document_line_index())
	{
		m_nDocLength_chars = m_seq.size() - m_nHeaderSize;
		return m_lineBuffer.init(this);
	}

	// allocate the line-buffer for storing each line's BYTE offset
	if((m_pLineBuf_byte = new ULONG[buflen+1]) == 0)
		return false;

	// allocate the line-buffer for storing each line's CHARACTER offset
	if((m_pLineBuf_char = new ULONG[buflen+1]) == 0)
		return false;

	m_nNumLines = 0;


	// loop through every byte in the file
	for(offset_bytes = 0; offset_bytes < buflen; )
	{
		ULONG ch32;

		// get a UTF-32 character from the underlying file format.
		// this needs serious thought. Currently 
		ULONG len = decode_char(offset_bytes, buflen - offset_bytes, &ch32);
		offset_bytes += len;
		offset_chars += 1;

		if(ch32 == '\r')
		{
			// record where the line starts
			m_pLineBuf_byte[m_nNumLines] = linestart_bytes;
			m_pLineBuf_char[m_nNumLines] = linestart_chars;
			linestart_bytes				= offset_bytes;
			linestart_chars				= offset_chars;

			// look ahead to next char
			len = decode_char(offset_bytes, buflen - offset_bytes, &ch32);
			offset_bytes += len;
			offset_chars += 1;

			// carriage-return / line-feed combination
			if(ch32 == '\n')
			{
				linestart_bytes		= offset_bytes;
				linestart_chars		= offset_chars;
			}
			
			m_nNumLines++;
		}
		else if(ch32 == '\n' || ch32 == '\x0b' || ch32 == '\x0c' || ch32 == 0x0085 || ch32 == 0x2029 || ch32 == 0x2028)
		{
			// record where the line starts
			m_pLineBuf_byte[m_nNumLines] = linestart_bytes;
			m_pLineBuf_char[m_nNumLines] = linestart_chars;
			linestart_bytes				= offset_bytes;
			linestart_chars				= offset_chars;
			m_nNumLines++;
		}
		// force a 'hard break' 
		else if(offset_chars - linestart_chars > 128)
		{
			m_pLineBuf_byte[m_nNumLines] = linestart_bytes;
			m_pLineBuf_char[m_nNumLines] = linestart_chars;
			linestart_bytes				= offset_bytes;
			linestart_chars				= offset_chars;
			m_nNumLines++;
		}
	}

	if(buflen > 0)
	{
		m_pLineBuf_byte[m_nNumLines] = linestart_bytes;
		m_pLineBuf_char[m_nNumLines] = linestart_chars;
		m_nNumLines++;
	}

	m_pLineBuf_byte[m_nNumLines] = buflen;
	m_pLineBuf_char[m_nNumLines] = offset_chars;
	m_nDocLength_chars = offset_chars;

	return true;
}

//
//	Return the number of lines
//
ULONG TextDocument::linecount()
{
	if(use_document_line_index())
		return m_lineBuffer.linecount();

	return m_nNumLines;
}

bool TextDocument::linecount_known()
{
	if(use_document_line_index())
		return m_lineBuffer.linecount_known();

	return true;
}

bool TextDocument::lineno_known(ULONG lineno)
{
	if(!use_document_line_index())
		return lineno < m_nNumLines;

	return m_lineBuffer.lineno_known(lineno);
}

bool TextDocument::line_number_range_known(ULONG offset_chars, ULONG length_chars)
{
	if(use_document_line_index())
		return m_lineBuffer.line_number_range_known(offset_chars, length_chars);

	return true;
}

void TextDocument::index_lines(ULONG offset_chars, ULONG length_chars)
{
	if(use_document_line_index())
		m_lineBuffer.index_lines(offset_chars, length_chars);
}

//
//	Return the length of longest line
//
ULONG TextDocument::longestline(int tabwidth)
{
	//ULONG i;
	ULONG longest = 0;
	ULONG xpos = 0;
//	char *bufptr = (char *)(buffer + m_nHeaderSize);
/*
	for(i = 0; i < length_bytes; i++)
	{
		if(bufptr[i] == '\r')
		{
			if(bufptr[i+1] == '\n')
				 i++;

			longest = max(longest, xpos);
			xpos = 0;
		}
		else if(bufptr[i] == '\n')
		{
			longest = max(longest, xpos);
			xpos = 0;
		}
		else if(bufptr[i] == '\t')
		{
			xpos += tabwidth - (xpos % tabwidth);
		}
		else
		{
			xpos ++;
		}
	}

	longest = max(longest, xpos);*/
	return 100;//longest;
}

//
//	Return information about specified line
//
bool TextDocument::raw_lineinfo_from_lineno(ULONG lineno, RawLineInfo *lineinfo)
{
	if(lineinfo == 0)
		return false;

	if(use_document_line_index())
	{
		TextLineBufferInfo bufferinfo;

		if(!m_lineBuffer.lineinfo_from_lineno(lineno, &bufferinfo))
			return false;

		copy_linebuffer_info(&bufferinfo, lineinfo);
		return true;
	}

	if(lineno < m_nNumLines)
	{
		lineinfo->lineno = lineno;
		lineinfo->lineoff_chars = m_pLineBuf_char[lineno];
		lineinfo->linelen_chars = m_pLineBuf_char[lineno+1] - m_pLineBuf_char[lineno];
		lineinfo->lineoff_bytes = m_pLineBuf_byte[lineno];
		lineinfo->linelen_bytes = m_pLineBuf_byte[lineno+1] - m_pLineBuf_byte[lineno];

		return true;
	}
	else
	{
		return false;
	}
}

bool TextDocument::lineinfo_from_lineno(ULONG lineno, ULONG *lineoff_chars, ULONG *linelen_chars)
{
	RawLineInfo lineinfo;

	if(!raw_lineinfo_from_lineno(lineno, &lineinfo))
		return false;

	if(lineoff_chars) *lineoff_chars = lineinfo.lineoff_chars;
	if(linelen_chars) *linelen_chars = lineinfo.linelen_chars;

	return true;
}

//
//	Perform a reverse lookup - file-offset to line number
//
bool TextDocument::raw_lineinfo_from_offset(ULONG offset_chars, RawLineInfo *lineinfo)
{
	ULONG low  = 0;
	ULONG high = m_nNumLines-1;
	ULONG line = 0;

	if(lineinfo == 0)
		return false;

	if(use_document_line_index())
	{
		TextLineBufferInfo bufferinfo;

		if(!m_lineBuffer.lineinfo_from_offset(offset_chars, &bufferinfo))
			return false;

		copy_linebuffer_info(&bufferinfo, lineinfo);
		return true;
	}

	if(m_nNumLines == 0)
	{
		lineinfo->lineno = 0;
		lineinfo->lineoff_chars = 0;
		lineinfo->linelen_chars = 0;
		lineinfo->lineoff_bytes = 0;
		lineinfo->linelen_bytes = 0;

		return false;
	}

	while(low <= high)
	{
		line = (high + low) / 2;

		if(offset_chars >= m_pLineBuf_char[line] && offset_chars < m_pLineBuf_char[line+1])
		{
			break;
		}
		else if(offset_chars < m_pLineBuf_char[line])
		{
			high = line-1;
		}
		else
		{
			low = line+1;
		}
	}

	lineinfo->lineno = line;
	lineinfo->lineoff_chars = m_pLineBuf_char[line];
	lineinfo->linelen_chars = m_pLineBuf_char[line+1] - m_pLineBuf_char[line];
	lineinfo->lineoff_bytes = m_pLineBuf_byte[line];
	lineinfo->linelen_bytes = m_pLineBuf_byte[line+1] - m_pLineBuf_byte[line];

	return true;
}

bool TextDocument::lineinfo_from_offset(ULONG offset_chars, ULONG *lineno, ULONG *lineoff_chars, ULONG *linelen_chars)
{
	RawLineInfo lineinfo;

	if(!raw_lineinfo_from_offset(offset_chars, &lineinfo))
		return false;

	if(lineno) *lineno = lineinfo.lineno;
	if(lineoff_chars) *lineoff_chars = lineinfo.lineoff_chars;
	if(linelen_chars) *linelen_chars = lineinfo.linelen_chars;

	return true;
}

bool TextDocument::lineinfo_from_offset(ULONG offset_chars, TextLineInfo *lineinfo)
{
	RawLineInfo rawinfo;

	if(lineinfo == 0)
		return false;

	if(!raw_lineinfo_from_offset(offset_chars, &rawinfo))
		return false;

	lineinfo->lineno = rawinfo.lineno;
	lineinfo->lineoff_chars = rawinfo.lineoff_chars;
	lineinfo->linelen_chars = rawinfo.linelen_chars;

	return true;
}

ULONG TextDocument::line_text_end(TextLineInfo *lineinfo)
{
	RawLineInfo rawinfo;
	ULONG offset_chars;
	ULONG offset_bytes;
	ULONG bytes_left;

	if(lineinfo == 0)
		return 0;

	if(!raw_lineinfo_from_offset(lineinfo->lineoff_chars, &rawinfo))
		return lineinfo->lineoff_chars;

	offset_chars = rawinfo.lineoff_chars;
	offset_bytes = rawinfo.lineoff_bytes;
	bytes_left = rawinfo.linelen_bytes;

	while(bytes_left)
	{
		TCHAR buf[0x100];
		ULONG chars = 0x100;
		ULONG bytes = decode_text(offset_bytes, bytes_left, buf, &chars);

		for(ULONG i = 0; i < chars; i++)
		{
			if(is_linebreak(buf[i]))
				return offset_chars + i;
		}

		if(bytes == 0)
			break;

		offset_chars += chars;
		offset_bytes += bytes;

		if(bytes > bytes_left)
			break;

		bytes_left -= bytes;
	}

	return offset_chars;
}

bool TextDocument::previous_lineinfo_from_offset(ULONG offset_chars, ULONG num_lines, TextLineInfo *lineinfo)
{
	ULONG docLength = text_length();
	TextLineInfo target;

	if(lineinfo == 0 || docLength == 0)
		return false;

	if(offset_chars > docLength)
		offset_chars = docLength;

	if(!lineinfo_from_offset(offset_chars, &target))
		return false;

	// Walk using physical line starts, so navigation does not depend on exact global line numbers.
	while(num_lines-- > 0 && target.lineoff_chars > 0)
	{
		TextLineInfo prev;
		ULONG probe = target.lineoff_chars - 1;

		if(!lineinfo_from_offset(probe, &prev))
			break;

		// A line-boundary lookup can resolve back to the current line; step inside the previous line.
		if(prev.lineoff_chars >= target.lineoff_chars && target.lineoff_chars > 1)
		{
			if(!lineinfo_from_offset(target.lineoff_chars - 2, &prev))
				break;
		}

		if(prev.lineoff_chars >= target.lineoff_chars)
			break;

		target = prev;
	}

	*lineinfo = target;
	return true;
}

bool TextDocument::next_lineinfo_from_offset(ULONG offset_chars, ULONG num_lines, TextLineInfo *lineinfo)
{
	ULONG docLength = text_length();
	TextLineInfo target;

	if(lineinfo == 0 || docLength == 0)
		return false;

	if(offset_chars > docLength)
		offset_chars = docLength;

	if(!lineinfo_from_offset(offset_chars, &target))
		return false;

	// Walk by line length to reach the next physical line, stopping cleanly at EOF.
	while(num_lines-- > 0)
	{
		TextLineInfo next;
		ULONG lineEnd;

		if(target.lineoff_chars >= docLength || target.linelen_chars > docLength - target.lineoff_chars)
			lineEnd = docLength;
		else
			lineEnd = target.lineoff_chars + target.linelen_chars;

		if(lineEnd >= docLength)
		{
			if(target.linelen_chars == 0 || !lineinfo_from_offset(docLength, &next))
				break;

			if(next.lineoff_chars != docLength || next.lineno == target.lineno)
				break;
		}
		else if(!lineinfo_from_offset(lineEnd, &next))
		{
			break;
		}

		if(next.lineoff_chars <= target.lineoff_chars)
			break;

		target = next;
	}

	*lineinfo = target;
	return true;
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

ULONG TextDocument::lineno_from_offset(ULONG offset)
{
	ULONG lineno = 0;
	lineinfo_from_offset(offset, &lineno, 0, 0);
	return lineno;
}

ULONG TextDocument::offset_from_lineno(ULONG lineno)
{
	ULONG lineoff = 0;
	lineinfo_from_lineno(lineno, &lineoff, 0);
	return lineoff;
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

ULONG TextDocument::replace_raw(ULONG offset_bytes, TCHAR *text, ULONG length, ULONG erase_chars)
{
	BYTE  buf[0x100];
	ULONG buflen;
	ULONG copied;
	ULONG rawlen = 0;
	ULONG offset = offset_bytes + m_nHeaderSize;

	ULONG erase_bytes = count_chars(offset_bytes, erase_chars);

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

	while(length_chars && offset_bytes < m_seq.size())
	{
		TCHAR buf[0x100];
		ULONG charlen = min(length_chars, 0x100);
		ULONG bytelen;

		bytelen = decode_text(offset_bytes, m_seq.size() - offset_bytes, buf, &charlen);

		length_chars -= charlen;
		offset_bytes += bytelen;
	}

	return offset_bytes - offset_start;
}

ULONG TextDocument::count_code_units(ULONG offset_bytes, ULONG length_bytes)
{
	ULONG chars = 0;

	while(length_bytes && offset_bytes < m_seq.size())
	{
		TCHAR buf[0x100];
		ULONG charlen = 0x100;
		ULONG bytelen = decode_text(offset_bytes, length_bytes, buf, &charlen);

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
ULONG TextDocument::insert_text(ULONG offset_chars, TCHAR *text, ULONG length)
{
	ULONG offset_bytes = charoffset_to_byteoffset(offset_chars);
	ULONG rawlen = insert_raw(offset_bytes, text, length);

	if(rawlen)
	{
		m_nDocLength_chars += length;
		init_line_index();
	}

	return rawlen;
}

//
//	Overwrite text at specified character-offset
//
ULONG TextDocument::replace_text(ULONG offset_chars, TCHAR *text, ULONG length, ULONG erase_len)
{
	ULONG offset_bytes = charoffset_to_byteoffset(offset_chars);
	ULONG rawlen = replace_raw(offset_bytes, text, length, erase_len);

	if(rawlen)
	{
		m_nDocLength_chars = m_nDocLength_chars - erase_len + length;
		init_line_index();
	}

	return rawlen;
}

//
//	Erase text at specified character-offset
//
ULONG TextDocument::erase_text(ULONG offset_chars, ULONG length)
{
	ULONG offset_bytes = charoffset_to_byteoffset(offset_chars);
	ULONG erased = erase_raw(offset_bytes, length);

	if(erased)
	{
		m_nDocLength_chars -= erased;
		init_line_index();
	}

	return erased;
}

bool TextDocument::undo(ULONG *offset_start, ULONG *offset_end)
{
	ULONG start, length;

	if(!m_seq.undo())
		return false;

	start  = m_seq.event_index() - m_nHeaderSize;
	length = m_seq.event_length();

	*offset_start = byteoffset_to_charoffset(start);
	*offset_end   = byteoffset_to_charoffset(start+length);

	m_nDocLength_chars = byteoffset_to_charoffset(m_seq.size() - m_nHeaderSize);
	init_line_index();
	
	return true;
}

bool TextDocument::redo(ULONG *offset_start, ULONG *offset_end)
{
	ULONG start, length;

	if(!m_seq.redo())
		return false;

	start  = m_seq.event_index() - m_nHeaderSize;
	length = m_seq.event_length();

	*offset_start = byteoffset_to_charoffset(start);
	*offset_end   = byteoffset_to_charoffset(start+length);
	
	m_nDocLength_chars = byteoffset_to_charoffset(m_seq.size() - m_nHeaderSize);
	init_line_index();

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
