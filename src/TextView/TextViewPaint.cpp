//
//	MODULE:		TextViewPaint.cpp
//
//	PURPOSE:	Painting and display for the TextView control
//
//	NOTES:		www.catch22.net
//

#define STRICT
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <limits.h>
#include <tchar.h>
#include "TextView.h"
#include "TextViewInternal.h"
#include "../TextDocument/Unicode.h"
#include "win32/Win32Canvas.h"

extern "C" COLORREF MixRGB(COLORREF, COLORREF);

// Diff gutter sizes, in pixels. The inset is the gap before the text edge.
static const int CHANGE_MARKER_WIDTH    = 8;
static const int CHANGE_MARKER_INSET    = CHANGE_MARKER_WIDTH;
static const int CHANGE_TRIANGLE_WIDTH  = 9;
static const int CHANGE_TRIANGLE_HEIGHT = 9; // maximum; clipped to fit small line heights
static const int CHANGE_ICON_PADDING    = 3;
static const int CHANGE_MARGIN_WIDTH =
    CHANGE_MARKER_INSET + CHANGE_ICON_PADDING +
    (CHANGE_MARKER_WIDTH > CHANGE_TRIANGLE_WIDTH ? CHANGE_MARKER_WIDTH : CHANGE_TRIANGLE_WIDTH);

static void PaintDeletionTriangle(TextCanvas &canvas, int x, int y, int width, int height, COLORREF colour)
{
    int middle = height / 2;

    for (int row = 0; row < height; ++row)
    {
        int distance = row > middle ? row - middle : middle - row;
        int extent   = middle ? 1 + (width - 1) * (middle - distance) / middle : width;

        canvas.FillRect(x, y + row, extent, 1, colour);
    }
}

//
//	Perform a full redraw of the entire window
//
VOID TextView::RefreshWindow()
{
	m_pHost->Invalidate();
}

//
//	Return the laid-out line containing coord. Entries are keyed by the line's
//	starting byte; ResetLineCache flushes them on every edit.
//
LINECACHE *TextView::GetLineCache(TextCoord *coord)
{
	TCHAR	*buff;
	ATTR	*attr;
	ULONG	 buflen;
	ULONG	 colno = 0;
	int		 len;
	int		 columns;

	TextLayout *layout;
	ULONG    lru_usage = -1;
	int		 lru_index = 0;

	if(coord == 0)
		return 0;

	//
	//	Search the cache to see if we've already analyzed the requested line
	//
	for(int i = 0; i < LINE_CACHE_SIZE; i++)
	{
		// remember the least-recently used
		if(m_lineCache[i].usage < lru_usage)
		{
			lru_index = i;
			lru_usage = m_lineCache[i].usage;
		}

		if(m_lineCache[i].usage > 0 && m_lineCache[i].line_begin == coord->line_begin)
		{
			m_lineCache[i].usage++;
			return &m_lineCache[i];
		}
	}

	//
	// not found? overwrite the "least-recently-used" entry
	//
	m_lineCache[lru_index].line_begin = coord->line_begin;
	m_lineCache[lru_index].usage		= 1;
	layout = m_lineCache[lru_index].layout;

	//
	// get the text for the entire line (up to the layout limit) and apply style
	// attributes. A line never decodes to more UTF-16 units than it has bytes.
	//
	buflen = (ULONG)min(coord->line_next - coord->line_begin, (size_w)LINE_LAYOUT_LIMIT);
	buff   = new TCHAR[buflen + 1];
	attr   = new ATTR[buflen + 1];

	len = m_pTextDoc->getline(*coord, buff, buflen);

	// cache the line's length information
	m_lineCache[lru_index].length		= len;
	m_lineCache[lru_index].length_CRLF	= len - CRLF_size(buff, len);

	len = ApplyTextAttributes(coord, colno, buff, len, attr);

	//
	// go!
	//
	layout->Analyze(buff, len, attr, m_nTabWidthChars);

	//
	//	Apply the selection
	//
	ApplySelection(layout, coord, len);

	//
	//	The widest line laid out so far sets the horizontal scrolling range.
	//	It only ever grows, with a column to spare for the caret at the end.
	//
	columns = (layout->Width() + m_nFontWidth - 1) / m_nFontWidth + 1;

	if(columns > m_nLongestLine)
	{
		m_nLongestLine = columns;
		SetupScrollbars();
	}

	delete[] buff;
	delete[] attr;

	return &m_lineCache[lru_index];
}

