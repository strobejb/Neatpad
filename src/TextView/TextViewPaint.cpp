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
#include "Unicode.h"

void	PaintRect(HDC hdc, int x, int y, int width, int height, COLORREF fill);
void	PaintRect(HDC hdc, RECT *rect, COLORREF fill);
void	DrawCheckedRect(HDC hdc, RECT *rect, COLORREF fg, COLORREF bg);

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

static void PaintDeletionTriangle(HDC hdc, int x, int y, int width, int height, COLORREF colour)
{
    int middle = height / 2;

    for (int row = 0; row < height; ++row)
    {
        int distance = row > middle ? row - middle : middle - row;
        int extent   = middle ? 1 + (width - 1) * (middle - distance) / middle : width;

        PaintRect(hdc, x, y + row, extent, 1, colour);
    }
}

//
//	Perform a full redraw of the entire window
//
VOID TextView::RefreshWindow()
{
	InvalidateRect(m_hWnd, NULL, FALSE);
}

//
//	Return the analyzed Uniscribe data for the line containing coord. Entries are
//	keyed by the line's starting byte; ResetLineCache flushes them on every edit.
//
USPCACHE *TextView::GetUspCache(HDC hdc, TextCoord *coord)
{
	TCHAR	*buff;
	ATTR	*attr;
	ULONG	 buflen;
	ULONG	 colno = 0;
	int		 len;
	int		 columns;
	SIZE	 size;
	HDC		 hdcTemp;

	USPDATA *uspData;
	ULONG    lru_usage = -1;
	int		 lru_index = 0;

	if(coord == 0)
		return 0;

	//
	//	Search the cache to see if we've already analyzed the requested line
	//
	for(int i = 0; i < USP_CACHE_SIZE; i++)
	{
		// remember the least-recently used
		if(m_uspCache[i].usage < lru_usage)
		{
			lru_index = i;
			lru_usage = m_uspCache[i].usage;
		}

		if(m_uspCache[i].usage > 0 && m_uspCache[i].line_begin == coord->line_begin)
		{
			m_uspCache[i].usage++;
			return &m_uspCache[i];
		}
	}

	//
	// not found? overwrite the "least-recently-used" entry
	//
	m_uspCache[lru_index].line_begin = coord->line_begin;
	m_uspCache[lru_index].usage		= 1;
	uspData = m_uspCache[lru_index].uspData;

	if(hdc == 0)	hdcTemp = GetDC(m_hWnd);
	else			hdcTemp = hdc;

	//
	// get the text for the entire line (up to the layout limit) and apply style
	// attributes. A line never decodes to more UTF-16 units than it has bytes.
	//
	buflen = (ULONG)min(coord->line_next - coord->line_begin, (size_w)LINE_LAYOUT_LIMIT);
	buff   = new TCHAR[buflen + 1];
	attr   = new ATTR[buflen + 1];

	len = m_pTextDoc->getline(*coord, buff, buflen);

	// cache the line's length information
	m_uspCache[lru_index].length		= len;
	m_uspCache[lru_index].length_CRLF	= len - CRLF_size(buff, len);

	len = ApplyTextAttributes(coord, colno, buff, len, attr);

	//
	// setup the tabs + itemization states
	//
	int				tablist[]		= { m_nTabWidthChars };
	SCRIPT_TABDEF	tabdef			= { 1, 0, tablist, 0 };
	SCRIPT_CONTROL	scriptControl	= { 0 };
	SCRIPT_STATE	scriptState		= { 0 };

	//SCRIPT_DIGITSUBSTITUTE scriptDigitSub;
	//ScriptRecordDigitSubstitution(LOCALE_USER_DEFAULT, &scriptDigitSub);
	//ScriptApplyDigitSubstitution(&scriptDigitSub, &scriptControl, &scriptState);

	//
	// go!
	//
	UspAnalyze(
		uspData,
		hdcTemp,
		buff,
		len,
		attr,
		0,
		m_uspFontList,
		&scriptControl,
		&scriptState,
		&tabdef
	);

	//
	//	Apply the selection
	//
	ApplySelection(uspData, coord, len);

	//
	//	The widest line laid out so far sets the horizontal scrolling range.
	//	It only ever grows, with a column to spare for the caret at the end.
	//
	UspGetSize(uspData, &size);
	columns = (size.cx + m_nFontWidth - 1) / m_nFontWidth + 1;

	if(columns > m_nLongestLine)
	{
		m_nLongestLine = columns;
		SetupScrollbars();
	}

	if(hdc == 0)
		ReleaseDC(m_hWnd, hdcTemp);

	delete[] buff;
	delete[] attr;

	return &m_uspCache[lru_index];
}

