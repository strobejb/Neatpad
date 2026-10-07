#define STRICT
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include "TextLineMapper.h"
#include "TextView.h"

bool TextLineMapper::linecount(TextDocument *doc, ULONG *lines)
{
	if(lines == 0 || !active(doc))
		return false;

	*lines = doc->text_length() == 0 ? 0 : (ULONG)doc->m_seq.linecount();
	return true;
}

bool TextLineMapper::linecount_known(TextDocument *doc, bool *known)
{
	if(known == 0 || !active(doc))
		return false;

	*known = doc->text_length() == 0 || doc->m_seq.linecount_known();
	return true;
}

bool TextLineMapper::lineno_known(TextDocument *doc, ULONG lineno, bool *known)
{
	if(known == 0 || !active(doc))
		return false;

	*known = doc->text_length() != 0 && doc->m_seq.line_number_known(lineno);
	return true;
}

bool TextLineMapper::line_number_range_known(TextDocument *doc, ULONG offset_chars, ULONG length_chars, bool *known)
{
	size_w sequence_offset = 0;
	size_w sequence_length = 0;

	if(known == 0 || !active(doc))
		return false;

	if(!char_range_to_sequence_range(doc, offset_chars, length_chars, &sequence_offset, &sequence_length))
	{
		*known = false;
		return true;
	}

	*known = doc->m_seq.line_numbers_known(sequence_offset, sequence_length);
	return true;
}

void TextLineMapper::index_lines(TextDocument *doc, ULONG offset_chars, ULONG length_chars)
{
	size_w sequence_offset = 0;
	size_w sequence_length = 0;

	if(active(doc) && char_range_to_sequence_range(doc, offset_chars, length_chars, &sequence_offset, &sequence_length))
		doc->m_seq.index_lines(sequence_offset, sequence_length);
}

bool TextLineMapper::lineinfo_from_lineno(TextDocument *doc, ULONG lineno, LineInfo *lineinfo)
{
	size_w sequence_line_offset = 0;
	size_w sequence_next_offset = 0;

	if(lineinfo == 0 || !active(doc) || doc->text_length() == 0)
		return false;

	if(!doc->m_seq.lineoffset(lineno, &sequence_line_offset))
		return false;

	if(!doc->m_seq.next_lineoffset(sequence_line_offset, &sequence_next_offset))
		sequence_next_offset = doc->m_seq.size();

	copy_lineinfo(doc, lineno, sequence_line_offset, sequence_next_offset, lineinfo);
	return true;
}

bool TextLineMapper::lineinfo_from_offset(TextDocument *doc, ULONG offset_chars, LineInfo *lineinfo)
{
	size_w sequence_offset = 0;
	size_w sequence_line = 0;
	size_w sequence_line_offset = 0;
	size_w sequence_next_offset = 0;

	if(lineinfo == 0 || !active(doc) || doc->text_length() == 0)
		return false;

	if(!charoffset_to_sequence_offset(doc, offset_chars, &sequence_offset))
		return false;

	if(doc->m_seq.linecount_known())
	{
		if(!doc->m_seq.linefromoffset(sequence_offset, &sequence_line, &sequence_line_offset))
			return false;

		if(!doc->m_seq.next_lineoffset(sequence_line_offset, &sequence_next_offset))
			sequence_next_offset = doc->m_seq.size();

		copy_lineinfo(doc, sequence_line, sequence_line_offset, sequence_next_offset, lineinfo);
		return true;
	}

	if(!doc->m_seq.linebounds_from_offset(sequence_offset, &sequence_line_offset, &sequence_next_offset))
		return false;

	if(!doc->m_seq.linefromoffset(sequence_offset, &sequence_line, 0))
		return false;

	copy_lineinfo(doc, sequence_line, sequence_line_offset, sequence_next_offset, lineinfo);
	return true;
}

bool TextLineMapper::active(TextDocument *doc)
{
	ULONG rawLength;

	if(doc == 0)
		return false;

	rawLength = doc->m_seq.size() - doc->m_nHeaderSize;

	if(rawLength == 0)
		return true;

	return doc->m_nFileFormat == NCP_ASCII ||
		   doc->m_nFileFormat == NCP_UTF8 ||
		   doc->m_nFileFormat == NCP_UTF16 ||
		   doc->m_nFileFormat == NCP_UTF16BE;
}