//
//	Return the laid-out line containing coord
//
TextLayout *TextView::GetLayout(TextCoord *coord)
{
	LINECACHE *lineCache = GetLineCache(coord);

	if(lineCache)
		return lineCache->layout;
	else
		return 0;
}

//
//	Invalidate every entry in the cache so we can start afresh
//
void TextView::ResetLineCache()
{
	for(int i = 0; i < LINE_CACHE_SIZE; i++)
	{
		m_lineCache[i].usage	= 0;
	}
}

//
//	Painting procedure for TextView objects
//
LONG TextView::OnPaint()
{
	PAINTSTRUCT ps;
	HRGN		hrgnUpdate;

	//
	// get update region *before* BeginPaint validates the window
	//
	hrgnUpdate = CreateRectRgn(0,0,1,1);
	GetUpdateRgn(m_hWnd, hrgnUpdate, FALSE);

	BeginPaint(m_hWnd, &ps);

	TextCanvas *canvas = CreateCanvas(ps.hdc);

	Paint(*canvas, &ps.rcPaint);
	delete canvas;

	EndPaint(m_hWnd, &ps);

	DeleteObject(hrgnUpdate);

	return 0;
}

//
//	WM_PRINTCLIENT: draw the whole client area into hdc, as WM_PAINT draws it
//	when the whole window needs updating
//
LONG TextView::OnPrintClient(HDC hdc)
{
	RECT rect;

	GetClientRect(m_hWnd, &rect);

	TextCanvas *canvas = CreateCanvas(hdc);

	Paint(*canvas, &rect);
	delete canvas;

	return 0;
}

//
//	A canvas over hdc, with the view's main font for line numbers. The host
//	will make these once it holds the fonts.
//
TextCanvas *TextView::CreateCanvas(HDC hdc)
{
	return new Win32Canvas(hdc, m_uspFontList[0].hFont, m_hImageList);
}

//
//	Draw the rows that rcPaint covers, one line at a time. A row is drawn
//	whole, even when only part of it needs it.
//
void TextView::Paint(TextCanvas &canvas, RECT *rcPaint)
{
	ULONG		row;
	ULONG		first;
	ULONG		last;
	TextCoord	lineCoord;
	LineIterator lines;
	bool		haveLine;
	RECT		rect;

	GetClientRect(m_hWnd, &rect);

	//
	// figure out which lines to redraw
	//
	first = rcPaint->top    / m_nLineHeight;
	last  = rcPaint->bottom / m_nLineHeight;

	// rows past the end of the document have no line
	haveLine = ViewportLineFromRow(first, &lineCoord);
	lines = m_pTextDoc->line_at(lineCoord);

	//
	// draw the display line-by-line
	//
	for(row = first; row <= last; row++)
	{
		int sy		= row * m_nLineHeight;
		int width	= rect.right-rect.left;
		TextCoord *line = haveLine ? &lineCoord : 0;

		canvas.BeginRow(sy, width, m_nLineHeight);

		// prep the background
		canvas.FillRect(0, 0, width, m_nLineHeight, LineColour(line));

		// draw the line, then its margin
		PaintLine(canvas, line, -m_nHScrollPos * m_nFontWidth);

		canvas.EndRow();

		// step down to the next line, if there is one
		if(haveLine)
		{
			haveLine  = lines.next();
			lineCoord = lines.line();
		}
	}

}

