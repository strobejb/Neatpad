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
//	ULONG nCharOff;
	ULONG nFileOff;
	int   px;
	
	// remove any existing selection
	InvalidateRange(m_nSelectionStart, m_nSelectionEnd);

	// regular mouse input - mouse is within 
	if(mx >= LeftMarginWidth())
	{
		// map the mouse-coordinates to a real file-offset-coordinate
		MouseCoordToFilePos(mx, my, &nLineNo, &nFileOff, &px);

		MoveCaret(px, (nLineNo - m_nVScrollPos) * m_nLineHeight);

		// reset cursor and selection offsets to the same location
		m_nSelectionStart	= nFileOff;
		m_nSelectionEnd		= nFileOff;
		m_nCursorOffset		= nFileOff;

		// set capture for mouse-move selections
		m_nSelectionMode = SELMODE_NORMAL;
	}
	// mouse clicked within margin 
	else
	{
		nLineNo = (my / m_nLineHeight) + m_nVScrollPos;

		//
		// if we click in the margin then jump back to start of line
		//
		if(m_nHScrollPos != 0)
		{
			m_nHScrollPos = 0;
			SetupScrollbars();
			RefreshWindow();
		}

		m_pTextDoc->lineinfo_from_lineno(nLineNo, &m_nSelectionStart, &m_nSelectionEnd, 0, 0);
		m_nSelectionEnd    += m_nSelectionStart;
		m_nCursorOffset	    = m_nSelectionStart;
		
		m_nSelMarginOffset1 = m_nSelectionStart;
		m_nSelMarginOffset2 = m_nSelectionEnd;

		InvalidateRange(m_nSelectionStart, m_nSelectionEnd);
		
		//MoveCaret(LeftMarginWidth(), (nLineNo - m_nVScrollPos) * m_nLineHeight);
		RepositionCaret();

		// set capture for mouse-move selections
		m_nSelectionMode = SELMODE_MARGIN;
	}

	UpdateLine(nLineNo);

	SetCapture(m_hWnd);

	return 0;
}

