// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

//
//	MODULE:		TextViewFilter.cpp
//
//	PURPOSE:	Showing matching lines and their surrounding context.
//
//	NOTES:		www.catch22.net
//
//				The lines are written, as they're found, to a temp file in the
//				build's own text (UTF-16 or UTF-8), whatever the document's
//				encoding, so it reads back the same. A
//				read-only document of its own shows it, growing with it as a
//				tailed file does, so a filter of a huge document is read from
//				disk as it's needed too. The file goes when it's closed.
//

#include <algorithm>
#include <chrono>
#include "TextViewInternal.h"

// Filtering in the background: a megabyte at a time, for this long each tick
static const size_w FILTER_CHUNK = 0x100000;
static const int    FILTER_SLICE_MS = 15;

//
//	The whole document: the one shown, unless it's filtered
//
TextDocument *TextView::FileDoc() const
{
	return m_pSourceDoc ? m_pSourceDoc : m_pTextDoc;
}

// Set the surrounding line count, rebuilding an active filter when changed.
uint32_t TextView::SetFilterContext(uint32_t lines)
{
    uint32_t previous = m_nFilterContext;

    m_nFilterContext = min_of(lines, (uint32_t)TV_MAX_FILTER_CONTEXT);
    if (m_pSourceDoc && previous != m_nFilterContext)
    {
        EndFilter(true);
        m_fFilter = StartFilter();
    }

    return previous;
}

//
//	Show matching lines and their context, read-only: the first of them at
//	once, and the rest as they're found. The caret keeps its place, at the
//	first of them from its line on, once that's found
//
bool TextView::StartFilter()
{
	// the text's own byte-order mark, so the document reads it as written
	const uint16_t bom16  = 0xFEFF;
	const void	  *bom    = TCHAR_UTF8 ? (const void *)"\xEF\xBB\xBF" : (const void *)&bom16;
	size_t		   bomlen = TCHAR_UTF8 ? 3 : sizeof(bom16);
	size_w		   length = m_pTextDoc->byte_length();
	bool		   atEnd  = m_fTailing && m_cursorPos.byte_offset == length && m_selAnchor.byte_offset == length;
	TextDocument  *filtered;
	uint32_t	   row    = 0;

	if(!m_filterFile.create())
		return false;

	filtered = new TextDocument();

	if(!m_filterFile.write(bom, bomlen) || !filtered->init(m_filterFile.path(), true))
	{
		delete filtered;
		m_filterFile.close();
		return false;
	}

    m_pSourceDoc      = m_pTextDoc;
    m_pTextDoc        = filtered;
    m_filterFrom      = TextPos(0);
    m_filterWrittenTo = TextPos(0);
    m_filterAfter     = 0;
    m_nFilterEditMode = m_nEditMode;
    m_nEditMode       = MODE_READONLY;

	// the place to keep: the caret's line, on the same row of the window
	m_fFilterSeek    = !atEnd && m_cursorPos.byte_offset != 0;
	m_filterSeekLine = m_cursorPos.line_begin;

	if(m_fFilterSeek)
		ViewportRowFromLine(&m_cursorPos, &row);

	m_nFilterSeekRow = row;

    m_pTextDoc->coord_from_pos(TextPos(0), &m_cursorPos);
	m_selAnchor = m_cursorPos;
	SetScrollCoord(&m_cursorPos);

	if(FilterSlice())
		m_pHost->StartTimer(FILTER_TIMER, 0);

	ShowFilteredLines();
	SeekFilteredPlace();

	// following the file's end, follow the end of its matches
	if(atEnd)
		CaretToEnd();

    // Source line numbers become available through the existing index timer.
    IndexFile(true);
    return true;
}