//
//	Draw the specified line (including margins etc) in the current row, with
//	the text starting at xpos. coord is 0 for rows past the end of the document.
//
void TextView::PaintLine(TextCanvas &canvas, TextCoord *coord, int xpos)
{
	RECT	bounds;

	// the text's part of the row, from where the line starts on-screen
	GetClientRect(m_hWnd, &bounds);

	bounds.left		= (long)(-m_nHScrollPos * m_nFontWidth + LeftMarginWidth());
	bounds.top		= 0;
	bounds.bottom	= m_nLineHeight;

	PaintText(canvas, coord, xpos + LeftMarginWidth(), 0, &bounds);

	//
	//	draw the margin straight over the top
	//
	if(LeftMarginWidth() > 0)
	{
		PaintMargin(canvas, coord, 0, 0);
	}
}

//
//	Return width of margin
//
int TextView::LeftMarginWidth()
{
	int width	= 0;
	int cx		= 0;
	int cy		= 0;

	// get dimensions of imagelist icons
	if(m_hImageList)
		ImageList_GetIconSize(m_hImageList, &cx, &cy);

	if(CheckStyle(TXS_LINENUMBERS))
	{		
		width += m_nLinenoWidth;

		if(CheckStyle(TXS_SELMARGIN) && cx > 0)
			width += cx + 4;
		else
			width += 20;

        if (CheckStyle(TXS_SELMARGIN) && CheckStyle(TXS_CHANGEMARKERS))
        {
            width += CHANGE_MARGIN_WIDTH;
        }

        return width;
	}
	// selection margin by itself
	else if(CheckStyle(TXS_SELMARGIN))
	{
        width += cx + 4 + (CheckStyle(TXS_CHANGEMARKERS) ? CHANGE_MARGIN_WIDTH : 0);

		return width;
	}

	return 0;
}

//
//	This must be called whenever the number of lines changes
//  (probably easier to call it when the file-size changes)
//
void TextView::UpdateMarginWidth()
{
	HDC		hdc		 = GetDC(m_hWnd);
	HANDLE	hOldFont = SelectObject(hdc, m_uspFontList[0].hFont);

	TCHAR	buf[32];
    int     len = wsprintf(buf, LINENO_FMT, (unsigned __int64)FileDoc()->linecount());

    m_nLinenoWidth = TextWidth(hdc, buf, len);

	SelectObject(hdc, hOldFont);
	ReleaseDC(m_hWnd, hdc);
}

// Filtered text is a UTF-16 copy. Compare its source line instead, so both
// the colours and the gutter retain the original document's saved baseline.
uint32_t TextView::LineDiff(TextCoord *line, const std::function<void(size_w, size_w)> &inserted, size_w columns)
{
    if (!line)
    {
        return false;
    }

    if (m_pSourceDoc)
    {
        size_w    lineno;
        TextCoord source;

        if (m_pTextDoc->lineno_from_pos(*line, &lineno) != TDR_OK || lineno >= m_filterOrigins.size() ||
            !m_pSourceDoc->coord_from_pos(m_filterOrigins[(size_t)lineno], &source))
        {
            return false;
        }

        return m_pSourceDoc->line_diff(source, inserted, columns);
    }

    return m_pTextDoc->line_diff(*line, inserted, columns);
}

