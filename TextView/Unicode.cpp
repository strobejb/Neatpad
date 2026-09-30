//
//	Unicode conversion routines
//
//	www.catch22.net
//	
#include <windows.h>

typedef unsigned long	UTF32;	// at least 32 bits
typedef unsigned short	UTF16;	// at least 16 bits
typedef unsigned char	UTF8;	// typically 8 bits

// Some fundamental constants 
#define UNI_REPLACEMENT_CHAR (UTF32)0x0000FFFD
#define UNI_MAX_BMP			 (UTF32)0x0000FFFF
#define UNI_MAX_UTF16		 (UTF32)0x0010FFFF
#define UNI_MAX_UTF32		 (UTF32)0x7FFFFFFF
#define UNI_MAX_LEGAL_UTF32  (UTF32)0x0010FFFF

#define UNI_SUR_HIGH_START   (UTF32)0xD800
#define UNI_SUR_HIGH_END     (UTF32)0xDBFF
#define UNI_SUR_LOW_START    (UTF32)0xDC00
#define UNI_SUR_LOW_END      (UTF32)0xDFFF

#define SWAPWORD(val) (((WORD)(val) << 8) | ((WORD)(val) >> 8))

//
//	Convert the specified UTF-8 stream of text to a single UTF-32
//
//	Illegal sequences are converted to the unicode replacement character
//	
//	Returns number of bytes processed from utf8str
//  UTF-32 value stored in pch32
//
size_t utf8_to_utf32(BYTE *utf8str, size_t utf8len, ULONG *pch32)
{
	BYTE   ch       = *utf8str++;
	ULONG  val32    = 0;	
	size_t trailing = 0;
	size_t len      = 1;
	size_t i;
	static ULONG nonshortest[] = { 0, 0x80, 0x800, 0x10000, 0xffffffff, 0xffffffff };

	// validate parameters
	if(utf8str == 0 || utf8len <= 0 || pch32 == 0)
		return 0;

	// look for plain ASCII first as this is most likely
	if(ch < 0x80)
	{
		*pch32 = (ULONG)ch;
		return 1;
	}
	// LEAD-byte of 2-byte seq: 110xxxxx 10xxxxxx
	else if((ch & 0xE0) == 0xC0)			
	{
		trailing = 1;
		val32    = ch & 0x1F;
	}
	// LEAD-byte of 3-byte seq: 1110xxxx 10xxxxxx 10xxxxxx
	else if((ch & 0xF0) == 0xE0)	
	{
		trailing = 2;
		val32    = ch & 0x0F;
	}
	// LEAD-byte of 4-byte seq: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
	else if((ch & 0xF8) == 0xF0)	
	{
		trailing = 3;
		val32    = ch & 0x07;
	}
	// ILLEGAL 5-byte seq: 111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
	else if((ch & 0xFC) == 0xF8)	
	{
		// range-checking the UTF32 result will catch this
		trailing = 4;
		val32    = ch & 0x03;
	}
	// ILLEGAL 6-byte seq: 1111110x 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
	else if((ch & 0xFE) == 0xFC)	
	{
		// range-checking the UTF32 result will catch this
		trailing = 5;
		val32    = ch & 0x01;
	}
	// ILLEGAL continuation (trailing) byte by itself
	else if((ch & 0xC0) == 0x80)
	{
		*pch32 = UNI_REPLACEMENT_CHAR;
		return 1;
	}
	// any other ILLEGAL form.
	else							
	{
		*pch32 = UNI_REPLACEMENT_CHAR;
		return 1;
	}

	// process trailing bytes
	for(i = 0; i < trailing && len < utf8len; i++)
	{
		ch = *utf8str++;

		// Valid trail-byte: 10xxxxxx
		if((ch & 0xC0) == 0x80)
		{
			val32 = (val32 << 6) + (ch & 0x7f);
			len++;
		}
		// Anything else is an error
		else
		{
			*pch32 = UNI_REPLACEMENT_CHAR;
			return len;
		}
	}

	// did we 
	if(val32 < nonshortest[trailing] || i != trailing)
		*pch32 = UNI_REPLACEMENT_CHAR;
	else
		*pch32 = val32;

	return len;
}

int ascii_to_utf16(BYTE *asciistr, size_t asciilen, wchar_t *utf16str, size_t *utf16len)
{
	int len = min(*utf16len, asciilen);
		
	MultiByteToWideChar(CP_ACP, 0, (CCHAR*)asciistr, len, utf16str, len);
	*utf16len = len;
	return len;
}

//
//	Return number of bytes processed
//
int copy_utf16(wchar_t *src, size_t srclen, wchar_t *dest, size_t *destlen)
{
	size_t len = min(*destlen, srclen);
	memcpy(dest, src, len * sizeof(WCHAR));

	*destlen = len;
	return len * sizeof(WCHAR);
}

int swap_utf16(wchar_t *src, size_t srclen, wchar_t *dest, size_t *destlen)
{
	size_t len = min(*destlen, srclen);
	
	for(size_t i = 0; i < len; i++)
		dest[i] = SWAPWORD(src[i]);

	*destlen = len;
	return len * sizeof(WCHAR);
}