//
//	Return a fully-analyzed USPDATA object for the specified line
//
USPDATA *TextView::GetUspData(HDC hdc, TextCoord *coord)
{
	USPCACHE *uspCache = GetUspCache(hdc, coord);

	if(uspCache)
		return uspCache->uspData;
	else
		return 0;
}

//
//	Invalidate every entry in the cache so we can start afresh
//
void TextView::ResetLineCache()
{
	for(int i = 0; i < USP_CACHE_SIZE; i++)
	{
		m_uspCache[i].usage	= 0;
	}
}

//
//	Painting procedure for TextView objects
//
LONG TextView::OnPaint()
{
	PAINTSTRUCT ps;
	ULONG		row;
	ULONG		first;
	ULONG		last;
	TextCoord	lineCoord;
	LineIterator lines;
	bool		haveLine;

	HRGN		hrgnUpdate;
	HDC			hdcMem;
	HBITMAP		hbmMem;
	RECT		rect;

	//
	// get update region *before* BeginPaint validates the window
	//
	hrgnUpdate = CreateRectRgn(0,0,1,1);
	GetUpdateRgn(m_hWnd, hrgnUpdate, FALSE);

	//
	// create a memoryDC the same size a single line, for double-buffering
	//
	BeginPaint(m_hWnd, &ps);
	GetClientRect(m_hWnd, &rect);

	hdcMem = CreateCompatibleDC(ps.hdc);
	hbmMem = CreateCompatibleBitmap(ps.hdc, rect.right-rect.left, m_nLineHeight);

	SelectObject(hdcMem, hbmMem);

	//
	// figure out which lines to redraw
	//
	first = ps.rcPaint.top    / m_nLineHeight;
	last  = ps.rcPaint.bottom / m_nLineHeight;

	// rows past the end of the document have no line
	haveLine = ViewportLineFromRow(first, &lineCoord);
	lines = m_pTextDoc->line_at(lineCoord);

	//
	// draw the display line-by-line
	//
	for(row = first; row <= last; row++)
	{
		int sx		= 0;
		int sy		= row * m_nLineHeight;
		int width	= rect.right-rect.left;
		TextCoord *line = haveLine ? &lineCoord : 0;

		// prep the background
		PaintRect(hdcMem, 0, 0, width, m_nLineHeight, LineColour(line));
		//PaintRect(hdcMem, m_cpBlockStart.xpos+LeftMarginWidth(), 0, m_cpBlockEnd.xpos-m_cpBlockStart.xpos, m_nLineHeight,GetColour(TXC_HIGHLIGHT));

		// draw each line into the offscreen buffer
		PaintLine(hdcMem, line, row, -m_nHScrollPos * m_nFontWidth, 0, hrgnUpdate);

		// transfer to screen 
		BitBlt(	ps.hdc, sx, sy, width, m_nLineHeight, hdcMem, 0, 0, SRCCOPY);

		// step down to the next line, if there is one
		if(haveLine)
		{
			haveLine  = lines.next();
			lineCoord = lines.line();
		}
	}

	//
	//	Cleanup
	//
	EndPaint(m_hWnd, &ps);

	DeleteDC(hdcMem);
	DeleteObject(hbmMem);
	DeleteObject(hrgnUpdate);

	return 0;
}