bool TextLineMapper::charoffset_to_sequence_offset(TextDocument *doc, ULONG offset_chars, size_w *sequence_offset)
{
	ULONG rawLength;
	ULONG offset_bytes;

	if(sequence_offset == 0 || offset_chars > doc->text_length())
		return false;

	rawLength = doc->m_seq.size() - doc->m_nHeaderSize;

	switch(doc->m_nFileFormat)
	{
	case NCP_ASCII:
		offset_bytes = offset_chars;
		break;

	case NCP_UTF16:
	case NCP_UTF16BE:
		offset_bytes = offset_chars * sizeof(WCHAR);
		break;

	case NCP_UTF8:
		if(rawLength > MEM_BLOCK_SIZE)
			offset_bytes = offset_chars;
		else
			offset_bytes = doc->count_chars(0, offset_chars);
		break;

	default:
		return false;
	}

	if(offset_bytes > rawLength)
		offset_bytes = rawLength;

	*sequence_offset = offset_bytes + doc->m_nHeaderSize;
	return true;
}

bool TextLineMapper::char_range_to_sequence_range(TextDocument *doc, ULONG offset_chars, ULONG length_chars, size_w *sequence_offset, size_w *sequence_length)
{
	size_w sequence_start = 0;
	size_w sequence_end = 0;
	ULONG end_chars;

	if(sequence_offset == 0 || sequence_length == 0)
		return false;

	if(length_chars > doc->text_length() || offset_chars > doc->text_length() - length_chars)
		return false;

	end_chars = offset_chars + length_chars;

	if(!charoffset_to_sequence_offset(doc, offset_chars, &sequence_start))
		return false;

	if(!charoffset_to_sequence_offset(doc, end_chars, &sequence_end))
		return false;

	*sequence_offset = sequence_start;
	*sequence_length = sequence_end >= sequence_start ? sequence_end - sequence_start : 0;

	return true;
}

ULONG TextLineMapper::byteoffset_to_charoffset(TextDocument *doc, ULONG offset_bytes)
{
	ULONG rawLength = doc->m_seq.size() - doc->m_nHeaderSize;

	switch(doc->m_nFileFormat)
	{
	case NCP_ASCII:
		return offset_bytes;

	case NCP_UTF16:
	case NCP_UTF16BE:
		return offset_bytes / sizeof(WCHAR);

	case NCP_UTF8:
		if(rawLength > MEM_BLOCK_SIZE)
			return offset_bytes;

		return doc->count_code_units(0, offset_bytes);

	default:
		break;
	}

	return doc->byteoffset_to_charoffset(offset_bytes);
}

void TextLineMapper::copy_lineinfo(TextDocument *doc, size_w lineno, size_w sequence_line_offset, size_w sequence_next_offset, LineInfo *lineinfo)
{
	ULONG lineoff_bytes;
	ULONG nextoff_bytes;

	lineoff_bytes = sequence_line_offset <= (size_w)doc->m_nHeaderSize ? 0 : (ULONG)(sequence_line_offset - doc->m_nHeaderSize);
	nextoff_bytes = sequence_next_offset <= (size_w)doc->m_nHeaderSize ? 0 : (ULONG)(sequence_next_offset - doc->m_nHeaderSize);

	lineinfo->lineno = (ULONG)lineno;
	lineinfo->lineoff_bytes = lineoff_bytes;
	lineinfo->linelen_bytes = nextoff_bytes >= lineoff_bytes ? nextoff_bytes - lineoff_bytes : 0;
	lineinfo->lineoff_chars = byteoffset_to_charoffset(doc, lineoff_bytes);
	lineinfo->linelen_chars = byteoffset_to_charoffset(doc, nextoff_bytes) - lineinfo->lineoff_chars;
	lineinfo->chars_known = doc->m_nFileFormat != NCP_UTF8 || (doc->m_seq.size() - doc->m_nHeaderSize) <= MEM_BLOCK_SIZE;
}
