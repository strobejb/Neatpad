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
//	The answer from a lookup that needs the document's index: only the indexed
//	part of a document, from the top, has exact line numbers
//
typedef enum TEXT_RESULT
{
	TDR_OK = 0,					// answered exactly
	TDR_PENDING_INDEX,			// past the indexed part: index further, then ask again
	TDR_FAILED					// no answer: one the document doesn't have, or can't give
} TEXT_RESULT;

//
//	Kinds of line break, as bits: one is a line format, several are the
//	breaks a document has been seen to use
//
#define TXL_LF				1		// line-feed
#define TXL_CR				2		// carriage-return
#define TXL_CRLF			4		// carriage-return, line-feed (default)
#define TXL_ALL				7		// allow all forms regardless

#endif
