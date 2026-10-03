#ifndef CODEPAGES_INCLUDED
#define CODEPAGES_INCLUDED

//
//	Text encodings supported by TextDocument.
//
typedef enum TEXT_ENCODING
{
	NCP_ASCII = 0,
	NCP_UTF8,
	NCP_UTF16,
	NCP_UTF16BE,
	NCP_UTF32,
	NCP_UTF32BE
} TEXT_ENCODING;

typedef struct BOM_LOOKUP
{
	DWORD		  bom;
	ULONG		  len;
	TEXT_ENCODING encoding;
} BOM_LOOKUP;

typedef struct TEXT_ENCODING_INFO
{
	TEXT_ENCODING encoding;
	ULONG		  code_unit_bits;
	BOOL		  fixed_width;

	// TextDocument character offsets are UTF-16 code units.
	BOOL		  byte_offset_equals_char_offset;
} TEXT_ENCODING_INFO;

#endif
