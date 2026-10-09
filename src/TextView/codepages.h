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

//
//	Kinds of line break, as bits: one is a line format, several are the
//	breaks a document has been seen to use
//
#define TXL_LF				1		// line-feed
#define TXL_CR				2		// carriage-return
#define TXL_CRLF			4		// carriage-return, line-feed (default)
#define TXL_ALL				7		// allow all forms regardless

#endif