//
//	Convert the specified UTF-8 stream of text to UTF-16
//
//	1. The maximum number of UTF-16 characters are stored in wstr
//	2. Illegal sequences are converted to the unicode replacement character
//	3. Returns the number of bytes processeed from utf8str
//
//	wlen - on input, specifies the size (in WCHARs) or wstr
//		 - on output, returns the actual number of UTF-16 codepoints stored
//
int utf8_to_utf16(BYTE *utf8str, size_t utf8len, wchar_t *utf16str, size_t *utf16len)
{
	int   len;
	ULONG ch32;
	WCHAR *utf16start = utf16str;
	BYTE  *utf8start  = utf8str;

	while(utf8len > 0 && *utf16len > 0)
	{
		len = utf8_to_utf32(utf8str, utf8len, &ch32);

		// target is a character <= 0xffff
		if(ch32 < 0xfffe)
		{
			// make sure we don't represent anything in UTF16 surrogate range
			// (this helps protect against non-shortest forms)
			if(ch32 >= UNI_SUR_HIGH_START && ch32 <= UNI_SUR_LOW_END)
			{
				*utf16str++ = UNI_REPLACEMENT_CHAR;
				(*utf16len)--;
			}
			else
			{
				*utf16str++ = (WORD)ch32;
				(*utf16len)--;
			}
		}
		// FFFE and FFFF are illegal mid-stream
		else if(ch32 == 0xfffe || ch32 == 0xffff)
		{
			*utf16str++ = UNI_REPLACEMENT_CHAR;
			(*utf16len)--;
		}
		// target is illegal Unicode value
		else if(ch32 > UNI_MAX_UTF16)
		{
			*utf16str++ = UNI_REPLACEMENT_CHAR;
			(*utf16len)--;
		}
		// target is in range 0xffff - 0x10ffff
		else if(*utf16len >= 2)
		{ 
			ch32 -= 0x0010000;

			*utf16str++ = (WORD)((ch32 >> 10)   + UNI_SUR_HIGH_START);
			*utf16str++ = (WORD)((ch32 & 0x3ff) + UNI_SUR_LOW_START);

			(*utf16len)-=2;
		}
		else
		{
			// no room to store result
			break;
		}

		utf8str   += len;
		utf8len   -= len;
	}

	*utf16len = utf16str - utf16start;

	return utf8str - utf8start;
}

//
//	Convert the specified UTF-16 stream of text to UTF-32
//
//
//	utf16len - size in WCHARs of UTF-16 string
//
//	returns number of WCHARs processed
//
int utf16_to_utf32(WCHAR *utf16str, size_t utf16len, ULONG *utf32str, size_t *utf32len)
{
	WCHAR *utf16start = utf16str;
	ULONG *utf32start = utf32str;

	while(utf16len > 0 && *utf32len > 0)
	{
		ULONG ch = *utf16str;

		// first of a surrogate pair?
		if(ch >= UNI_SUR_HIGH_START && ch < UNI_SUR_HIGH_END && utf16len >= 2)
		{
			ULONG ch2 = *(utf16str + 1);
			
			// valid trailing surrogate unit?
			if(ch2 >= UNI_SUR_LOW_START && ch < UNI_SUR_LOW_END)
			{
				ch = ((ch  - UNI_SUR_HIGH_START) << 10) + 
					 ((ch2 - UNI_SUR_LOW_START) + 0x00010000);

				utf16str++;
				utf16len--;
			}
			// illegal character
			else
			{
				ch = UNI_REPLACEMENT_CHAR;
			}
		}

		*utf32str++ = ch;
		(*utf32len)--;		
		
		utf16str++;
		utf16len--;
	}

	*utf32len = utf32str - utf32start;
	return utf16str - utf16start;
}

int utf16be_to_utf32(WCHAR *utf16str, size_t utf16len, ULONG *utf32str, size_t *utf32len)
{
	WCHAR *utf16start = utf16str;
	ULONG *utf32start = utf32str;

	while(utf16len > 0 && *utf32len > 0)
	{
		ULONG ch = SWAPWORD(*utf16str);

		// first of a surrogate pair?
		if(ch >= UNI_SUR_HIGH_START && ch < UNI_SUR_HIGH_END && utf16len >= 2)
		{
			ULONG ch2 = SWAPWORD(*(utf16str + 1));
			
			// valid trailing surrogate unit?
			if(ch2 >= UNI_SUR_LOW_START && ch < UNI_SUR_LOW_END)
			{
				ch = ((ch  - UNI_SUR_HIGH_START) << 10) + 
					 ((ch2 - UNI_SUR_LOW_START) + 0x00010000);

				utf16str++;
				utf16len--;
			}
			// illegal character
			else
			{
				ch = UNI_REPLACEMENT_CHAR;
			}
		}

		*utf32str++ = ch;
		(*utf32len)--;
		
		utf16str++;
		utf16len--;
	}

	*utf32len = utf32str - utf32start;
	return utf16str - utf16start;
}