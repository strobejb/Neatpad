#ifndef TEXTLINEBUFFER_INCLUDED
#define TEXTLINEBUFFER_INCLUDED

#include <windows.h>

class TextDocument;

// Private line lookup result: document line number plus UTF-16 and backing-byte ranges.
struct TextLineBufferInfo
{
	ULONG lineno;
	ULONG lineoff_chars;
	ULONG linelen_chars;
	ULONG lineoff_bytes;
	ULONG linelen_bytes;
};

class TextLineBuffer
{
public:
	TextLineBuffer();
	~TextLineBuffer();

	bool  init(TextDocument *doc);
	void  clear();

	ULONG linecount() const;
	bool  linecount_known() const;
	bool  lineno_known(ULONG lineno) const;
	bool  line_number_range_known(ULONG offset_chars, ULONG length_chars) const;

	void  index_lines(ULONG offset_chars, ULONG length_chars);
	bool  lineinfo_from_lineno(ULONG lineno, TextLineBufferInfo *lineinfo);
	bool  lineinfo_from_offset(ULONG offset_chars, TextLineBufferInfo *lineinfo);

private:
	// One lazily indexed page of document text.
	struct LinePage
	{
		ULONG   offset_bytes;
		ULONG   length_bytes;
		ULONG * line_offsets;
		ULONG   line_count;
		ULONG   line_base;

		bool    indexed;
		bool    line_base_known;
		bool    starts_with_lf;
		bool    ends_with_cr;
	};

	ULONG  scan_lines(ULONG offset_bytes, ULONG length_bytes, ULONG *line_offsets);
	ULONG  estimate_line_count() const;
	bool   lineoffset_from_lineno(ULONG lineno, ULONG *offset_chars);
	bool   next_lineoffset(ULONG lineoff_chars, ULONG *nextoff_chars);
	bool   find_line_start_near_offset(ULONG near_offset_chars, ULONG *offset_chars);
	ULONG  text_length() const;

	TextDocument * m_pTextDoc;
	LinePage     * m_pLinePages;
	ULONG          m_nLinePageCount;
	ULONG          m_nLineCount;
	bool           m_fLineCountKnown;
};

#endif