// Join only neighbouring source lines: filtering can put unrelated lines
// beside each other on screen, and those must retain their separate end caps.
bool TextView::DiffJoins(TextCoord *line, bool previous)
{
    if (!line)
    {
        return false;
    }

    LineIterator neighbour = m_pTextDoc->line_at(line->line_begin);

    if (!(previous ? neighbour.prev() : neighbour.next()))
    {
        return false;
    }

    if (m_pSourceDoc)
    {
        size_w row;

        if (m_pTextDoc->lineno_from_pos(*line, &row) != TDR_OK || row >= m_filterOrigins.size() ||
            (previous && row == 0) || (!previous && row + 1 >= m_filterOrigins.size()))
        {
            return false;
        }

        size_w       adjacent = previous ? row - 1 : row + 1;
        LineIterator source   = m_pSourceDoc->line_at(m_filterOrigins[(size_t)row]);

        if (!(previous ? source.prev() : source.next()) ||
            source.line().line_begin != m_filterOrigins[(size_t)adjacent])
        {
            return false;
        }
    }

    TextCoord adjacent = neighbour.line();

    return (LineDiff(&adjacent) & TextDocument::diff_inserted) != 0;
}

//
//	Draw the specified line's margin into the area described by *margin*
//
int TextView::PaintMargin(TextCanvas &canvas, TextCoord *line, int xpos, int ypos)
{
	RECT	rect = { xpos, ypos, xpos + LeftMarginWidth(), ypos + m_nLineHeight };
	size_w	nLineNo = 0;
	bool	exact = line && m_pTextDoc->lineno_from_pos(*line, &nLineNo) == TDR_OK;

	int		imgWidth;
	int		imgHeight;
	int		imgX;
	int		imgY;
	int		selwidth = CheckStyle(TXS_SELMARGIN) ? 20 : 0;

	TCHAR	ach[32];

	//int nummaxwidth = 60;

	if(m_hImageList && selwidth > 0)
	{
		// selection margin must include imagelists
		ImageList_GetIconSize(m_hImageList, &imgWidth, &imgHeight);

		imgX = xpos + (selwidth		 - imgWidth) / 2;
		imgY = ypos + (m_nLineHeight - imgHeight) / 2;
	}

	if(CheckStyle(TXS_LINENUMBERS))
	{
        // One continuous background for numbers, icons and change markers.
        canvas.FillRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, GetColour(TXC_LINENUMBER));

        size_w number = nLineNo;
        bool   known  = exact && nLineNo < m_nLineCount;

        // Filtered rows retain their source offsets. Only the displayed number
        // changes; line images and selection still use the filtered row.
        if (m_pSourceDoc && known)
        {
            known = nLineNo < m_filterOrigins.size() &&
                    m_pSourceDoc->lineno_from_pos(m_filterOrigins[(size_t)nLineNo], &number) == TDR_OK;
        }

        // Leave pending source numbers and the filtered file's synthetic final
        // empty line blank. Painting must not force the source to be indexed.
        int len   = known ? wsprintf(ach, LINENO_FMT, (unsigned __int64)(number + 1)) : 0;
        int width = canvas.MarginTextWidth(ach, len);

        rect.right = rect.left + m_nLinenoWidth;

        if (CheckStyle(TXS_SELMARGIN) && m_hImageList)
        {
            imgX        = rect.right;
            rect.right += imgWidth + 4;
        }
        else
		{
			rect.right += 20;
		}
		
		canvas.DrawMarginText(rect.left + m_nLinenoWidth - width,
							  rect.top  + NeatTextYOffset(&m_uspFontList[0]),
							  ach,
							  len,
							  GetColour(TXC_LINENUMBERTEXT),
							  GetColour(TXC_LINENUMBER),
							  rect.left, rect.top, rect.right, rect.bottom);
	}
	else
	{
		canvas.FillChecked(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
						   GetColour(TXC_SELMARGIN1), GetColour(TXC_SELMARGIN2));
	}

	//
	//	Retrieve information about this specific line
	//
	LINEINFO *linfo = exact && nLineNo <= (size_w)ULONG_MAX ? GetLineInfo((ULONG)nLineNo) : 0;

	if(m_hImageList && linfo && nLineNo < m_nLineCount)
	{
		canvas.DrawMarginImage(linfo->nImageIdx, imgX, imgY, imgWidth, imgHeight);
	}

    // The margin background above also clears markers after Save or a style change.
    if (CheckStyle(TXS_SELMARGIN) && CheckStyle(TXS_CHANGEMARKERS))
    {
        int      right       = xpos + LeftMarginWidth();
        int      markerRight = right - CHANGE_MARKER_INSET;
        int      markerLeft  = markerRight - CHANGE_MARKER_WIDTH;
        int      triangle    = min(CHANGE_TRIANGLE_HEIGHT, max(1, m_nLineHeight - 2));
        uint32_t diff        = LineDiff(line);

        // Mixed insertion/deletion is a modification: its outline takes
        // precedence. Deletion-only lines use triangles in the same gutter.
        if (diff & TextDocument::diff_inserted)
        {
            COLORREF colour = GetColour(TXC_CHANGEDLINE);

            canvas.FillRect(markerLeft, ypos, CHANGE_MARKER_WIDTH, m_nLineHeight, GetColour(TXC_CHANGEDLINEBG));
            canvas.FillRect(markerLeft, ypos, 1, m_nLineHeight, colour);
            canvas.FillRect(markerRight - 1, ypos, 1, m_nLineHeight, colour);

            if (!DiffJoins(line, true))
            {
                canvas.FillRect(markerLeft, ypos, CHANGE_MARKER_WIDTH, 1, colour);
            }

            if (!DiffJoins(line, false))
            {
                canvas.FillRect(markerLeft, ypos + m_nLineHeight - 1, CHANGE_MARKER_WIDTH, 1, colour);
            }
        }
        else
        {
            // Keep a sharp centre point and room for two deletion boundaries.
            if ((diff & TextDocument::diff_erased_before) && (diff & TextDocument::diff_erased_after))
            {
                triangle = min(triangle, max(1, (m_nLineHeight - 1) / 2));
            }
            if (triangle > 1 && triangle % 2 == 0)
            {
                --triangle;
            }

            int triangleLeft = markerRight - CHANGE_TRIANGLE_WIDTH;

            if (diff & TextDocument::diff_erased_before)
            {
                PaintDeletionTriangle(canvas, triangleLeft, ypos, CHANGE_TRIANGLE_WIDTH, triangle,
                                      GetColour(TXC_CHANGEDTEXT));
            }

            if (diff & TextDocument::diff_erased_after)
            {
                PaintDeletionTriangle(canvas, triangleLeft, ypos + m_nLineHeight - triangle, CHANGE_TRIANGLE_WIDTH,
                                      triangle, GetColour(TXC_CHANGEDTEXT));
            }
        }
    }

    return rect.right - rect.left;
}

