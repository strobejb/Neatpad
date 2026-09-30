#ifndef UNICODE_LIB_INCLUDED
#define UNICODE_LIB_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

// Define the basic types for storing Unicode 
typedef unsigned long	UTF32;	
typedef unsigned short	UTF16;	
typedef unsigned char	UTF8;	

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
size_t	utf8_to_utf32(UTF8 *utf8str, size_t utf8len, UTF32 *pcp32);
size_t  utf32_to_utf8(UTF8 *utf8str, size_t utf8len, UTF32 ch32);

//
//	Conversions between UTF-16 and UTF-8 strings
//
size_t	utf8_to_utf16(UTF8 *utf8str,   size_t utf8len, UTF16 *utf16str, size_t *utf16len);
size_t  utf16_to_utf8(UTF16 *utf16str, size_t utf16len, UTF8 *utf8str,  size_t *utf8len);

//
//	Conversions between UTF-16 and UTF-32 strings
//
size_t  utf16_to_utf32(UTF16 *utf16str,   size_t utf16len, UTF32 *utf32str, size_t *utf32len);
size_t  utf32_to_utf16(UTF32 *utf32str,   size_t utf32len, UTF16 *utf16str, size_t *utf16len);
size_t	utf16be_to_utf32(UTF16 *utf16str, size_t utf16len, UTF32 *utf32str, size_t *utf32len);


//
//	Conversions between ASCII/ANSI and UTF-16 strings
//
size_t	ascii_to_utf16(UTF8 *asciistr,  size_t asciilen, UTF16 *utf16str, size_t *utf16len);
size_t  utf16_to_ascii(UTF16 *utf16str, size_t utf16len, UTF8  *asciistr, size_t *asciilen);

//
//	Miscallaneaous
//
size_t	copy_utf16(UTF16 *src, size_t srclen, UTF16 *dest, size_t *destlen);
size_t	swap_utf16(UTF16 *src, size_t srclen, UTF16 *dest, size_t *destlen);



#ifdef __cplusplus
}
#endif

#endif