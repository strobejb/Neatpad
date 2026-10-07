#pragma once

#ifndef NEATPAD_TEXTLINEMAPPER_INCLUDED
#define NEATPAD_TEXTLINEMAPPER_INCLUDED

#include "TextDocument.h"

//
//	TextLineMapper translates sequence-owned raw line facts into TextDocument's
//	UTF-16 line coordinate system. It is stateless; TextDocument remains the
//	owner of encoding, BOM/header, and document-length policy.
//
class TextLineMapper
{
public:
	struct LineInfo
	{
		ULONG lineno;
		ULONG lineoff_chars;
		ULONG linelen_chars;
		ULONG lineoff_bytes;
		ULONG linelen_bytes;
		bool  chars_known;
	};

	static bool linecount(TextDocument *doc, ULONG *lines);
	static bool linecount_known(TextDocument *doc, bool *known);
	static bool lineno_known(TextDocument *doc, ULONG lineno, bool *known);
	static bool line_number_range_known(TextDocument *doc, ULONG offset_chars, ULONG length_chars, bool *known);
	static void index_lines(TextDocument *doc, ULONG offset_chars, ULONG length_chars);

	static bool lineinfo_from_lineno(TextDocument *doc, ULONG lineno, LineInfo *lineinfo);
	static bool lineinfo_from_offset(TextDocument *doc, ULONG offset_chars, LineInfo *lineinfo);
	static ULONG byteoffset_to_charoffset(TextDocument *doc, ULONG offset_bytes);

private:
	static bool active(TextDocument *doc);
	static bool charoffset_to_sequence_offset(TextDocument *doc, ULONG offset_chars, size_w *sequence_offset);
	static bool char_range_to_sequence_range(TextDocument *doc, ULONG offset_chars, ULONG length_chars, size_w *sequence_offset, size_w *sequence_length);
	static void copy_lineinfo(TextDocument *doc, size_w lineno, size_w sequence_line_offset, size_w sequence_next_offset, LineInfo *lineinfo);
};

#endif