//
//	Show the whole document again. Keeping its place, the caret goes to the
//	start of the line it was on, on the same row of the window.
//
void TextView::EndFilter(bool fKeepPlace)
{
	TextPos origin;
	size_w  lineno;
	uint32_t   row = 0;
	size_w  length;
	bool    atEnd;

	if(m_pSourceDoc == 0)
		return;

	length = m_pTextDoc->byte_length();
	atEnd  = m_fTailing && m_cursorPos.byte_offset == length && m_selAnchor.byte_offset == length;

	m_pHost->StopTimer(FILTER_TIMER);

	if(fKeepPlace && m_pTextDoc->lineno_from_pos(m_cursorPos, &lineno) == TDR_OK && lineno < m_filterOrigins.size())
	{
		origin = m_filterOrigins[(size_t)lineno];
		ViewportRowFromLine(&m_cursorPos, &row);
	}

	delete m_pTextDoc;
	m_filterFile.close();

	m_pTextDoc	  = m_pSourceDoc;
	m_pSourceDoc  = 0;
	m_nEditMode	  = m_nFilterEditMode;
	std::vector<TextPos>().swap(m_filterOrigins);

	if(fKeepPlace)
	{
		LineIterator line = m_pTextDoc->line_at(origin);
		TextCoord    top;

		while(row-- > 0 && line.prev())
			;

		top = line.line();
		SetScrollCoord(&top);

		m_pTextDoc->coord_from_pos(origin, &m_cursorPos);
		m_selAnchor = m_cursorPos;

		ResetLineCache();
		UpdateViewState(true);
		RefreshWindow();

		// following the end of the matches, follow the file's end
		if(atEnd)
			CaretToEnd();
	}
}

//
//	Filter for a few milliseconds, writing each line that holds a match to the
//	temp file, and remembering where it came from. Returns whether there's more.
//
bool TextView::FilterSlice()
{
    size_w        added;

    auto start = std::chrono::steady_clock::now();

    // A match at the old end may still be waiting for following context lines.
    if (m_filterAfter && m_filterWrittenTo.byte_offset < m_pSourceDoc->byte_length())
    {
        LineIterator line = m_pSourceDoc->line_at(m_filterWrittenTo);

        if (line.line().line_begin == m_filterWrittenTo || line.next())
            m_filterAfter = WriteFilterLines(line, m_filterAfter);
    }

    auto found = [this](TextPos begin, TextPos)
    {
        WriteFilterContext(begin);
    };

    do
    {
        m_filterFrom = m_pSourceDoc->filter_lines(m_sMatchText.data(), (uint32_t)m_sMatchText.size(),
                                                  m_fMatchCase != false, m_filterFrom, FILTER_CHUNK, found);

    } while (m_filterFrom.byte_offset < m_pSourceDoc->byte_length() &&
             std::chrono::steady_clock::now() - start < std::chrono::milliseconds(FILTER_SLICE_MS));

    m_pTextDoc->grow(&added);

    return m_filterFrom.byte_offset < m_pSourceDoc->byte_length();
}

// Expand a match with the existing line iterator. Matching and chunk scanning
// stay in TextDocument; source offsets let overlapping ranges merge naturally.
void TextView::WriteFilterContext(TextPos match)
{
    LineIterator line  = m_pSourceDoc->line_at(match);
    uint32_t         count = m_nFilterContext + 1;

    for (uint32_t before = 0; before < m_nFilterContext && line.prev(); ++before)
        ++count;

    m_filterAfter = WriteFilterLines(line, count);
}

// Append only source lines not already included by an earlier match. Return
// the context lines not yet available at EOF, for a file being followed.
uint32_t TextView::WriteFilterLines(LineIterator line, uint32_t count)
{
    while (count && line.line().line_begin.byte_offset < m_pSourceDoc->byte_length())
    {
        const TextCoord &coord = line.line();

        if (coord.line_begin >= m_filterWrittenTo)
        {
            WriteFilterLine(coord.line_begin, coord.line_next);
            m_filterOrigins.push_back(coord.line_begin);
            m_filterWrittenTo = coord.line_next;
        }

        --count;
        if (!line.next())
            break;
    }

    return count;
}

