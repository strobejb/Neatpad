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

struct _BOM_LOOKUP BOMLOOK[] = 
{
	// define longest headers first
	{ 0x0000FEFF, 4, NCP_UTF32    },
	{ 0xFFFE0000, 4, NCP_UTF32BE  },
	{ 0xBFBBEF,	  3, NCP_UTF8	  },
	{ 0xFFFE,	  2, NCP_UTF16BE  },
	{ 0xFEFF,	  2, NCP_UTF16    },
	{ 0,          0, NCP_ASCII	  },
};

//
//	TextDocument constructor
//
TextDocument::TextDocument()
{
//	buffer			= 0;
	
	m_nDocLength_bytes  = 0;
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
	HANDLE hFile;
	
	hFile = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);

	if(hFile == INVALID_HANDLE_VALUE)
		return false;

	return init(hFile);
}

//
//	Initialize using a file-handle
//
bool TextDocument::init(HANDLE hFile)
{
	ULONG numread;
	char *buffer;

	if((m_nDocLength_bytes = GetFileSize(hFile, 0)) == 0)
		return false;

	// allocate new file-buffer
	if((buffer = new char[m_nDocLength_bytes]) == 0)
		return false;

	// read entire file into memory
	ReadFile(hFile, buffer, m_nDocLength_bytes, &numread, 0);

	m_seq.init((BYTE *)buffer, m_nDocLength_bytes);

	// try to detect if this is an ascii/unicode/utf8 file
	m_nFileFormat = detect_file_format(&m_nHeaderSize);

	// work out where each line of text starts
	if(!init_linebuffer())
		clear();

	CloseHandle(hFile);
	delete[] buffer;
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
int TextDocument::detect_file_format(int *m_nHeaderSize)
{
	BYTE header[4] = { 0 };
	m_seq.render(0, header, 4);

	for(int i = 0; BOMLOOK[i].len; i++)
	{
		if(m_nDocLength_bytes >= BOMLOOK[i].len &&
		   memcmp(header, &BOMLOOK[i].bom, BOMLOOK[i].len) == 0)
		{
			*m_nHeaderSize = BOMLOOK[i].len;
			return BOMLOOK[i].type;
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
	m_nDocLength_bytes = 0;

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

bool TextDocument::EmptyDoc()
{
	clear();
	m_seq.init();

	// this is not robust. it's just to get the thing
	// up-and-running until I write a proper line-buffer mananger
	m_pLineBuf_byte = new ULONG[0x1000];
	m_pLineBuf_char = new ULONG[0x1000];

	m_pLineBuf_byte[0] = 0;
	m_pLineBuf_char[0] = 0;

	return true;
}


//
//	Return a UTF-32 character value
//
int TextDocument::getchar(ULONG offset, ULONG lenbytes, ULONG *pch32)
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
ULONG TextDocument::gettext(ULONG offset, ULONG lenbytes, TCHAR *buf, ULONG *buflen)
{
//	BYTE	*rawdata = (BYTE *)(buffer + offset + m_nHeaderSize);

	ULONG chars_copied = 0;
	ULONG bytes_processed = 0;

	if(offset >= m_nDocLength_bytes)
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

ULONG TextDocument::getdata(ULONG offset, BYTE *buf, size_t len)
{
	//memcpy(buf, buffer + offset + m_nHeaderSize, len);
	m_seq.render(offset + m_nHeaderSize, buf, len);
	return len;
}

//
//	Initialize the line-buffer
//
//	With Unicode a newline sequence is defined as any of the following:
//
//	\u000A | \u000B | \u000C | \u000D | \u0085 | \u2028 | \u2029 | \u000D\u000A
//
bool TextDocument::init_linebuffer()
{
	ULONG offset_bytes		= 0;
	ULONG offset_chars		= 0;
	ULONG linestart_bytes	= 0;
	ULONG linestart_chars	= 0;
	ULONG bytes_left	    = m_nDocLength_bytes - m_nHeaderSize;

	ULONG buflen  = m_nDocLength_bytes - m_nHeaderSize;

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
		ULONG len = getchar(offset_bytes, buflen - offset_bytes, &ch32);
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
			len = getchar(offset_bytes, buflen - offset_bytes, &ch32);
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

	return true;
}


//
//	Return the number of lines
//
ULONG TextDocument::linecount()
{
	return m_nNumLines;
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
bool TextDocument::lineinfo_from_lineno(ULONG lineno, ULONG *lineoff_chars,  ULONG *linelen_chars, ULONG *lineoff_bytes, ULONG *linelen_bytes)
{
	if(lineno < m_nNumLines)
	{
		if(linelen_chars) *linelen_chars  = m_pLineBuf_char[lineno+1] - m_pLineBuf_char[lineno];
		if(lineoff_chars) *lineoff_chars  = m_pLineBuf_char[lineno];

		if(linelen_bytes) *linelen_bytes  = m_pLineBuf_byte[lineno+1] - m_pLineBuf_byte[lineno];
		if(lineoff_bytes) *lineoff_bytes  = m_pLineBuf_byte[lineno];

		return true;
	}
	else
	{
		return false;
	}
}

//
//	Perform a reverse lookup - file-offset to line number
//
bool TextDocument::lineinfo_from_offset(ULONG offset_chars, ULONG *lineno, ULONG *lineoff_chars, ULONG *linelen_chars, ULONG *lineoff_bytes, ULONG *linelen_bytes)
{
	ULONG low  = 0;
	ULONG high = m_nNumLines-1;
	ULONG line = 0;

	if(m_nNumLines == 0)
	{
		if(lineno)			*lineno			= 0;
		if(lineoff_chars)	*lineoff_chars	= 0;
		if(linelen_chars)	*linelen_chars  = 0;
		if(lineoff_bytes)	*lineoff_bytes	= 0;
		if(linelen_bytes)	*linelen_bytes  = 0;

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

	if(lineno)			*lineno			= line;
	if(lineoff_bytes)	*lineoff_bytes	= m_pLineBuf_byte[line];
	if(linelen_bytes)	*linelen_bytes  = m_pLineBuf_byte[line+1] - m_pLineBuf_byte[line];
	if(lineoff_chars)	*lineoff_chars  = m_pLineBuf_char[line];
	if(linelen_chars)	*linelen_chars  = m_pLineBuf_char[line+1] - m_pLineBuf_char[line];

	return true;
}

int TextDocument::getformat()
{
	return m_nFileFormat;
}

ULONG TextDocument::size()
{
	return m_nDocLength_bytes;
}

TextIterator TextDocument::iterate(ULONG offset_chars)
{
	ULONG off_bytes = charoffset_to_byteoffset(offset_chars);
	ULONG len_bytes = m_nDocLength_bytes - off_bytes;

	//if(!lineinfo_from_offset(offset_chars, 0, linelen, &offset_bytes, &length_bytes))
	//	return TextIterator();

	return TextIterator(off_bytes, len_bytes, this);
}


//
//
//
TextIterator TextDocument::iterate_line(ULONG lineno, ULONG *linestart, ULONG *linelen)
{
	ULONG offset_bytes;
	ULONG length_bytes;

	if(!lineinfo_from_lineno(lineno, linestart, linelen, &offset_bytes, &length_bytes))
		return TextIterator();
	
	return TextIterator(offset_bytes, length_bytes, this);
}

TextIterator TextDocument::iterate_line_offset(ULONG offset_chars, ULONG *lineno, ULONG *linestart)
{
	ULONG offset_bytes;
	ULONG length_bytes;

	if(!lineinfo_from_offset(offset_chars, lineno, linestart, 0, &offset_bytes, &length_bytes))
		return TextIterator();
	
	return TextIterator(offset_bytes, length_bytes, this);
}

ULONG TextDocument::lineno_from_offset(ULONG offset)
{
	ULONG lineno = 0;
	lineinfo_from_offset(offset, &lineno, 0, 0, 0, 0);
	return lineno;
}

ULONG TextDocument::offset_from_lineno(ULONG lineno)
{
	ULONG lineoff = 0;
	lineinfo_from_lineno(lineno, &lineoff, 0, 0, 0);
	return lineoff;
}

//
//	Retrieve an entire line of text
//	
ULONG TextDocument::getline(ULONG nLineNo, TCHAR *buf, ULONG buflen, ULONG *off_chars)
{
	ULONG offset_bytes;
	ULONG length_bytes;
	ULONG offset_chars;
	ULONG length_chars;

	if(!lineinfo_from_lineno(nLineNo, &offset_chars, &length_chars, &offset_bytes, &length_bytes))
	{
		*off_chars = 0;	
		return 0;
	}

	gettext(offset_bytes, length_bytes, buf, &buflen);
	
	*off_chars = offset_chars;
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

	m_nDocLength_bytes = m_seq.size();
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

	m_nDocLength_bytes = m_seq.size();
	return rawlen;
}

//
//	Erase is a little different. Need to work out how many
//  bytes the specified number of UTF16 characters takes up
//
ULONG TextDocument::erase_raw(ULONG offset_bytes, ULONG length)
{
	/*TCHAR  buf[0x100];
	ULONG  buflen;
	ULONG  bytes;
	ULONG  erase_bytes  = 0;
	ULONG  erase_offset = offset_bytes;

	while(length)
	{
		buflen = min(0x100, length);
		bytes = gettext(offset_bytes, 0x100, buf, &buflen); 

		erase_bytes  += bytes;
		offset_bytes += bytes;
		length       -= buflen;
	}

	// do the piece-table deletion!
	if(m_seq.erase(erase_offset + m_nHeaderSize, erase_bytes))
	{
		m_nDocLength_bytes = m_seq.size();
		return length;
	}*/

	ULONG erase_bytes  = count_chars(offset_bytes, length);
	
	if(m_seq.erase(offset_bytes + m_nHeaderSize, erase_bytes))
	{
		m_nDocLength_bytes = m_seq.size();
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

	while(length_chars && offset_bytes < m_nDocLength_bytes)
	{
		TCHAR buf[0x100];
		ULONG charlen = min(length_chars, 0x100);
		ULONG bytelen;

		bytelen = gettext(offset_bytes, m_nDocLength_bytes - offset_bytes, buf, &charlen);

		length_chars -= charlen;
		offset_bytes += bytelen;
	}

	return offset_bytes - offset_start;
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
		// bug bug! need to implement this. 
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

	ULONG lineoff_chars;
	ULONG lineoff_bytes;

	if(lineinfo_from_offset(offset_chars, 0, &lineoff_chars, 0, &lineoff_bytes, 0))
	{
		return count_chars(lineoff_bytes, offset_chars - lineoff_chars) 
				+ lineoff_bytes;
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
	return insert_raw(offset_bytes, text, length);
}

//
//	Overwrite text at specified character-offset
//
ULONG TextDocument::replace_text(ULONG offset_chars, TCHAR *text, ULONG length, ULONG erase_len)
{
	ULONG offset_bytes = charoffset_to_byteoffset(offset_chars);
	return replace_raw(offset_bytes, text, length, erase_len);
}

//
//	Erase text at specified character-offset
//
ULONG TextDocument::erase_text(ULONG offset_chars, ULONG length)
{
	ULONG offset_bytes = charoffset_to_byteoffset(offset_chars);
	return erase_raw(offset_bytes, length);
}

bool TextDocument::Undo(ULONG *offset_start, ULONG *offset_end)
{
	ULONG start, length;

	if(!m_seq.undo())
		return false;

	start  = m_seq.event_index() - m_nHeaderSize;
	length = m_seq.event_length();

	*offset_start = byteoffset_to_charoffset(start);
	*offset_end   = byteoffset_to_charoffset(start+length);

	m_nDocLength_bytes = m_seq.size();
	
	return true;
}

bool TextDocument::Redo(ULONG *offset_start, ULONG *offset_end)
{
	ULONG start, length;

	if(!m_seq.redo())
		return false;

	start  = m_seq.event_index() - m_nHeaderSize;
	length = m_seq.event_length();

	*offset_start = byteoffset_to_charoffset(start);
	*offset_end   = byteoffset_to_charoffset(start+length);
	
	m_nDocLength_bytes = m_seq.size();

	return true;
}
