//
//	MODULE:		TextViewMouse.cpp
//
//	PURPOSE:	Mouse and caret support for the TextView control
//
//	NOTES:		www.catch22.net
//

#include <windows.h>
#include <tchar.h>
#include "TextView.h"
#include "TextViewInternal.h"

int ScrollDir(int counter, int dir);

//
//	WM_MOUSEACTIVATE
//
//	Grab the keyboard input focus 
//	
LONG TextView::OnMouseActivate(HWND hwndTop, UINT nHitTest, UINT nMessage)
{
	SetFocus(m_hWnd);
	return MA_ACTIVATE;
}

//
//	WM_LBUTTONDOWN
//
//  Position caret to nearest text character under mouse
//
LONG TextView::OnLButtonDown(UINT nFlags, int mx, int my)
{
	ULONG nLineNo;
	ULONG nCharOff;
	ULONG nFileOff;
	int   px;
	
	// map the mouse-coordinates to a real file-offset-coordinate
	MouseCoordToFilePos(mx, my, &nLineNo, &nCharOff, &nFileOff, &px);

	SetCaretPos(px, (nLineNo - m_nVScrollPos) * m_nLineHeight);

	// reset cursor and selection offsets to the same location
	m_nSelectionStart	= nFileOff;
	m_nSelectionEnd		= nFileOff;
	m_nCursorOffset		= nFileOff;

	// set capture for mouse-move selections
	m_fMouseDown = true;
	SetCapture(m_hWnd);
	RefreshWindow();

	return 0;
}

//
//	WM_LBUTTONUP 
//
//	Release capture and cancel any mouse-scrolling
//
LONG TextView::OnLButtonUp(UINT nFlags, int mx, int my)
{
	if(m_fMouseDown)
	{
		m_fMouseDown = false;
		ReleaseCapture();
		RefreshWindow();
	}

	return 0;
}

//
//	WM_MOUSEMOVE
//
//	Set the selection end-point if we are dragging the mouse
//
LONG TextView::OnMouseMove(UINT nFlags, int mx, int my)
{
	if(m_fMouseDown)
	{
		ULONG	nLineNo, nCharOff, nFileOff;
		int		cx;

		// get new cursor offset+coordinates
		MouseCoordToFilePos(mx, my, &nLineNo, &nCharOff, &nFileOff, &cx);

		// update the region of text that has changed selection state
		if(m_nSelectionEnd != nFileOff)
		{
			// redraw from old selection-pos to new position
			InvalidateRange(m_nSelectionEnd, nFileOff);

			SetCaretPos(cx, (nLineNo - m_nVScrollPos) * m_nLineHeight);

			// adjust the cursor + selection *end* to the new offset
			m_nSelectionEnd = nFileOff;
			m_nCursorOffset	= nFileOff;
		}
	}

	return 0;
}


//
//	Convert mouse(client) coordinates to a file-relative offset
//
//	Currently only uses the main font so will not support other
//	fonts introduced by syntax highlighting
//
BOOL TextView::MouseCoordToFilePos(	int		 mx,			// [in]  mouse x-coord
									int		 my,			// [in]  mouse x-coord
									ULONG	*pnLineNo,		// [out] line number
									ULONG	*pnCharOffset,	// [out] char-offset from start of line
									ULONG	*pfnFileOffset, // [out] zero-based file-offset
									int		*px				// [out] adjusted x coord of caret
									)
{
	ULONG nLineNo;

	ULONG charoff = 0;
	ULONG fileoff = 0;

	TCHAR buf[TEXTBUFSIZE];
	int   len;
	int	  curx = 0;
	RECT  rect;

	// get scrollable area
	GetClientRect(m_hWnd, &rect);
	rect.bottom -= rect.bottom % m_nLineHeight;
		
	// clip mouse to edge of window
	if(mx < 0)				mx = 0;
	if(my < 0)				my = 0;
	if(my >= rect.bottom)	my = rect.bottom - 1;
	if(mx >= rect.right)	mx = rect.right  - 1;

	// It's easy to find the line-number: just divide 'y' by the line-height
	nLineNo = (my / m_nLineHeight) + m_nVScrollPos;
	
	// make sure we don't go outside of the document
	if(nLineNo >= m_nLineCount)
	{
		nLineNo = m_nLineCount ? m_nLineCount - 1 : 0;
		fileoff = m_pTextDoc->size();
	}

	HDC    hdc      = GetDC(m_hWnd);
	HANDLE hOldFont = SelectObject(hdc, m_FontAttr[0].hFont);

	*pnCharOffset = 0;

	mx += m_nHScrollPos * m_nFontWidth;

	// character offset within the line is more complicated. We have to 
	// parse the text.
	for(;;)
	{
		// grab some text
		if((len = m_pTextDoc->getline(nLineNo, charoff, buf, TEXTBUFSIZE, &fileoff)) == 0)
			break;

		int tlen=len;

		if(len > 1 && buf[len-2] == '\r')
		{
			buf[len-2]=0;
			len-=2;
		}

		// find it's width
		int width = NeatTextWidth(hdc, buf, len, -(curx % TabWidth()));

		// does cursor fall within this segment?
		if(mx >= curx && mx < curx + width)
		{ 
			//
			//	We have a range of text, with the mouse 
			//  somewhere in the middle. Perform a "binary chop" to
			//  locate the exact character that the mouse is positioned over
			//
			int low   = 0;
			int high  = len;
			int lowx  = 0;
			int highx = width;
	
			while(low < high - 1)
			{
				int newlen   = (high - low) / 2;

				width = NeatTextWidth(hdc, buf + low, newlen, -lowx-curx);

				if(mx-curx < width + lowx)
				{
					high  = low + newlen;
					highx = lowx + width;
				}
				else
				{
					low   = low + newlen;
					lowx  = lowx + width;
				}
			}

			// base coordinates on centre of characters, not the edges
			if(mx - curx > highx - m_nFontWidth/2)
			{
				curx    += highx;
				charoff += high;
			}
			else
			{
				curx    += lowx;
				charoff += low;
			}
			
			*pnCharOffset = charoff;
			break;
		}

		curx			+= width;
		charoff			+= tlen;
		*pnCharOffset	+= len;
	}

	SelectObject(hdc, hOldFont);
	ReleaseDC(m_hWnd, hdc);


	*pnLineNo = nLineNo;
	//*pnCharOffset=charoff;
	*pfnFileOffset=fileoff+*pnCharOffset;
	*px = curx - m_nHScrollPos *m_nFontWidth;

	return 0;
}