//
//	WM_LBUTTONUP 
//
//	Release capture and cancel any mouse-scrolling
//
LONG TextView::OnLButtonUp(UINT nFlags, int mx, int my)
{
	// shift cursor to end of selection
	if(m_nSelectionMode == SELMODE_MARGIN)
	{
		m_nCursorOffset = m_nSelectionEnd;
		RepositionCaret();
	}

	if(m_nSelectionMode)
	{
		// cancel the scroll-timer if it is still running
		if(m_nScrollTimer != 0)
		{
			KillTimer(m_hWnd, m_nScrollTimer);
			m_nScrollTimer = 0;
		}

		m_nSelectionMode = SELMODE_NONE;
		ReleaseCapture();
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
	if(m_nSelectionMode)
	{
		ULONG	nLineNo, nFileOff;

		RECT	rect;
		POINT	pt = { mx, my };
		int		cx;					// caret coordinates

		//
		//	First thing we must do is switch from margin-mode to normal-mode 
		//	if the mouse strays into the main document area
		//
		if(m_nSelectionMode == SELMODE_MARGIN && mx > LeftMarginWidth())
		{
			m_nSelectionMode = SELMODE_NORMAL;
			SetCursor(LoadCursor(0, IDC_IBEAM));
		}

		//
		//	Mouse-scrolling: detect if the mouse
		//	is inside/outside of the TextView scrolling area
		//  and stop/start a scrolling timer appropriately
		//
		GetClientRect(m_hWnd, &rect);
		
		// build the scrolling area
		rect.bottom -= rect.bottom % m_nLineHeight;
		rect.left   += LeftMarginWidth();

		// If mouse is within this area, we don't need to scroll
		if(PtInRect(&rect, pt))
		{
			if(m_nScrollTimer != 0)
			{
				KillTimer(m_hWnd, m_nScrollTimer);
				m_nScrollTimer = 0;
			}
		}
		// If mouse is outside window, start a timer in
		// order to generate regular scrolling intervals
		else 
		{
			if(m_nScrollTimer == 0)
			{
				m_nScrollCounter = 0;
				m_nScrollTimer   = SetTimer(m_hWnd, 1, 10, 0);
			}
		}

		// get new cursor offset+coordinates
		MouseCoordToFilePos(mx, my, &nLineNo, &nFileOff, &cx);

		// redraw the old and new lines if they are different
		UpdateLine(nLineNo);

		// update the region of text that has changed selection state
		//if(m_nSelectionEnd != nFileOff)
		{
			ULONG linelen;
			m_pTextDoc->lineinfo_from_lineno(nLineNo, 0, &linelen, 0, 0);

			m_nCursorOffset	= nFileOff;

			if(m_nSelectionMode == SELMODE_MARGIN)
			{
				if(nFileOff >= m_nSelectionStart)
				{
					nFileOff += linelen;
					m_nSelectionStart = m_nSelMarginOffset1;
				}
				else
				{
					m_nSelectionStart = m_nSelMarginOffset2;
				}
			}

			// redraw from old selection-pos to new position
			InvalidateRange(m_nSelectionEnd, nFileOff);

			// adjust the cursor + selection to the new offset
			m_nSelectionEnd = nFileOff;
		}
		
		// always set the caret position because we might be scrolling
		MoveCaret(cx, (nLineNo - m_nVScrollPos) * m_nLineHeight);

	}
	// mouse isn't being used for a selection, so set the cursor instead
	else
	{
		if(mx < LeftMarginWidth())
		{
			SetCursor(m_hMarginCursor);
		}
		else
		{
			//OnLButtonDown(0, mx, my);
			SetCursor(LoadCursor(0, IDC_IBEAM));
		}

	}

	return 0;
}

//
//	WM_TIMER handler
//
//	Used to create regular scrolling 
//
LONG TextView::OnTimer(UINT nTimerId)
{
	int	  dx = 0, dy = 0;	// scrolling vectors
	RECT  rect;
	POINT pt;
	
	// find client area, but make it an even no. of lines
	GetClientRect(m_hWnd, &rect);
	rect.bottom -= rect.bottom % m_nLineHeight;
	rect.left   += LeftMarginWidth();

	// get the mouse's client-coordinates
	GetCursorPos(&pt);
	ScreenToClient(m_hWnd, &pt);

	//
	// scrolling up / down??
	//
	if(pt.y < rect.top)					
		dy = ScrollDir(m_nScrollCounter, pt.y - rect.top);

	else if(pt.y >= rect.bottom)	
		dy = ScrollDir(m_nScrollCounter, pt.y - rect.bottom);

	//
	// scrolling left / right?
	//
	if(pt.x < rect.left)					
		dx = ScrollDir(m_nScrollCounter, pt.x - rect.left);

	else if(pt.x > rect.right)		
		dx = ScrollDir(m_nScrollCounter, pt.x - rect.right);

	//
	// Scroll the window but don't update any invalid
	// areas - we will do this manually after we have 
	// repositioned the caret
	//
	HRGN hrgnUpdate = ScrollRgn(dx, dy, true);

	//
	// do the redraw now that the selection offsets are all 
	// pointing to the right places and the scroll positions are valid.
	//
	if(hrgnUpdate != NULL)
	{
		// We perform a "fake" WM_MOUSEMOVE for two reasons:
		//
		// 1. To get the cursor/caret/selection offsets set to the correct place
		//    *before* we redraw (so everything is synchronized correctly)
		//
		// 2. To invalidate any areas due to mouse-movement which won't
		//    get done until the next WM_MOUSEMOVE - and then it would
		//    be too late because we need to redraw *now*
		//
		OnMouseMove(0, pt.x, pt.y);

		// invalidate the area returned by ScrollRegion
		InvalidateRgn(m_hWnd, hrgnUpdate, FALSE);
		DeleteObject(hrgnUpdate);

		// the next time we process WM_PAINT everything 
		// should get drawn correctly!!
		UpdateWindow(m_hWnd);
	}
	
	// keep track of how many WM_TIMERs we process because
	// we might want to skip the next one
	m_nScrollCounter++;

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
									ULONG	*pnFileOffset,  // [out] zero-based file-offset (in chars)
									int		*px				// [out] adjusted x coord of caret
									)
{
	TCHAR buf[TEXTBUFSIZE];
	int   len;
	int	  curx = 0;
	RECT  rect;

	ULONG nLineNo;
	ULONG off_chars;

	// get scrollable area
	GetClientRect(m_hWnd, &rect);
	rect.bottom -= rect.bottom % m_nLineHeight;

	// take left margin into account
	mx -= LeftMarginWidth();
		
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
		nLineNo   = m_nLineCount ? m_nLineCount - 1 : 0;
		off_chars = m_pTextDoc->size();
	}

	HDC    hdc      = GetDC(m_hWnd);
	HANDLE hOldFont = SelectObject(hdc, m_FontAttr[0].hFont);

	mx += m_nHScrollPos * m_nFontWidth;

	TextIterator itor = m_pTextDoc->iterate_line(nLineNo, &off_chars);

	// character offset within the line is more complicated. 
	// We have to parse the text to work out the exact character which
	// falls under the mouse x-coordinate
	while((len = itor.gettext(buf, TEXTBUFSIZE)) > 0)
	{
		len = StripCRLF(buf, len, true);

		// find it's width
		int width = NeatTextWidth(hdc, buf, len, -(curx % TabWidth()));

		// does cursor fall within this segment?
		if(mx >= curx && mx < curx + width)
		{ 
			//
			//	We have a block of text, with the mouse 
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
				curx       += highx;
				off_chars  += high;
			}
			else
			{
				curx       += lowx;
				off_chars  += low;
			}
			
			break;
		}
		else
		{
			curx	  += width;
			off_chars += len;
		}
	}

	SelectObject(hdc, hOldFont);
	ReleaseDC(m_hWnd, hdc);

	*pnLineNo		= nLineNo;
	*pnFileOffset	= off_chars;
	*px				= curx - m_nHScrollPos * m_nFontWidth;
	*px			   += LeftMarginWidth();

	return 0;
}

