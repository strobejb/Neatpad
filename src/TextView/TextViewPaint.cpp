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

void	PaintRect(HDC hdc, int x, int y, int width, int height, COLORREF fill);
void	PaintRect(HDC hdc, RECT *rect, COLORREF fill);
void	DrawCheckedRect(HDC hdc, RECT *rect, COLORREF fg, COLORREF bg);

extern "C" COLORREF MixRGB(COLORREF, COLORREF);


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
			TextCoord nextCoord;

			haveLine = m_pTextDoc->next_line_from_coord(&lineCoord, 1, &nextCoord) && nextCoord.line_begin != lineCoord.line_begin;
			lineCoord = nextCoord;
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

		if(1) width += 1;
		if(0) width += 5;
		
		return width;
	}
	// selection margin by itself
	else if(CheckStyle(TXS_SELMARGIN))
	{
		width += cx + 4;

		if(0) width += 1;
		if(0) width += 5;

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
	int len = wsprintf(buf, LINENO_FMT, (unsigned __int64)m_nLineCount);

	m_nLinenoWidth = TextWidth(hdc, buf, len);

	SelectObject(hdc, hOldFont);
	ReleaseDC(m_hWnd, hdc);
}

//
//	Draw the specified line's margin into the area described by *margin*
//
int TextView::PaintMargin(HDC hdc, TextCoord *line, int xpos, int ypos)
{
	RECT	rect = { xpos, ypos, xpos + LeftMarginWidth(), ypos + m_nLineHeight };
	bool	exact = false;
	size_w	nLineNo = line ? m_pTextDoc->lineno_from_coord(line, &exact) : 0;

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
		
		int  len   = wsprintf(ach, LINENO_FMT, (unsigned __int64)(nLineNo + 1));
		int	 width = TextWidth(hdc, ach, len);

		// only draw line number if in-range and exact
		if(!exact || nLineNo >= m_nLineCount)
			len = 0;

		rect.right  = rect.left + m_nLinenoWidth;

		if(CheckStyle(TXS_SELMARGIN) && m_hImageList)
		{
			imgX = rect.right;
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

		// vertical line
		rect.left   = rect.right;
		rect.right += 1;
		//PaintRect(hdc, &rect, MixRGB(GetSysColor(COLOR_3DFACE), 0xffffff));
		PaintRect(hdc, &rect, GetColour(TXC_BACKGROUND));

		// bleed area - use this to draw "folding" arrows
		/*rect.left   = rect.right;
		rect.right += 5;
		PaintRect(hdc, &rect, GetColour(TXC_BACKGROUND));*/

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
	
	return rect.right-rect.left;
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
	if(GetFocus() == m_hWnd)
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

	//
	//	TODO: 2. Apply bookmarks, line highlighting etc (override syntax colouring)
	//

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
	//	Turn any CR/LF at the end of a line into a single 'space' character
	//
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
