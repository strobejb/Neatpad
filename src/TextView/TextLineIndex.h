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
	bool  chars_known;
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
	// One lazily indexed page of native text code-units.
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
		bool    offset_known;
		bool    chars_known;
		bool    line_base_known;
		bool    starts_with_lf;
		bool    ends_with_cr;
	};

	bool   direct_offset_mapping() const;
	void   ensure_page_offset(LinePage *page);
	ULONG  scan_lines(LinePage *page, ULONG *line_offsets_bytes, ULONG *line_offsets_chars);
	ULONG  estimate_line_count() const;
	bool   lineoffset_from_lineno(ULONG lineno, ULONG *offset_chars);
	bool   next_lineoffset(ULONG lineoff_chars, ULONG *nextoff_chars);
	bool   find_line_start_near_offset(ULONG near_offset_chars, ULONG *offset_chars);
	ULONG  text_length() const;
	ULONG  raw_length() const;
	ULONG  codeunit_size() const;
	ULONG  byte_from_char(ULONG offset_chars) const;
	ULONG  char_from_byte(ULONG offset_bytes) const;

	TextDocument * m_pTextDoc;
	LinePage     * m_pLinePages;
	ULONG          m_nLinePageCount;
	ULONG          m_nLineCount;
	bool           m_fLineCountKnown;
};

#endif