LONG TextView::InvalidateLine(ULONG nLineNo)
{
	if(nLineNo >= m_nVScrollPos && nLineNo <= m_nVScrollPos + m_nWindowLines)
	{
		RECT rect;
		
		GetClientRect(m_hWnd, &rect);
		
		rect.top    = (nLineNo - m_nVScrollPos) * m_nLineHeight;
		rect.bottom = rect.top + m_nLineHeight;
	
		InvalidateRect(m_hWnd, &rect, FALSE);
	}

	return 0;
}
//
//	Redraw the specified range of text/data in the control
//
LONG TextView::InvalidateRange(ULONG nStart, ULONG nFinish)
{
	ULONG start  = min(nStart, nFinish);
	ULONG finish = max(nStart, nFinish);

	int   xpos1 = 0, xpos2 = 0;
	int   ypos;
	RECT  rect;
	RECT  client;
	
	TextIterator itor;

	// information about current line:
	ULONG lineno;
	ULONG off_chars;
	ULONG len_chars;

	GetClientRect(m_hWnd, &client);

	// nothing to do?
	if(start == finish)
		return 0;

	//
	//	Find the start-of-line information from specified file-offset
	//
	lineno = m_pTextDoc->lineno_from_offset(start);

	// clip to top of window
	if(lineno < m_nVScrollPos)
	{
		lineno = m_nVScrollPos;
		itor   = m_pTextDoc->iterate_line(lineno, &off_chars, &len_chars);
		start  = off_chars;
	}
	else
	{
		itor   = m_pTextDoc->iterate_line(lineno, &off_chars, &len_chars);
	}

	if(!itor || start >= finish)
		return 0;

	HDC hdc = GetDC(m_hWnd);
	SelectObject(hdc, m_FontAttr[0].hFont);

	ypos = (lineno - m_nVScrollPos) * m_nLineHeight;

	//
	// selection starts midline:
	// find the x-coordinate of this character-offset within the line
	//
	if(off_chars < start)
	{
		int   len   = start - off_chars;
		int   width = 0;
		int   tlen;

		TCHAR buf[TEXTBUFSIZE];
		
		// loop until we get on-screen
		while((tlen = itor.gettext(buf, min(len, TEXTBUFSIZE))) > 0 && off_chars < start)
		{
			len		  -= tlen;
			off_chars += tlen;
			len_chars -= tlen;
			
			width	   = NeatTextWidth(hdc, buf, tlen, -(xpos1 % TabWidth()));
			xpos1     += width;

			if(tlen == 0)
				break;
		}

		// xpos now equals the start of range
	}

	//
	// Invalidate all *whole* lines that aren't part of the last line
	//
	while(itor && finish >= off_chars + len_chars)
	{
		SetRect(&rect, xpos1, ypos, client.right, ypos + m_nLineHeight);
		rect.left -= m_nHScrollPos * m_nFontWidth;
		rect.left += LeftMarginWidth();
			
		//InvertRect(hdc, &rect);
		InvalidateRect(m_hWnd, &rect, FALSE);

		// jump down to next line
		xpos1	= 0;
		ypos   += m_nLineHeight;
			
		// get next line's file-offset & length
		itor = m_pTextDoc->iterate_line(++lineno, &off_chars, &len_chars);
	}

	xpos2 = xpos1;

	//
	// erase up to the end of selection on *last* line of selection
	//
	if(off_chars < finish)
	{
		TCHAR buf[TEXTBUFSIZE];
		int width;
		int len;
		
		while((len = itor.gettext(buf,  min((finish - off_chars), TEXTBUFSIZE))) && off_chars < finish)
		{
			width	   = NeatTextWidth(hdc, buf, len, -(xpos2 % TabWidth()));
			xpos2     += width;
			off_chars += len;
		}
	}
	
	SetRect(&rect, xpos1, ypos, xpos2, ypos + m_nLineHeight);
	OffsetRect(&rect, -m_nHScrollPos * m_nFontWidth, 0);
	OffsetRect(&rect, LeftMarginWidth(), 0);

	//InvertRect(hdc, &rect);
	InvalidateRect(m_hWnd, &rect, FALSE);

	ReleaseDC(m_hWnd, hdc);

	return 0;
}

