//
//	MODULE:		TextViewPaint.cpp
//
//	PURPOSE:	Painting and display for the TextView control
//
//	NOTES:		www.catch22.net
//

#include <windows.h>
#include <tchar.h>
#include "TextView.h"
#include "TextViewInternal.h"

//
//	Painting procedure for TextView objects
//
LONG TextView::OnPaint()
{
	PAINTSTRUCT ps;

	ULONG i, first, last;

	BeginPaint(m_hWnd, &ps);

	// select which font we will be using
	SelectObject(ps.hdc, m_hFont);

	// figure out which lines to redraw
	first = ps.rcPaint.top    / m_nFontHeight;
	last  = ps.rcPaint.bottom / m_nFontHeight;

	// make sure we never wrap around the 4gb boundary
	if(last < first) last = -1;

	// draw the display line-by-line
	for(i = first; i <= last; i++)
	{
		PaintLine(ps.hdc, i);
	}

	EndPaint(m_hWnd, &ps);

	return 0;
}

//
//	Emulates ExtTextOut, but draws text using tabs using TabbedTextOut
//
void TextView::TabbedExtTextOut(HDC hdc, RECT *rect, TCHAR *buf, int len)
{
	int  tab = 4 * m_nFontWidth;
	int  width;
	RECT fill = *rect;

	// Draw line and expand tabs
	width = TabbedTextOut(hdc, rect->left, rect->top, buf, len, 1, &tab, rect->left);
	
	// Erase the rest of the line with the background colour
	fill.left += LOWORD(width);
	ExtTextOut(hdc, 0, 0, ETO_OPAQUE, &fill, 0, 0, 0);
}

//
//	Draw the specified line
//
void TextView::PaintLine(HDC hdc, ULONG nLineNo)
{
	RECT  rect;
	
	TCHAR buf[LONGEST_LINE];
	int   len;

	// Get the area we want to update
	GetClientRect(m_hWnd, &rect);

	// calculate rectangle for entire length of line in window
	rect.top		= nLineNo  * m_nFontHeight;
	rect.bottom		= rect.top + m_nFontHeight;

	// check we have data to draw on this line
	if(nLineNo >= m_nLineCount)
	{
		SetBkColor(hdc, GetTextViewColor(HVC_BACKGROUND));
		ExtTextOut(hdc, 0, 0, ETO_OPAQUE, &rect, 0, 0, 0);
		return;
	}

	// get the data for this single line of text
	len = m_pTextDoc->getline(nLineNo, buf, LONGEST_LINE);

	// set the colours
	SetTextColor(hdc, GetTextViewColor(HVC_FOREGROUND));
	SetBkColor(hdc,   GetTextViewColor(HVC_BACKGROUND));

	// draw text and fill line background at the same time
	TabbedExtTextOut(hdc, &rect, buf, len);
}

//
//	Return an RGB value corresponding to the specified HVC_xxx index
//
COLORREF TextView::GetTextViewColor(UINT idx)
{
	switch(idx)
	{
	case HVC_BACKGROUND: return GetSysColor(COLOR_WINDOW);
	case HVC_FOREGROUND: return GetSysColor(COLOR_WINDOWTEXT);
	default:			 return 0;
	}
}