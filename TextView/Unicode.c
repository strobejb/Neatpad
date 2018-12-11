//
//	UNICODE.C
//
//	Unicode conversion routines
//
//	www.catch22.net
//	Written by J Brown 2006
//
//	Freeware
//	

#define STRICT
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include "Unicode.h"

//
//	utf8_to_utf32
//
//	Converts a single codepoint in the specified UTF-8 stream of text
//	into a UTF-32 value
//
//	Illegal sequences are converted to the unicode replacement character
//	
//	utf8str		- [in]   buffer containing UTF-8 text
//	utf8len		- [in]   number of code-units (bytes) available in buffer
//	pch32		- [out]  single UTF-32 value
//
//	Returns number of bytes processed from utf8str
//
size_t utf8_to_utf32(UTF8 *utf8str, size_t utf8len, UTF32 *pch32)
{
	UTF8   ch       = *utf8str++;
	UTF32  val32    = 0;	
	size_t trailing = 0;
	size_t len      = 1;
	size_t i;
	
	static UTF32 nonshortest[] = 
	{ 
		0, 0x80, 0x800, 0x10000, 0xffffffff, 0xffffffff 
	};

	// validate parameters
	if(utf8str == 0 || utf8len <= 0 || pch32 == 0)
		return 0;

	// look for plain ASCII first as this is most likely
	if(ch < 0x80)
	{
		*pch32 = (UTF32)ch;
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

	// did we decode a full utf-8 sequence?
	if(val32 < nonshortest[trailing] || i != trailing)
		*pch32 = UNI_REPLACEMENT_CHAR;
	else
		*pch32 = val32;

	return len;
}

//
//	utf32_to_utf8
//
//	Converts the specified UTF-32 value to UTF-8
//
//	ch32		- [in]		single utf-32 value
//	utf8str		- [out]		buffer to receive UTF-8 text
//	utf8len		- [in]		size of utf8 buffer in bytes
//	
//	Returns number of bytes stored in utf8str
//
size_t utf32_to_utf8(UTF8 *utf8str, size_t utf8len, UTF32 ch32)
{
	size_t len = 0;

	// validate parameters
	if(utf8str == 0 || utf8len == 0)
		return 0;

	// ASCII is the easiest
	if(ch32 < 0x80)
	{
		*utf8str = (UTF8)ch32;
		return 1;
	}

	// make sure we have a legal utf32 char
	if(ch32 > UNI_MAX_LEGAL_UTF32)
		ch32 = UNI_REPLACEMENT_CHAR;

	// cannot encode the surrogate range
	if(ch32 >= UNI_SUR_HIGH_START && ch32 <= UNI_SUR_LOW_END)
		ch32 = UNI_REPLACEMENT_CHAR;

	// 2-byte sequence
	if(ch32 < 0x800 && utf8len >= 2)
	{
		*utf8str++ = (UTF8)((ch32 >> 6)			| 0xC0);
		*utf8str++ = (UTF8)((ch32 & 0x3f)		| 0x80);
		len = 2;
	}
	// 3-byte sequence
	else if(ch32 < 0x10000 && utf8len >= 3)
	{
		*utf8str++ = (UTF8)((ch32 >> 12)        | 0xE0);
		*utf8str++ = (UTF8)((ch32 >> 6) & 0x3f  | 0x80);
		*utf8str++ = (UTF8)((ch32 & 0x3f)       | 0x80);
		len = 3;
	}
	// 4-byte sequence
	else if(ch32 <= UNI_MAX_LEGAL_UTF32 && utf8len >= 4)
	{
		*utf8str++ = (UTF8)((ch32 >> 18)        | 0xF0);
		*utf8str++ = (UTF8)((ch32 >> 12) & 0x3f | 0x80);
		*utf8str++ = (UTF8)((ch32 >> 6) & 0x3f  | 0x80);
		*utf8str++ = (UTF8)((ch32 & 0x3f)       | 0x80);
		len = 4;
	}

	// 5/6 byte sequences never occur because we limit using UNI_MAX_LEGAL_UTF32

	return len;
}

//
//	utf8_to_utf16
//
//	Convert the specified UTF-8 stream of text to UTF-16
//
//	1. The maximum number possible of whole UTF-16 characters are stored in wstr
//	2. Illegal sequences are converted to the unicode replacement character
//	3. Returns the number of bytes processeed from utf8str
//
//	utf8str		- [in]		buffer containing utf-8 text
//	utf8len		- [in]		number of code-units (bytes) in buffer
//	utf16str	- [out]		receives resulting utf-16 text
//	utf16len	- [in/out]	on input, specifies the size (in UTF16s) of utf16str
//							on output, holds actual number of UTF16s stored in utf16str
//
//	Returns the number of bytes processed from utf8str
//
size_t utf8_to_utf16(UTF8 *utf8str, size_t utf8len, UTF16 *utf16str, size_t *utf16len)
{
	UTF16 *utf16start = utf16str;
	UTF8  *utf8start  = utf8str;

	size_t len;
	size_t tmp16len;
	UTF32  ch32;

	while(utf8len > 0 && *utf16len > 0)
	{
		// convert to utf-32
		len		     = utf8_to_utf32(utf8str, utf8len, &ch32);
		utf8str     += len;
		utf8len     -= len;

		// convert to utf-16
		tmp16len     = *utf16len;
		len          = utf32_to_utf16(&ch32, 1, utf16str, &tmp16len);
		utf16str    += len;
		(*utf16len) -= len;
	}

	*utf16len = utf16str - utf16start;
	return utf8str - utf8start;
}

//
//	utf16_to_utf8
//
//	Convert the specified UTF-16 stream of text to UTF-8
//
//	1. As many whole codepoints as possible are stored in utf8str 
//	2. Illegal sequences are converted to the unicode replacement character
//
//	utf16str		- [in]		buffer containing utf-16 text
//	utf16len		- [in]		number of code-units (UTF16s) in buffer
//	utf8str			- [out]		receives resulting utf-8 text
//	utf8len			- [in/out]	on input, specifies the size (in bytes) of utf8str
//								on output, holds actual number of bytes stored in utf8str
//
//	Returns the number of characters (UTF16s) processed from utf16str
//
size_t utf16_to_utf8(UTF16 *utf16str, size_t utf16len, UTF8 *utf8str, size_t *utf8len)
{
	UTF16 * utf16start = utf16str;
	UTF8  * utf8start  = utf8str;
	size_t  len;
	UTF32	ch32;
	size_t	ch32len;

	while(utf16len > 0 && *utf8len > 0)
	{
		// convert to utf-32
		ch32len	    = 1;
		len		    = utf16_to_utf32(utf16str, utf16len, &ch32, &ch32len);
		utf16str   += len;
		utf16len   -= len;

		// convert to utf-8
		len		    = utf32_to_utf8(utf8str, *utf8len, ch32);
		utf8str    += len;
		(*utf8len) -= len;
	}

	*utf8len = utf8str - utf8start;
	return utf16str - utf16start;
}

//
//	ascii_to_utf16
//
//	Converts plain ASCII string to UTF-16
//
//	asciistr	- [in]     buffer containing ASCII characters
//	asciilen	- [in]     number of characters in buffer
//	utf16str	- [out]    receives the resulting UTF-16 text
//	utf16len	- [in/out] on input, specifies length of utf16 buffer,
//						   on output, holds number of chars stored in utf16str
//
//	Returns number of characters processed from asciistr
//
size_t ascii_to_utf16(UTF8 *asciistr, size_t asciilen, UTF16 *utf16str, size_t *utf16len)
{
	size_t len = min(*utf16len, asciilen);
		
	MultiByteToWideChar(CP_ACP, 0, (CCHAR*)asciistr, len, (WCHAR *)utf16str, len);
	*utf16len = len;
	return len;
}

//
//	utf16_to_ascii
//
//	Converts UTF-16 to plain ASCII (lossy)
//
//	utf16str	- [in]     buffer containing UTF16 characters
//	utf16len	- [in]     number of WCHARs in buffer
//	asciistr	- [out]    receives the resulting UTF-16 text
//	asciilen	- [in/out] on input, specifies length of ascii buffer,
//						   on output, holds number of chars stored in asciistr
//
//	Returns number of characters processed from utf16str
//
size_t utf16_to_ascii(UTF16 *utf16str, size_t utf16len, UTF8 *asciistr, size_t *asciilen)
{
	size_t len = min(utf16len, *asciilen);
	
	WideCharToMultiByte(CP_ACP, 0, utf16str, len, asciistr, *asciilen, 0, 0);
	*asciilen = len;
	return len;
}

//
//	copy_utf16
//
//	Copies UTF-16 string from src to dest
//
//	src			- [in]		buffer containing utf-16 text
//	srclen		- [in]		number of code-units in src
//	dest		- [out]		receives resulting string
//	destlen		- [in/out]	on input, specifies length of dest buffer
//							on output, holds number of UTF16s stored in dest
//
//	returns number of WCHARs processed from src
//
size_t copy_utf16(UTF16 *src, size_t srclen, UTF16 *dest, size_t *destlen)
{
	size_t len = min(*destlen, srclen);
	memcpy(dest, src, len * sizeof(UTF16));

	*destlen = len;
	return len;
}

//
//	swap_utf16
//
//	Copies UTF-16 string from src to dest, performing endianess swap
//	for each code-unit
//
//	src			- [in]		buffer containing utf-16 text
//	srclen		- [in]		number of code-units in src
//	dest		- [out]		receives resulting word-swapped string
//	destlen		- [in/out]	on input, specifies length of dest buffer
//							on output, holds number of UTF16s stored in dest
//
//	Returns number of WCHARs processed from src
//
size_t swap_utf16(UTF16 *src, size_t srclen, UTF16 *dest, size_t *destlen)
{
	size_t len = min(*destlen, srclen);
	size_t i;
	
	for(i = 0; i < len; i++)
		dest[i] = SWAPWORD(src[i]);

	*destlen = len;
	return len;
}

//
//	utf32_to_utf16
//
//	Converts the specified UTF-32 stream of text to UTF-16
//
//	utf32str	- [in]		buffer containing utf-32 text
//	utf32len	- [in]		number of characters (UTF32s) in utf32str
//	utf16str	- [out]		receives resulting utf-16 text
//	utf16len	- [in/out]	on input, specifies the size (in UTF16s) of utf16str
//							on output, holds actual number of UTF16 values stored in utf16str
//
//	returns number of UTF32s processed from utf32str
//
size_t utf32_to_utf16(UTF32 *utf32str, size_t utf32len, UTF16 *utf16str, size_t *utf16len)
{
	UTF16 *utf16start = utf16str;
	UTF32 *utf32start = utf32str;

	while(utf32len > 0 && *utf16len > 0)
	{
		UTF32 ch32 = *utf32str++;
		utf32len--;

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
	}

	*utf16len = utf16str - utf16start;
	return utf32str - utf32start;
}

//
//	utf16_to_utf32
//
//	Converts the specified UTF-16 stream of text to UTF-32
//
//	utf16str	- [in]		buffer containing utf-16 text
//	utf16len	- [in]		number of code-units (UTF16s) in utf16str
//	utf32str	- [out]		receives resulting utf-32 text
//	utf32len	- [in/out]	on input, specifies the size (in UTF32s) of utf32str
//							on output, holds actual number of UTF32 values stored in utf32str
//
//	returns number of UTF16s processed from utf16str
//
size_t utf16_to_utf32(UTF16 *utf16str, size_t utf16len, UTF32 *utf32str, size_t *utf32len)
{
	UTF16 *utf16start = utf16str;
	UTF32 *utf32start = utf32str;

	while(utf16len > 0 && *utf32len > 0)
	{
		UTF32 ch = *utf16str;

		// first of a surrogate pair?
		if(ch >= UNI_SUR_HIGH_START && ch <= UNI_SUR_HIGH_END && utf16len >= 2)
		{
			// get the second half of the pair
			UTF32 ch2 = *(utf16str + 1);
			
			// valid trailing surrogate unit?
			if(ch2 >= UNI_SUR_LOW_START && ch2 <= UNI_SUR_LOW_END)
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

//
//	utf16be_to_utf32
//
//	Converts the specified big-endian UTF-16 stream of text to UTF-32
//
//	utf16str	- [in]		buffer containing utf-16 big-endian text
//	utf16len	- [in]		number of code-units (UTF16s) in utf16str
//	utf32str	- [out]		receives resulting utf-32 text
//	utf32len	- [in/out]	on input, specifies the size (in UTF32s) of utf32str
//							on output, holds actual number of UTF32 values stored in utf32str
//
//	returns number of UTF16s processed from utf16str
//
size_t utf16be_to_utf32(UTF16 *utf16str, size_t utf16len, UTF32 *utf32str, size_t *utf32len)
{
	UTF16 *utf16start = utf16str;
	UTF32 *utf32start = utf32str;

	while(utf16len > 0 && *utf32len > 0)
	{
		UTF32 ch = SWAPWORD(*utf16str);

		// first of a surrogate pair?
		if(ch >= UNI_SUR_HIGH_START && ch <= UNI_SUR_HIGH_END && utf16len >= 2)
		{
			UTF32 ch2 = SWAPWORD(*(utf16str + 1));
			
			// valid trailing surrogate unit?
			if(ch2 >= UNI_SUR_LOW_START && ch2 <= UNI_SUR_LOW_END)
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