//
//	Wrapper around SetCaretPos, hides the caret when it goes
//  off-screen (in case x/y wrap around)
//
VOID TextView::MoveCaret(int x, int y)
{
	if(x < LeftMarginWidth() && m_fHideCaret == false)
	{
		m_fHideCaret = true;
		HideCaret(m_hWnd);
	}
	else if(x >= LeftMarginWidth() && m_fHideCaret == true)
	{
		m_fHideCaret = false;
		ShowCaret(m_hWnd);
	}

	if(m_fHideCaret == false)
		SetCaretPos(x, y);
}

//
//	Set the caret position based on m_nCursorOffset,
//	typically used whilst scrolling 
//	(i.e. not due to mouse clicks/keyboard input)
//
ULONG TextView::RepositionCaret()
{
	int   xpos   = 0;
	int   ypos   = 0;
	TCHAR buf[TEXTBUFSIZE];

	ULONG lineno;
	ULONG off_chars;
	int	  len;

	// get start-of-line information from cursor-offset
	TextIterator itor = m_pTextDoc->iterate_line_offset(m_nCursorOffset, &lineno, &off_chars);

	if(!itor)
		return 0;

	// make sure we are using the right font
	HDC hdc = GetDC(m_hWnd);
	SelectObject(hdc, m_FontAttr[0].hFont);

	// y-coordinate from line-number
	ypos = (lineno - m_nVScrollPos) * m_nLineHeight;

	// now find the x-coordinate on the specified line
	while((len = itor.gettext(buf, TEXTBUFSIZE)) > 0 && off_chars < m_nCursorOffset)
	{
		// only parse the bit we want
		len		     = min(m_nCursorOffset - off_chars, len);
		xpos		+= NeatTextWidth(hdc, buf, len, -xpos);
		off_chars	+= len;
	}
	
	ReleaseDC(m_hWnd, hdc);

	// take horizontal scrollbar into account
	xpos -= m_nHScrollPos * m_nFontWidth;

	// take left margin into account
	xpos += LeftMarginWidth();

	MoveCaret(xpos, ypos);

	return 0;
}

void TextView::UpdateLine(ULONG nLineNo)
{
	// redraw the old and new lines if they are different
	if(m_nCurrentLine != nLineNo)
	{
		if(CheckStyle(TXS_HIGHLIGHTCURLINE))
			InvalidateLine(m_nCurrentLine);

		m_nCurrentLine = nLineNo;

		if(CheckStyle(TXS_HIGHLIGHTCURLINE))
			InvalidateLine(m_nCurrentLine);
	}
}

//
//	return direction to scroll (+1,-1 or 0) based on 
//  distance of mouse from window edge
//
//	"counter" is used to achieve variable-speed scrolling
//
int ScrollDir(int counter, int distance)
{
	int amt;

	// amount to scroll based on distance of mouse from window
	if(abs(distance) < 16)			amt = 8;
	else if(abs(distance) < 48)		amt = 3;
	else							amt = 1;

	if(counter % amt == 0)
		return distance < 0 ? -1 : 1;
	else
		return 0;
}