//
//	Redraw the specified range of text/data in the control
//
LONG TextView::InvalidateRange(ULONG nStart, ULONG nFinish)
{
	ULONG start  = min(nStart, nFinish);
	ULONG finish = max(nStart, nFinish);

	ULONG lineno, charoff, linelen, lineoff;
	int   xpos1 = 0, xpos2 = 0;
	int  ypos;

	HDC hdc = GetDC(m_hWnd);
	SelectObject(hdc, m_FontAttr[0].hFont);

	RECT rect, client;
	GetClientRect(m_hWnd, &client);

	// nothing to do?
	if(start == finish)
		return 0;

	//
	//	Find the line number and character offset of specified file-offset
	//
	if(!m_pTextDoc->offset_to_line(start, &lineno, &charoff))
		return 0;

	// clip to top of window
	if(lineno < m_nVScrollPos)
	{
		lineno	= m_nVScrollPos;
		charoff	= 0;
		xpos1	= 0;
	}

	if(!m_pTextDoc->getlineinfo(lineno, &lineoff, &linelen))
		return 0;

	ypos = (lineno - m_nVScrollPos) * m_nLineHeight;

	// selection starts midline...
	if(charoff != 0)
	{
		ULONG off = 0;
		TCHAR buf[TEXTBUFSIZE];
		int   len   = charoff;
		int   width = 0;
		
		// loop until we get on-screen
		while(off < charoff)
		{
			int tlen = min(len, TEXTBUFSIZE);
			tlen     = m_pTextDoc->getline(lineno, off, buf, tlen, 0);

			len		-= tlen;
			off		+= tlen;
			
			width	 = NeatTextWidth(hdc, buf, tlen, -(xpos1 % TabWidth()));
			xpos1   += width;

			if(tlen == 0)
				break;
		}

		// xpos now equals the start of range
	}

	// Invalidate any lines that aren't part of the last line
	while(finish >= lineoff + linelen)
	{
		SetRect(&rect, xpos1, ypos, client.right, ypos + m_nLineHeight);
		rect.left -= m_nHScrollPos * m_nFontWidth;
			
		//InvertRect(hdc, &rect);
		InvalidateRect(m_hWnd, &rect, FALSE);

		xpos1	= 0;
		charoff = 0;
		ypos   += m_nLineHeight;
			
		// get next line 
		if(!m_pTextDoc->getlineinfo(++lineno, &lineoff, &linelen))
			break;
	}

	// erase up to the end of selection
	ULONG offset = lineoff + charoff;
	
	xpos2 = xpos1;
	
	TCHAR buf[TEXTBUFSIZE];
	int width;
	
	while(offset < finish)
	{
		int tlen = min((finish - offset), TEXTBUFSIZE);
		tlen     = m_pTextDoc->getdata(offset, buf, tlen);
		
		offset += tlen;
		
		width	 = NeatTextWidth(hdc, buf, tlen, -(xpos2 % TabWidth()));
		xpos2   += width;
	}
	
	SetRect(&rect, xpos1, ypos, xpos2, ypos + m_nLineHeight);
	OffsetRect(&rect, -m_nHScrollPos * m_nFontWidth, 0);

	//InvertRect(hdc, &rect);
	InvalidateRect(m_hWnd, &rect, FALSE);

	ReleaseDC(m_hWnd, hdc);

	return 0;
}

//
//	Set the caret position based on m_nCursorOffset,
//	typically used whilst scrolling 
//	(i.e. not due to mouse clicks/keyboard input)
//
ULONG TextView::RepositionCaret()
{
	ULONG lineno, charoff;
	ULONG offset = 0;
	int   xpos   = 0;
	int   ypos   = 0;
	TCHAR buf[TEXTBUFSIZE];

	ULONG nOffset = m_nCursorOffset;

	// make sure we are using the right font
	HDC hdc = GetDC(m_hWnd);
	SelectObject(hdc, m_FontAttr[0].hFont);

	// get line number from cursor-offset
	if(!m_pTextDoc->offset_to_line(nOffset, &lineno, &charoff))
		return 0;

	// y-coordinate from line-number
	ypos = (lineno - m_nVScrollPos) * m_nLineHeight;

	// now find the x-coordinate on the specified line
	while(offset < charoff)
	{
		int tlen = min((charoff - offset), TEXTBUFSIZE);
		tlen     = m_pTextDoc->getdata(nOffset - charoff + offset, buf, tlen);

		offset += tlen;
		xpos   += NeatTextWidth(hdc, buf, tlen, -xpos);
	}
	
	ReleaseDC(m_hWnd, hdc);

	// take horizontal scrollbar into account
	xpos -= m_nHScrollPos * m_nFontWidth;

	SetCaretPos(xpos, ypos);
	return 0;
}


