#ifndef TEXTDOC_INCLUDED
#define TEXTDOC_INCLUDED

#include "codepages.h"

class TextIterator;

class TextDocument
{
	friend class TextIterator;

public:
	TextDocument();
	~TextDocument();

	bool  init(HANDLE hFile);
	bool  init(TCHAR *filename);
	
	bool  clear();

	ULONG lineno_from_offset(ULONG offset);

	bool  lineinfo_from_offset(ULONG offset_chars, ULONG *lineno, ULONG *lineoff_chars,  ULONG *linelen_chars, ULONG *lineoff_bytes, ULONG *linelen_bytes);
	bool  lineinfo_from_lineno(ULONG lineno,                      ULONG *lineoff_chars,  ULONG *linelen_chars, ULONG *lineoff_bytes, ULONG *linelen_bytes);	

	TextIterator iterate_line(ULONG lineno, ULONG *linestart = 0, ULONG *linelen = 0);
	TextIterator iterate_line_offset(ULONG offset_chars, ULONG *lineno, ULONG *linestart = 0);

	ULONG getdata(ULONG offset, BYTE *buf, size_t len);

	int   getformat();
	ULONG linecount();
	ULONG longestline(int tabwidth);
	ULONG size();

private:
	
	bool init_linebuffer();

	int   detect_file_format(int *headersize);
	int	  gettext(ULONG offset, ULONG lenbytes, TCHAR *buf, int *len);
	int   getchar(ULONG offset, ULONG lenbytes, ULONG *pch32);

	char *buffer;
	ULONG length_chars;
	ULONG length_bytes;

	ULONG *linebuf_byte;
	ULONG *linebuf_char;
	ULONG  numlines;
	
	int	   fileformat;
	int    headersize;
};

class TextIterator
{
public:
	// default constructor sets all members to zero
	TextIterator()
		: text_doc(0), off_bytes(0), len_bytes(0)
	{
	}

	TextIterator(ULONG off, ULONG len, TextDocument *td)
		: text_doc(td), off_bytes(off), len_bytes(len)
	{
		
	}

	// default copy-constructor
	TextIterator(const TextIterator &ti) 
		: text_doc(ti.text_doc), off_bytes(ti.off_bytes), len_bytes(ti.len_bytes)
	{
	}

	// assignment operator
	TextIterator & operator= (TextIterator &ti)
	{
		text_doc  = ti.text_doc;
		off_bytes = ti.off_bytes;
		len_bytes = ti.len_bytes;
		return *this;
	}

	int gettext(TCHAR *buf, int buflen)
	{
		if(text_doc)
		{
			// get text from the TextDocument at the specified byte-offset
			int len = text_doc->gettext(off_bytes, len_bytes, buf, &buflen);

			// adjust the iterator's internal position
			off_bytes += len;
			len_bytes -= len;

			return buflen;
		}
		else
		{
			return 0;
		}
	}

	operator bool()
	{
		return text_doc ? true : false;
	}

private:

	TextDocument *text_doc;
	
	ULONG off_bytes;
	ULONG len_bytes;
};


#endif