//
//	Draw the specified line (including margins etc) to the specified location.
//	coord is 0 for rows past the end of the document.
//
void TextView::PaintLine(HDC hdc, TextCoord *coord, ULONG row, int xpos, int ypos, HRGN hrgnUpdate)
{
	RECT	bounds;
	HRGN	hrgnBounds = NULL;

	GetClientRect(m_hWnd, &bounds);
	SelectClipRgn(hdc, NULL);

	// no point in drawing outside the window-update-region
	if(hrgnUpdate != NULL)
	{
		// work out where the line would have been on-screen
		bounds.left     = (long)(-m_nHScrollPos * m_nFontWidth + LeftMarginWidth());
		bounds.top		= (long)(row * m_nLineHeight);
		bounds.right	= (long)(bounds.right);
		bounds.bottom	= (long)(bounds.top + m_nLineHeight);
		
		//	clip the window update-region with the line's bounding rectangle
		hrgnBounds = CreateRectRgnIndirect(&bounds);
		CombineRgn(hrgnBounds, hrgnUpdate, hrgnBounds, RGN_AND);
		
		// work out the bounding-rectangle of this intersection
		GetRgnBox(hrgnBounds, &bounds);
		bounds.top		= 0;
		bounds.bottom	= m_nLineHeight;
	}

	PaintText(hdc, coord, xpos + LeftMarginWidth(), ypos, &bounds);

	if(hrgnBounds)
		DeleteObject(hrgnBounds);
	SelectClipRgn(hdc, NULL);

	//
	//	draw the margin straight over the top
	//
	if(LeftMarginWidth() > 0)
	{
		PaintMargin(hdc, coord, 0, 0);
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
int TextView::PaintMargin(HDC hdc, TextCoord *line, int xpos, int ypos)
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
		HANDLE hOldFont = SelectObject(hdc, m_uspFontList[0].hFont);

        // One continuous background for numbers, icons and change markers.
        PaintRect(hdc, &rect, GetColour(TXC_LINENUMBER));

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
        int width = TextWidth(hdc, ach, len);

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
		
		SetTextColor(hdc, GetColour(TXC_LINENUMBERTEXT));
		SetBkColor(hdc,   GetColour(TXC_LINENUMBER));

		ExtTextOut(	hdc, 
					rect.left + m_nLinenoWidth - width,
					rect.top  + NeatTextYOffset(&m_uspFontList[0]),
					ETO_OPAQUE | ETO_CLIPPED,
					&rect,
					ach,
					len,
					0);

		SelectObject(hdc, hOldFont);
	}
	else
	{
		DrawCheckedRect(hdc, &rect, GetColour(TXC_SELMARGIN1), GetColour(TXC_SELMARGIN2));
	}

	//
	//	Retrieve information about this specific line
	//
	LINEINFO *linfo = exact && nLineNo <= (size_w)ULONG_MAX ? GetLineInfo((ULONG)nLineNo) : 0;

	if(m_hImageList && linfo && nLineNo < m_nLineCount)
	{
		ImageList_DrawEx(
					  m_hImageList,
					  linfo->nImageIdx,
					  hdc, 
					  imgX,
					  imgY,
					  imgWidth,
					  imgHeight,
					  CLR_NONE,
					  CLR_NONE,
					  ILD_TRANSPARENT
					  );
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

            PaintRect(hdc, markerLeft, ypos, CHANGE_MARKER_WIDTH, m_nLineHeight, GetColour(TXC_CHANGEDLINEBG));
            PaintRect(hdc, markerLeft, ypos, 1, m_nLineHeight, colour);
            PaintRect(hdc, markerRight - 1, ypos, 1, m_nLineHeight, colour);

            if (!DiffJoins(line, true))
            {
                PaintRect(hdc, markerLeft, ypos, CHANGE_MARKER_WIDTH, 1, colour);
            }

            if (!DiffJoins(line, false))
            {
                PaintRect(hdc, markerLeft, ypos + m_nLineHeight - 1, CHANGE_MARKER_WIDTH, 1, colour);
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
                PaintDeletionTriangle(hdc, triangleLeft, ypos, CHANGE_TRIANGLE_WIDTH, triangle,
                                      GetColour(TXC_CHANGEDTEXT));
            }

            if (diff & TextDocument::diff_erased_after)
            {
                PaintDeletionTriangle(hdc, triangleLeft, ypos + m_nLineHeight - triangle, CHANGE_TRIANGLE_WIDTH,
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
void TextView::PaintText(HDC hdc, TextCoord *coord, int xpos, int ypos, RECT *bounds)
{
	USPDATA * uspData;

	// grab the USPDATA for this line
	uspData = GetUspData(hdc, coord);

	if(uspData == 0)
		return;

	// set highlight-colours depending on window-focus
	if(HasAppFocus(m_hWnd))
		UspSetSelColor(uspData, GetColour(TXC_HIGHLIGHTTEXT), GetColour(TXC_HIGHLIGHT));
	else
		UspSetSelColor(uspData, GetColour(TXC_HIGHLIGHTTEXT2), GetColour(TXC_HIGHLIGHT2));

	// update selection-attribute information for the line
	ULONG selStart, selEnd;

	SelectionColumns(coord, uspData->stringLen, &selStart, &selEnd);
	UspApplySelection(uspData, selStart, selEnd);

	ApplySelection(uspData, coord, uspData->stringLen);

	// draw the text!
	UspTextOut(uspData, hdc, xpos, ypos, m_nLineHeight, m_nHeightAbove, bounds);
}

int	TextView::ApplySelection(USPDATA *uspData, TextCoord *line, ULONG nTextLen)
{
	int selstart = 0;
	int selend   = 0;

	if(m_nSelectionType != SEL_BLOCK)
		return 0;

	if(line->line_begin >= m_cpBlockStart.line_begin && line->line_begin <= m_cpBlockEnd.line_begin)
	{
		int trailing;
		
		UspXToOffset(uspData, m_cpBlockStart.xpos, &selstart, &trailing, 0);
		selstart += trailing;
		
		UspXToOffset(uspData, m_cpBlockEnd.xpos, &selend, &trailing, 0);
		selend += trailing;

		if(selstart > selend)
			selstart ^= selend ^= selstart^= selend;
	}

	UspApplySelection(uspData, selend, selstart);

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

void PaintRect(HDC hdc, int x, int y, int width, int height, COLORREF fill)
{
	RECT rect = { x, y, x+width, y+height };

	fill = SetBkColor(hdc, fill);
	ExtTextOut(hdc, 0, 0, ETO_OPAQUE, &rect, 0, 0, 0);
	SetBkColor(hdc, fill);
}

void PaintRect(HDC hdc, RECT *rect, COLORREF fill)
{
	fill = SetBkColor(hdc, fill);
	ExtTextOut(hdc, 0, 0, ETO_OPAQUE, rect, 0, 0, 0);	
	SetBkColor(hdc, fill);
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

//
//	Paint a checkered rectangle, with each alternate
//	pixel being assigned a different colour
//
void DrawCheckedRect(HDC hdc, RECT *rect, COLORREF fg, COLORREF bg)
{
	static WORD wCheckPat[8] = 
	{ 
		0xaaaa, 0x5555, 0xaaaa, 0x5555, 0xaaaa, 0x5555, 0xaaaa, 0x5555 
	};

	HBITMAP hbmp;
	HBRUSH  hbr, hbrold;
	COLORREF fgold, bgold;

	hbmp = CreateBitmap(8, 8, 1, 1, wCheckPat);
	hbr  = CreatePatternBrush(hbmp);

	SetBrushOrgEx(hdc, rect->left, 0, 0);
	hbrold = (HBRUSH)SelectObject(hdc, hbr);

	fgold = SetTextColor(hdc, fg);
	bgold = SetBkColor(hdc, bg);
	
	PatBlt(hdc, rect->left, rect->top, 
				rect->right - rect->left, 
				rect->bottom - rect->top, 
				PATCOPY);
	
	SetBkColor(hdc, bgold);
	SetTextColor(hdc, fgold);
	
	SelectObject(hdc, hbrold);
	DeleteObject(hbr);
	DeleteObject(hbmp);
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