static bool HasAppFocus(HWND hwnd)
{
	if(!IsWindowEnabled(hwnd) || !IsWindowEnabled(GetAncestor(hwnd, GA_ROOT)))
		return false;

	HWND hwndFocus = GetFocus();

	if(hwndFocus == hwnd || IsChild(hwnd, hwndFocus))
		return true;

	HWND hwndRoot = GetAncestor(hwnd, GA_ROOT);
	HWND hwndForeground = GetForegroundWindow();

	return hwndForeground == hwndRoot || GetAncestor(hwndForeground, GA_ROOT) == hwndRoot;
}

//
//	Draw a line of text into the specified device-context
//
void TextView::PaintText(TextCanvas &canvas, TextCoord *coord, int xpos, int ypos, RECT *bounds)
{
	TextLayout * layout;

	// grab the layout for this line
	layout = GetLayout(coord);

	if(layout == 0)
		return;

	// set highlight-colours depending on window-focus
	if(HasAppFocus(m_hWnd))
		layout->SetSelectionColours(GetColour(TXC_HIGHLIGHTTEXT), GetColour(TXC_HIGHLIGHT));
	else
		layout->SetSelectionColours(GetColour(TXC_HIGHLIGHTTEXT2), GetColour(TXC_HIGHLIGHT2));

	// update selection-attribute information for the line
	ULONG selStart, selEnd;

	SelectionColumns(coord, layout->Length(), &selStart, &selEnd);
	layout->SetSelection(selStart, selEnd);

	ApplySelection(layout, coord, layout->Length());

	// draw the text!
	layout->Draw(canvas, xpos, ypos, m_nLineHeight, m_nHeightAbove, bounds->left, bounds->top, bounds->right,
				 bounds->bottom);
}

