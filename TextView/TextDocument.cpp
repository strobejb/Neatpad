//
//	MODULE:		TextDocument.cpp
//
//	PURPOSE:	Basic implementation of a text data-sequence class
//
//	NOTES:		www.catch22.net
//
#include <windows.h>
#include "TextDocument.h"
#include "TextView.h"

//
//	Conversions to UTF-16
//
int	   ascii_to_utf16(BYTE *asciistr, size_t asciilen, wchar_t *utf16str, size_t *utf16len);
int    utf8_to_utf16(BYTE *utf8str, size_t utf8len, wchar_t *utf16str, size_t *utf16len);
size_t utf8_to_utf32(BYTE *utf8str, size_t utf8len, DWORD *pcp32);

int	   copy_utf16(wchar_t *src, size_t srclen, wchar_t *dest, size_t *destlen);
int	   swap_utf16(wchar_t *src, size_t srclen, wchar_t *dest, size_t *destlen);

//
//	Conversions to UTF-32
//
int    utf16_to_utf32(WCHAR *utf16str, size_t utf16len, ULONG *utf32str, size_t *utf32len);
int    utf16be_to_utf32(WCHAR *utf16str, size_t utf16len, ULONG *utf32str, size_t *utf32len);



struct _BOM_LOOKUP
{
	DWORD  bom;
	ULONG  len;
	int    type;

} BOMLOOK[] = 
{
	// define longest headers first
	{ 0x0000FEFF, 4, NCP_UTF32    },
	{ 0xFFFE0000, 4, NCP_UTF32BE  },
	{ 0xBFBBEF,	  3, NCP_UTF8	  },
	{ 0xFFFE,	  2, NCP_UTF16BE  },
	{ 0xFEFF,	  2, NCP_UTF16    },
	{ 0,          0, NCP_ASCII	  },
};

#define SWAPWORD(val) (((WORD)(val) << 8) | ((WORD)(val) >> 8))

//
//	TextDocument constructor
//
TextDocument::TextDocument()
{
	buffer			= 0;
	
	length_bytes    = 0;
	length_chars    = 0;

	linebuf_byte	= 0;
	linebuf_char    = 0;
	numlines		= 0;

	fileformat		= NCP_ASCII;
	headersize		= 0;
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

	if((length_bytes = GetFileSize(hFile, 0)) == 0)
		return false;

	// allocate new file-buffer
	if((buffer = new char[length_bytes]) == 0)
		return false;

	// read entire file into memory
	ReadFile(hFile, buffer, length_bytes, &numread, 0);

	// try to detect if this is an ascii/unicode/utf8 file
	fileformat = detect_file_format(&headersize);

	// work out where each line of text starts
	if(!init_linebuffer())
		clear();

	CloseHandle(hFile);
	return true;
}


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
int TextDocument::detect_file_format(int *headersize)
{
	for(int i = 0; BOMLOOK[i].len; i++)
	{
		if(length_bytes >= BOMLOOK[i].len &&
		   memcmp(buffer, &BOMLOOK[i].bom, BOMLOOK[i].len) == 0)
		{
			*headersize = BOMLOOK[i].len;
			return BOMLOOK[i].type;
		}
	}

	*headersize = 0;
	return NCP_ASCII;	// default to ASCII
}


//
//	Empty the data-TextDocument
//
bool TextDocument::clear()
{
	if(buffer)
	{
		delete buffer;
		buffer		 = 0;
		length_bytes = 0;
	}

	if(linebuf_byte)
	{
		delete linebuf_byte;
		linebuf_byte = 0;
	}

	if(linebuf_char)
	{
		delete linebuf_char;
		linebuf_char = 0;
	}
		
	numlines = 0;
	return true;
}

