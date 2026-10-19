//
//	MODULE:		TextViewFilter.cpp
//
//	PURPOSE:	Showing matching lines and their surrounding context.
//
//	NOTES:		www.catch22.net
//
//				The lines are written, as they're found, to a temp file: UTF-16,
//				whatever the document's encoding, so it reads back the same. A
//				read-only document of its own shows it, growing with it as a
//				tailed file does, so a filter of a huge document is read from
//				disk as it's needed too. The file goes when it's closed.
//

#define STRICT
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <tchar.h>
#include "TextView.h"
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
UINT TextView::SetFilterContext(UINT lines)
{
    UINT previous = m_nFilterContext;

    m_nFilterContext = min(lines, (UINT)TV_MAX_FILTER_CONTEXT);
    if (m_pSourceDoc && previous != m_nFilterContext)
    {
        EndFilter(true);
        m_fFilter = StartFilter();
    }

    return previous;
}

//
//	Show matching lines and their context, read-only, from
//	the top: the first of them at once, and the rest as they're found
//
bool TextView::StartFilter()
{
	TCHAR		  dir[MAX_PATH];
	TCHAR		  path[MAX_PATH];
	WCHAR		  bom = 0xFEFF;
	DWORD		  written;
	TextDocument *filtered;

	if(!GetTempPath(MAX_PATH, dir) || !GetTempFileName(dir, _T("npf"), 0, path))
		return false;

	m_hFilterFile = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0,
							   CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, 0);

	if(m_hFilterFile == INVALID_HANDLE_VALUE)
	{
		DeleteFile(path);
		return false;
	}

	filtered = new TextDocument();

	if(!WriteFile(m_hFilterFile, &bom, sizeof(bom), &written, 0) || !filtered->init(path, true))
	{
		delete filtered;
		CloseHandle(m_hFilterFile);
		m_hFilterFile = INVALID_HANDLE_VALUE;
		return false;
	}

    m_pSourceDoc      = m_pTextDoc;
    m_pTextDoc        = filtered;
    m_filterFrom      = TextPos(0);
    m_filterWrittenTo = TextPos(0);
    m_filterAfter     = 0;
    m_nFilterEditMode = m_nEditMode;
    m_nEditMode       = MODE_READONLY;

    m_pTextDoc->coord_from_pos(TextPos(0), &m_cursorPos);
	m_selAnchor = m_cursorPos;
	SetScrollCoord(&m_cursorPos);

	if(FilterSlice())
		SetTimer(m_hWnd, FILTER_TIMER, USER_TIMER_MINIMUM, 0);

	ShowFilteredLines();

    // Source line numbers become available through the existing index timer.
    IndexFile(TRUE);
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
	ULONG   row = 0;

	if(m_pSourceDoc == 0)
		return;

	KillTimer(m_hWnd, FILTER_TIMER);

	if(fKeepPlace && m_pTextDoc->lineno_from_pos(m_cursorPos, &lineno) == TDR_OK && lineno < m_filterOrigins.size())
	{
		origin = m_filterOrigins[(size_t)lineno];
		ViewportRowFromLine(&m_cursorPos, &row);
	}

	delete m_pTextDoc;
	CloseHandle(m_hFilterFile);

	m_pTextDoc	  = m_pSourceDoc;
	m_pSourceDoc  = 0;
	m_hFilterFile = INVALID_HANDLE_VALUE;
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
		UpdateViewState(TRUE);
		RefreshWindow();
	}
}

//
//	Filter for a few milliseconds, writing each line that holds a match to the
//	temp file, and remembering where it came from. Returns whether there's more.
//
bool TextView::FilterSlice()
{
    LARGE_INTEGER freq, start, now;
    size_w        added;

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

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
                                                  m_fMatchCase != FALSE, m_filterFrom, FILTER_CHUNK, found);

        QueryPerformanceCounter(&now);
    } while (m_filterFrom.byte_offset < m_pSourceDoc->byte_length() &&
             (now.QuadPart - start.QuadPart) * 1000 < freq.QuadPart * FILTER_SLICE_MS);

    m_pTextDoc->grow(&added);

    return m_filterFrom.byte_offset < m_pSourceDoc->byte_length();
}

// Expand a match with the existing line iterator. Matching and chunk scanning
// stay in TextDocument; source offsets let overlapping ranges merge naturally.
void TextView::WriteFilterContext(TextPos match)
{
    LineIterator line  = m_pSourceDoc->line_at(match);
    UINT         count = m_nFilterContext + 1;

    for (UINT before = 0; before < m_nFilterContext && line.prev(); ++before)
        ++count;

    m_filterAfter = WriteFilterLines(line, count);
}

// Append only source lines not already included by an earlier match. Return
// the context lines not yet available at EOF, for a file being followed.
UINT TextView::WriteFilterLines(LineIterator line, UINT count)
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
	TCHAR		 buf[0x1000];
	TCHAR		 last = 0;
	TCHAR		 crlf[] = _T("\r\n");
	uint32_t	 len;
	DWORD		 written;

	while((len = reader.read(buf, ARRAYSIZE(buf))) != 0)
	{
		WriteFile(m_hFilterFile, buf, len * sizeof(TCHAR), &written, 0);
		last = buf[len - 1];
	}

	if(last != '\r' && last != '\n')
	{
		TCHAR *brk = m_nCRLFMode == TXL_LF ? crlf + 1 : crlf;
		DWORD  brklen = m_nCRLFMode == TXL_LF || m_nCRLFMode == TXL_CR ? 1 : 2;

		WriteFile(m_hFilterFile, brk, brklen * sizeof(TCHAR), &written, 0);
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

LONG TextView::OnFilterTimer()
{
	if(m_pSourceDoc == 0 || !FilterSlice())
		KillTimer(m_hWnd, FILTER_TIMER);

	if(m_pSourceDoc != 0)
		ShowFilteredLines();

	return 0;
}