int	TextView::ApplySelection(TextLayout *layout, TextCoord *line, ULONG nTextLen)
{
	int selstart = 0;
	int selend   = 0;

	if(m_nSelectionType != SEL_BLOCK)
		return 0;

	if(line->line_begin >= m_cpBlockStart.line_begin && line->line_begin <= m_cpBlockEnd.line_begin)
	{
		bool trailing;

		layout->XToOffset(m_cpBlockStart.xpos, &selstart, &trailing);
		selstart += trailing;

		layout->XToOffset(m_cpBlockEnd.xpos, &selend, &trailing);
		selend += trailing;

		if(selstart > selend)
			selstart ^= selend ^= selstart^= selend;
	}

	layout->SetSelection(selend, selstart);

	return 0;
}

//
//	Colour each match of the text ShowMatches was given, left to right, as
//	finding it would match it: unit for unit, or regardless of case
//
void TextView::HighlightMatches(TCHAR *szText, int nTextLen, ATTR *attr)
{
	int length = (int)m_sMatchText.size();
	int i = 0;

	while(length != 0 && i + length <= nTextLen)
	{
		bool match = m_fMatchCase ?
			memcmp(szText + i, m_sMatchText.data(), length * sizeof(TCHAR)) == 0 :
			utf16_equal_nocase((const UTF16 *)(szText + i), (const UTF16 *)m_sMatchText.data(), length);

		if(!match)
		{
			i++;
			continue;
		}

		for(int end = i + length; i < end; i++)
		{
			attr[i].fg = GetColour(TXC_SEARCHMATCHTEXT);
			attr[i].bg = GetColour(TXC_SEARCHMATCH);
		}
	}
}

//
//	Apply visual-styles to the text by returning colour and font
//	information into the supplied TEXT_ATTR structure
//
//	line	- the line being analyzed
//
//	Returns new length of buffer if text has been modified
//
int TextView::ApplyTextAttributes(TextCoord *line, ULONG &nColumn, TCHAR *szText, int nTextLen, ATTR *attr)
{
	int i;

	ULONG selstart;
	ULONG selend;

	SelectionColumns(line, nTextLen, &selstart, &selend);

	//
	//	STEP 1. Apply the "base coat"
	//
	for(i = 0; i < nTextLen; i++)
	{
		attr[i].len		 = 1;
		attr[i].font	 = 0;
		attr[i].eol		 = 0;
		attr[i].reserved = 0;

		// change the background if the line is too long
		if(nColumn >= (ULONG)m_nLongLineLimit && CheckStyle(TXS_LONGLINES))
		{
			attr[i].fg = GetColour(TXC_FOREGROUND);
			attr[i].bg = LongColour(line);
		}
		else
		{
			attr[i].fg = GetColour(TXC_FOREGROUND);
			attr[i].bg = LineColour(line);//GetColour(TXC_BACKGROUND);
		}

		// keep track of how many columns we have processed
		if(szText[i] == '\t')
			nColumn += m_nTabWidthChars - (nColumn % m_nTabWidthChars);
		else
			nColumn += 1;
	}

	//
	//	TODO: 1. Apply syntax colouring first of all
	//

    // Changed text precedes search and selection, which keep their colours.
    if (CheckStyle(TXS_HIGHLIGHTCHANGES))
    {
        LineDiff(
            line,
            [&](size_w column, size_w length)
            {
                size_w end = min(column + length, (size_w)nTextLen);

                for (size_w changed = column; changed < end; ++changed)
                {
                    attr[changed].fg = GetColour(TXC_CHANGEDTEXT);
                }
            },
            nTextLen);
    }

	//
	//	STEP 2. Highlight the search's matches (overriding syntax colouring)
	//
	HighlightMatches(szText, nTextLen, attr);

	//
	//	STEP 3:  Now apply text-selection (overrides everything else)
	//
	if(m_nSelectionType == SEL_NORMAL)
	{
		for(i = 0; i < nTextLen; i++)
		{
			// highlight uses a separate attribute-flag
			if((ULONG)i >= selstart && (ULONG)i < selend)
				attr[i].sel = 1;
			else
				attr[i].sel = 0;
		}
	}
	else if(m_nSelectionType == SEL_BLOCK)
	{
	}

	//SyntaxColour(szText, nTextLen, attr);

	//
	//	Turn a CR/LF at the end of a line that matches the line format into a single
	//	'space' character. Any other line ending is left to show as control characters,
	//	as is every line ending when they are all being shown.
	//
	if(!CheckStyle(TXS_SHOWLINEENDINGS))
		nTextLen = StripCRLF(szText, attr, nTextLen, false);

	//
	//	Finally identify control-characters (after CR/LF has been changed to 'space')
	//


	for(i = 0; i < nTextLen; i++)
	{
		ULONG ch = szText[i];
		attr[i].ctrl	= ch < 0x20 ? 1 : 0;
		if(ch == '\r' || ch == '\n')
			attr[i].eol=TRUE;
	}

	return nTextLen;
}