//
//	Return a UTF-32 character value
//
int TextDocument::getchar(ULONG offset, ULONG lenbytes, ULONG *pch32)
{
	BYTE	*rawdata   = (BYTE *)(buffer + offset + headersize);
	WORD    *rawdata_w = (WORD *)(buffer + offset + headersize);
	WORD     ch16;
	size_t   ch32len = 1;

#ifdef UNICODE

	switch(fileformat)
	{
	// convert from ANSI->UNICODE
	case NCP_ASCII:
		MultiByteToWideChar(CP_ACP, 0, (CCHAR*)rawdata, 1, &ch16, 1);
		*pch32 = ch16;
		return 1;

	case NCP_UTF16:
		//*pch32 = (ULONG)(WORD)rawdata_w[0];
		//return 2;

		return utf16_to_utf32(rawdata_w, lenbytes / 2, pch32, &ch32len) * 2;
		
	case NCP_UTF16BE:
		//*pch32 = (ULONG)(WORD)SWAPWORD((WORD)rawdata_w[0]);
		//return 2;

		return utf16be_to_utf32(rawdata_w, lenbytes / 2, pch32, &ch32len) * 2;


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
//	Fetch a buffer of text from the specified offset - 
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
int	 TextDocument::gettext(ULONG offset, ULONG lenbytes, TCHAR *buf, int *buflen)
{
	BYTE	*rawdata = (BYTE *)(buffer + offset + headersize);
	int		 len;

	if(offset >= length_bytes)
	{
		*buflen = 0;
		return 0;
	}

#ifdef UNICODE

	switch(fileformat)
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

	switch(fileformat)
	{
	// we are already an ASCII app, so do a straight memory copy
	case NCP_ASCII:
		
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

}

ULONG TextDocument::getdata(ULONG offset, BYTE *buf, size_t len)
{
	memcpy(buf, buffer + offset, len);
	return len;
}

//
//	Initialize the line-buffer
//
bool TextDocument::init_linebuffer()
{
	ULONG offset_bytes		= 0;
	ULONG offset_chars		= 0;
	ULONG linestart_bytes	= 0;
	ULONG linestart_chars	= 0;
	ULONG bytes_left	    = length_bytes - headersize;

	ULONG buflen  = length_bytes - headersize;

	// allocate the line-buffer for storing each line's BYTE offset
	if((linebuf_byte = new ULONG[buflen]) == 0)
		return false;

	// allocate the line-buffer for storing each line's CHARACTER offset
	if((linebuf_char = new ULONG[buflen]) == 0)
		return false;

	numlines = 0;


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
			linebuf_byte[numlines] = linestart_bytes;
			linebuf_char[numlines] = linestart_chars;
			linestart_bytes		   = offset_bytes;
			linestart_chars		   = offset_chars;

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
			
			numlines++;
		}
		else if(ch32 == '\n')
		{
			// record where the line starts
			linebuf_byte[numlines] = linestart_bytes;
			linebuf_char[numlines] = linestart_chars;
			linestart_bytes		   = offset_bytes;
			linestart_chars		   = offset_chars;
			numlines++;
		}
	}

	if(buflen > 0)
	{
		linebuf_byte[numlines] = linestart_bytes;
		linebuf_char[numlines] = linestart_chars;
		numlines++;
	}

	linebuf_byte[numlines] = buflen;
	linebuf_char[numlines] = offset_chars;

	return true;
}


//
//	Return the number of lines
//
ULONG TextDocument::linecount()
{
	return numlines;
}

//
//	Return the length of longest line
//
ULONG TextDocument::longestline(int tabwidth)
{
	ULONG i;
	ULONG longest = 0;
	ULONG xpos = 0;
	char *bufptr = (char *)(buffer + headersize);

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

	longest = max(longest, xpos);
	return longest;
}

//
//	Return information about specified line
//
bool TextDocument::lineinfo_from_lineno(ULONG lineno, ULONG *lineoff_chars,  ULONG *linelen_chars, ULONG *lineoff_bytes, ULONG *linelen_bytes)
{
	if(lineno < numlines)
	{
		if(linelen_chars) *linelen_chars  = linebuf_char[lineno+1] - linebuf_char[lineno];
		if(lineoff_chars) *lineoff_chars  = linebuf_char[lineno];

		if(linelen_bytes) *linelen_bytes  = linebuf_byte[lineno+1] - linebuf_byte[lineno];
		if(lineoff_bytes) *lineoff_bytes  = linebuf_byte[lineno];

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
	ULONG high = numlines-1;
	ULONG line = 0;

	if(numlines == 0)
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

		if(offset_chars >= linebuf_char[line] && offset_chars < linebuf_char[line+1])
		{
			break;
		}
		else if(offset_chars < linebuf_char[line])
		{
			high = line-1;
		}
		else
		{
			low = line+1;
		}
	}

	if(lineno)			*lineno			= line;
	if(lineoff_bytes)	*lineoff_bytes	= linebuf_byte[line];
	if(linelen_bytes)	*linelen_bytes  = linebuf_byte[line+1] - linebuf_byte[line];
	if(lineoff_chars)	*lineoff_chars  = linebuf_char[line];
	if(linelen_chars)	*linelen_chars  = linebuf_char[line+1] - linebuf_char[line];

	return true;
}

int TextDocument::getformat()
{
	return fileformat;
}

ULONG TextDocument::size()
{
	return length_bytes;
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
