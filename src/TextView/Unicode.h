#ifndef UNICODE_LIB_INCLUDED
#define UNICODE_LIB_INCLUDED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Define the basic types for storing Unicode: the same widths on every platform
typedef uint32_t		UTF32;
typedef uint16_t		UTF16;
typedef uint8_t			UTF8;

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

#define SWAPWORD(val) (((UTF16)(val) << 8) | ((UTF16)(val) >> 8))

//
//	Conversions between UTF-8 and a single UTF-32 value
//
size_t	utf8_to_utf32          (UTF8 *utf8str,  size_t utf8len,  UTF32 *pcp32);
size_t  utf32_to_utf8          (UTF8 *utf8str,  size_t utf8len,  UTF32  ch32);
size_t	utf16_to_utf32_char    (UTF8 *utf16str, size_t utf16len, UTF32 *pch32);
size_t	utf16be_to_utf32_char  (UTF8 *utf16str, size_t utf16len, UTF32 *pch32);
size_t	utf32_to_utf32_char    (UTF8 *utf32str, size_t utf32len, UTF32 *pch32);
size_t	utf32be_to_utf32_char  (UTF8 *utf32str, size_t utf32len, UTF32 *pch32);

//
//	As above, but for the single codepoint that ends the stream of text
//
size_t	utf8_to_utf32_prev    (UTF8 *utf8str,  size_t utf8len,  UTF32 *pch32);
size_t	utf16_to_utf32_prev   (UTF8 *utf16str, size_t utf16len, UTF32 *pch32);
size_t	utf16be_to_utf32_prev (UTF8 *utf16str, size_t utf16len, UTF32 *pch32);

//
//	Length of a UTF-8 stream up to the end of its last whole sequence
//
size_t	utf8_complete_length (UTF8 *utf8str, size_t utf8len);

//
//	Conversions between UTF-16 and UTF-8 strings
//
size_t	utf8_to_utf16 (UTF8  *utf8str,  size_t utf8len, UTF16 *utf16str, size_t *utf16len);
size_t  utf16_to_utf8 (UTF16 *utf16str, size_t utf16len, UTF8 *utf8str,  size_t *utf8len);

//
//	Conversions between UTF-16 and UTF-32 strings
//
size_t  utf16_to_utf32   (UTF16 *utf16str, size_t utf16len, UTF32 *utf32str, size_t *utf32len);
size_t  utf32_to_utf16   (UTF32 *utf32str, size_t utf32len, UTF16 *utf16str, size_t *utf16len);
size_t	utf16be_to_utf32 (UTF16 *utf16str, size_t utf16len, UTF32 *utf32str, size_t *utf32len);

//
//	Conversions between UTF-16 strings and UTF-32 byte streams, in either byte order
//
size_t	utf32le_to_utf16 (UTF8 *utf32str,  size_t utf32len, UTF16 *utf16str, size_t *utf16len);
size_t	utf32be_to_utf16 (UTF8 *utf32str,  size_t utf32len, UTF16 *utf16str, size_t *utf16len);
size_t	utf16_to_utf32le (UTF16 *utf16str, size_t utf16len,  UTF8 *utf32str, size_t *utf32len);
size_t	utf16_to_utf32be (UTF16 *utf16str, size_t utf16len,  UTF8 *utf32str, size_t *utf32len);


//
//	Conversions between ASCII/ANSI and UTF-16 strings. These use the Windows
//	ANSI code page, so for now they only exist on Windows.
//
size_t	ascii_to_utf16 (UTF8 *asciistr,  size_t asciilen, UTF16 *utf16str, size_t *utf16len);
size_t  utf16_to_ascii (UTF16 *utf16str, size_t utf16len, UTF8  *asciistr, size_t *asciilen);

//
//	Compares two UTF-16 strings of the same length, ignoring case. This uses
//	the Windows invariant locale, so for now it only exists on Windows.
//
bool	utf16_equal_nocase(const UTF16 *str1, const UTF16 *str2, size_t len);

//
//	Miscallaneaous
//
size_t	copy_utf16 (UTF16 *src, size_t srclen, UTF16 *dest, size_t *destlen);
size_t	swap_utf16 (UTF16 *src, size_t srclen, UTF16 *dest, size_t *destlen);



#ifdef __cplusplus
}
#endif

#endif