int TextView::CRLF_size(TCHAR *szText, int nLength)
{
	if(nLength >= 2)
	{
		if(szText[nLength-2] == '\r' && szText[nLength-1] == '\n') 
			return 2;
	}

	if(nLength >= 1)
	{
		if(szText[nLength-1] == '\r' || szText[nLength-1] == '\n')
			return 1;
	}	

	return 0;
}

//
//	Strip CR/LF combinations from the end of a line and
//  replace with a single space character (for drawing purposes)
//
int TextView::StripCRLF(TCHAR *szText, ATTR *attr, int nLength, bool fAllow)
{
	if(nLength >= 2)
	{
		if(szText[nLength-2] == '\r' && szText[nLength-1] == '\n') 
		{
			attr[nLength-2].eol = TRUE;

			if(m_nCRLFMode & TXL_CRLF)
			{
				// convert CRLF to a single space
				szText[nLength-2] = ' ';
				return nLength - 1 - (int)fAllow;
			}
			else
			{
				return nLength;
			}
		}
	}
	
	if(nLength >= 1)
	{
		if(szText[nLength-1] == '\r')
		{
			attr[nLength-1].eol = TRUE;

			if(m_nCRLFMode & TXL_CR)
			{
				szText[nLength-1] = ' ';
				return nLength - (int)fAllow;
			}
		}

		if(szText[nLength-1] == '\n')
		{
			attr[nLength-1].eol = TRUE;

			if(m_nCRLFMode & TXL_LF)
			{
				szText[nLength-1] = ' ';
				return nLength - (int)fAllow;
			}
		}
	}
	
	return nLength;
}

//
//
//
COLORREF TextView::LineColour(TextCoord *line)
{
	if(line && line->line_begin == m_cursorPos.line_begin && CheckStyle(TXS_HIGHLIGHTCURLINE))
		return GetColour(TXC_CURRENTLINE);
	else
		return GetColour(TXC_BACKGROUND);
}

COLORREF TextView::LongColour(TextCoord *line)
{
	if(line->line_begin == m_cursorPos.line_begin && CheckStyle(TXS_HIGHLIGHTCURLINE))
		return GetColour(TXC_CURRENTLINE);
	else
		return GetColour(TXC_LONGLINE);
}

