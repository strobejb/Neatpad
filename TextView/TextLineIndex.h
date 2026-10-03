#ifndef TEXTLINEINDEX_INCLUDED
#define TEXTLINEINDEX_INCLUDED

#include <windows.h>

class TextDocument;

// Private line lookup result: document line number plus UTF-16 and backing-byte ranges.
struct TextLineIndexInfo
{
	ULONG lineno;
	ULONG lineoff_chars;
	ULONG linelen_chars;
	ULONG lineoff_bytes;
	ULONG linelen_bytes;
};

class TextLineIndex
{
public:
	TextLineIndex();
	~TextLineIndex();

	bool  init(TextDocument *doc);
	void  clear();

	ULONG linecount() const;
	bool  linecount_known() const;
	bool  lineno_known(ULONG lineno) const;
	bool  line_number_range_known(ULONG offset_chars, ULONG length_chars) const;

	void  index_lines(ULONG offset_chars, ULONG length_chars);
	bool  lineinfo_from_lineno(ULONG lineno, TextLineIndexInfo *lineinfo);
	bool  lineinfo_from_offset(ULONG offset_chars, TextLineIndexInfo *lineinfo);

private:
	// One lazily indexed page of document text.
	struct LinePage
	{
		ULONG   offset_bytes;
		ULONG   length_bytes;
		ULONG   offset_chars;
		ULONG   length_chars;
		ULONG * line_offsets_bytes;
		ULONG * line_offsets_chars;
		ULONG   line_count;
		ULONG   line_base;

		bool    indexed;
		bool    line_base_known;
		bool    starts_with_lf;
		bool    ends_with_cr;
	};

	ULONG  scan_lines(LinePage *page, ULONG *line_offsets_bytes, ULONG *line_offsets_chars);
	ULONG  estimate_line_count() const;
	bool   lineoffset_from_lineno(ULONG lineno, ULONG *offset_chars);
	bool   next_lineoffset(ULONG lineoff_chars, ULONG *nextoff_chars);
	bool   find_line_start_near_offset(ULONG near_offset_chars, ULONG *offset_chars);
	ULONG  text_length() const;
	ULONG  raw_length() const;
	ULONG  byte_from_char(ULONG offset_chars) const;
	ULONG  char_from_byte(ULONG offset_bytes) const;
	ULONG  utf16_length(ULONG ch32) const;
	bool   is_linebreak_char(ULONG ch32) const;

	TextDocument * m_pTextDoc;
	LinePage     * m_pLinePages;
	ULONG          m_nLinePageCount;
	ULONG          m_nLineCount;
	bool           m_fLineCountKnown;
};

#endif