//
//	Write a line of the whole document to the temp file - with a line break of
//	the document's kind, if it hasn't one: the document's last line
//
void TextView::WriteFilterLine(TextPos begin, TextPos next)
{
	TextReader	 reader = m_pSourceDoc->text_from_range(begin, next);
	tchar_t		 buf[0x1000];
	tchar_t		 last = 0;
	tchar_t		 crlf[] = TSTR("\r\n");
	uint32_t	 len;

	while((len = reader.read(buf, sizeof(buf) / sizeof(buf[0]))) != 0)
	{
		m_filterFile.write(buf, len * sizeof(tchar_t));
		last = buf[len - 1];
	}

	if(last != '\r' && last != '\n')
	{
		tchar_t *brk = m_nCRLFMode == TXL_LF ? crlf + 1 : crlf;
		uint32_t  brklen = m_nCRLFMode == TXL_LF || m_nCRLFMode == TXL_CR ? 1 : 2;

		m_filterFile.write(brk, brklen * sizeof(tchar_t));
	}
}

//
//	Show the lines found so far: the last of them can be longer now, too
//
void TextView::ShowFilteredLines()
{
	TextCoord top;

	m_pTextDoc->coord_from_pos(TextPos(m_cursorPos.byte_offset), &m_cursorPos);
	m_pTextDoc->coord_from_pos(TextPos(m_selAnchor.byte_offset), &m_selAnchor);
	m_pTextDoc->coord_from_pos(TextPos(m_scrollVPos.byte_offset), &top);
	SetScrollCoord(&top);

	m_nLineCount = m_pTextDoc->linecount();

	ResetLineCache();
	UpdateMarginWidth();
	SetupScrollbars();
	RepositionCaret();
	RefreshWindow();
}

//
//	Once the filter has found it, put the caret at the first line shown from
//	the line it was on (or the last line, if none is), on the window's row it
//	was on, as EndFilter keeps the place going back
//
void TextView::SeekFilteredPlace()
{
	bool		 done;
	size_t		 lineno;
	TextCoord	 coord;
	TextCoord	 top;
	uint32_t	 row = m_nFilterSeekRow;

	if(!m_fFilterSeek)
		return;

	// moved meanwhile, the caret stays where it's been put
	if(m_cursorPos.byte_offset != 0 || m_selAnchor.byte_offset != 0)
	{
		m_fFilterSeek = false;
		return;
	}

	done   = m_filterFrom.byte_offset >= m_pSourceDoc->byte_length();
	lineno = std::lower_bound(m_filterOrigins.begin(), m_filterOrigins.end(), m_filterSeekLine) -
			 m_filterOrigins.begin();

	if(lineno == m_filterOrigins.size())
	{
		if(!done)
			return;

		lineno = lineno ? lineno - 1 : 0;
	}

	m_fFilterSeek = false;

	if(m_pTextDoc->coord_from_lineno(lineno, &coord) != TDR_OK)
		return;

	LineIterator line = m_pTextDoc->line_at(coord);

	while(row-- > 0 && line.prev())
		;

	top = line.line();
	SetScrollCoord(&top);

	m_cursorPos = coord;
	m_selAnchor = m_cursorPos;

	ResetLineCache();
	UpdateViewState(true);
	RefreshWindow();
	NotifyCursorChange();
}

int32_t TextView::OnFilterTimer()
{
	// following a file, a caret at the end of the lines found so far stays at
	// the end as more are found, as UpdateTail keeps it there
	size_w length = m_pTextDoc->byte_length();
	bool   atEnd  = m_fTailing && m_cursorPos.byte_offset == length && m_selAnchor.byte_offset == length;

	if(m_pSourceDoc == 0 || !FilterSlice())
		m_pHost->StopTimer(FILTER_TIMER);

	if(m_pSourceDoc != 0)
	{
		ShowFilteredLines();
		SeekFilteredPlace();

		if(atEnd && m_pTextDoc->byte_length() != length)
			CaretToEnd();
	}

	return 0;
}