COLORREF MixRGB(COLORREF rgbCol1, COLORREF rgbCol2)
{
	return RGB(
		(GetRValue(rgbCol1) + GetRValue(rgbCol2)) / 2,
		(GetGValue(rgbCol1) + GetGValue(rgbCol2)) / 2,
		(GetBValue(rgbCol1) + GetBValue(rgbCol2)) / 2
		);
}

COLORREF RealizeColour(COLORREF col)
{
	COLORREF result = col;

	if(col & 0x80000000)
		result = GetSysColor(col & 0xff);
	
	if(col & 0x40000000)
		result = MixRGB(GetSysColor((col & 0xff00) >> 8), result);

	if(col & 0x20000000)
		result = MixRGB(GetSysColor((col & 0xff00) >> 8), result);

	return result;
}

//
//	Return an RGB value corresponding to the specified HVC_xxx index
//
//	If the RGB value has the top bit set (0x80000000) then it is
//  not a real RGB value - instead the low 29bits specify one
//  of the GetSysColor COLOR_xxx indices. This allows us to use
//	system colours without worrying about colour-scheme changes etc.
//
COLORREF TextView::GetColour(UINT idx)
{
	if(idx >= TXC_MAX_COLOURS)
		return 0;

	return REALIZE_SYSCOL(m_rgbColourList[idx]);
}

COLORREF TextView::SetColour(UINT idx, COLORREF rgbColour)
{
	COLORREF rgbOld;

	if(idx >= TXC_MAX_COLOURS)
		return 0;
	
	rgbOld				 = m_rgbColourList[idx];
	m_rgbColourList[idx] = rgbColour;

	ResetLineCache();

	return rgbOld;
}



#include <uxtheme.h>
#include <vssym32.h>


//
//	Need to custom-draw the non-client area when using XP/Vista themes,
//	otherwise the border looks old-style
//
LONG TextView::OnNcPaint(HRGN hrgnUpdate)
{
	HRGN hrgnClip = hrgnUpdate;

	if(m_hTheme != 0)
	{
		HDC hdc = GetWindowDC(m_hWnd);//GetDCEx(m_hWnd, GetWindowDC(m_hWnd);
		RECT rc;
		RECT rcWindow;
		DWORD state = ETS_NORMAL;
		
		if(!IsWindowEnabled(m_hWnd))
			state = ETS_DISABLED;
		else if(GetFocus() == m_hWnd)
			state = ETS_HOT;
		else
			state = ETS_NORMAL;
		
		GetWindowRect(m_hWnd, &rcWindow);
		GetClientRect(m_hWnd, &rc);
		ClientToScreen(m_hWnd, (POINT *)&rc.left);
		ClientToScreen(m_hWnd, (POINT *)&rc.right);
		rc.right = rcWindow.right - (rc.left - rcWindow.left);
		rc.bottom = rcWindow.bottom - (rc.top - rcWindow.top);
		
		hrgnClip = CreateRectRgn(rc.left, rc.top, rc.right, rc.bottom);
		
		if(hrgnUpdate != (HRGN)1)
			CombineRgn(hrgnClip, hrgnClip, hrgnUpdate, RGN_AND);
		
		OffsetRect(&rc, -rcWindow.left, -rcWindow.top);
		
		ExcludeClipRect(hdc, rc.left, rc.top, rc.right, rc.bottom);
		OffsetRect(&rcWindow, -rcWindow.left, -rcWindow.top);
		
		//if (IsThemeBackgroundPartiallyTransparent (hTheme, EP_EDITTEXT, state))
		//	DrawThemeParentBackground(m_hWnd, hdc, &rcWindow);
		
		DrawThemeBackground(m_hTheme, hdc, 
			6,
			state,
			//EP_EDITTEXT, 
			//state, 
			//3,0,
			&rcWindow, NULL);
		
		ReleaseDC(m_hWnd, hdc);
	}

	return (LONG)DefWindowProc(m_hWnd, WM_NCPAINT, (WPARAM)hrgnClip, 0);	
}
