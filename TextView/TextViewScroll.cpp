//
//	MODULE:		TextView.cpp
//
//	PURPOSE:	Implementation of the TextView control
//
//	NOTES:		www.catch22.net
//

#define STRICT
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <tchar.h>
#include "TextView.h"
#include "TextViewInternal.h"

bool IsKeyPressed(UINT nVirtKey);

//
//	Set scrollbar positions and range
//
VOID TextView::SetupScrollbars()
{
	SCROLLINFO si = { sizeof(si) };

	si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE | SIF_DISABLENOSCROLL;

	//
	//	Vertical scrollbar
	//
	si.nPos  = m_nVScrollPos;		// scrollbar thumb position
	si.nPage = m_nWindowLines;		// number of lines in a page
	si.nMin  = 0;					
	si.nMax  = m_nLineCount - 1;	// total number of lines in file
	
	SetScrollInfo(m_hWnd, SB_VERT, &si, TRUE);

	//
	//	Horizontal scrollbar
	//
	si.nPos  = m_nHScrollPos;		// scrollbar thumb position
	si.nPage = m_nWindowColumns;	// number of lines in a page
	si.nMin  = 0;
	si.nMax  = m_nLongestLine - 1;	// total number of lines in file

	SetScrollInfo(m_hWnd, SB_HORZ, &si, TRUE);

	// adjust our interpretation of the max scrollbar range to make
	// range-checking easier. The scrollbars don't use these values, they
	// are for our own use.
	m_nVScrollMax = m_nLineCount   - m_nWindowLines;
	m_nHScrollMax = m_nLongestLine - m_nWindowColumns;
}

//
//	Ensure that we never scroll off the end of the file
//
bool TextView::PinToBottomCorner()
{
	bool repos = false;

	if(m_nHScrollPos + m_nWindowColumns > m_nLongestLine)
	{
		m_nHScrollPos = m_nLongestLine - m_nWindowColumns;
		repos = true;
	}

	if(m_nVScrollPos + m_nWindowLines > m_nLineCount)
	{
		m_nVScrollPos = m_nLineCount - m_nWindowLines;
		repos = true;
	}

	return repos;
}

//
//	The window has changed size - update the scrollbars
//
LONG TextView::OnSize(UINT nFlags, int width, int height)
{
	int margin = LeftMarginWidth();

	m_nWindowLines   = min((unsigned)height		/ m_nLineHeight, m_nLineCount);
	m_nWindowColumns = min((width - margin)		/ m_nFontWidth,  m_nLongestLine);

	if(PinToBottomCorner())
	{
		RefreshWindow();
		RepositionCaret();
	}
	
	SetupScrollbars();

	return 0;
}

//
//	ScrollRgn
//
//	Scrolls the viewport in specified direction. If fReturnUpdateRgn is true, 
//	then a HRGN is returned which holds the client-region that must be redrawn 
//	manually. This region must be deleted by the caller using DeleteObject.
//
//  Otherwise ScrollRgn returns NULL and updates the entire window 
//
HRGN TextView::ScrollRgn(int dx, int dy, bool fReturnUpdateRgn)
{
	RECT clip;

	GetClientRect(m_hWnd, &clip);

	//
	// make sure that dx,dy don't scroll us past the edge of the document!
	//

	// scroll up
	if(dy < 0)
	{
		dy = -(int)min((ULONG)-dy, m_nVScrollPos);
		clip.top = -dy * m_nLineHeight;
	}
	// scroll down
	else if(dy > 0)
	{
		dy = min((ULONG)dy, m_nVScrollMax-m_nVScrollPos);
		clip.bottom = (m_nWindowLines -dy) * m_nLineHeight;
	}


	// scroll left
	if(dx < 0)
	{
		dx = -(int)min(-dx, m_nHScrollPos);
		clip.left = -dx * m_nFontWidth * 4;
	}
	// scroll right
	else if(dx > 0)
	{
		dx = min((unsigned)dx, (unsigned)m_nHScrollMax-m_nHScrollPos);
		clip.right = (m_nWindowColumns - dx - 4) * m_nFontWidth ;
	}

	// adjust the scrollbar thumb position
	m_nHScrollPos += dx;
	m_nVScrollPos += dy;

	// ignore clipping rectangle if its a whole-window scroll
	if(fReturnUpdateRgn == false)
		GetClientRect(m_hWnd, &clip);

	// take margin into account
	clip.left += LeftMarginWidth();

	// perform the scroll
	if(dx != 0 || dy != 0)
	{
		// do the scroll!
		ScrollWindowEx(
			m_hWnd, 
			-dx * m_nFontWidth,					// scale up to pixel coords
			-dy * m_nLineHeight,
			NULL,								// scroll entire window
			&clip,								// clip the non-scrolling part
			0, 
			0, 
			SW_INVALIDATE
			);

		SetupScrollbars();

		if(fReturnUpdateRgn)
		{
			RECT client;

			GetClientRect(m_hWnd, &client);

			//clip.left -= LeftMarginWidth();

			HRGN hrgnClient  = CreateRectRgnIndirect(&client);
			HRGN hrgnUpdate  = CreateRectRgnIndirect(&clip);

			// create a region that represents the area outside the
			// clipping rectangle (i.e. the part that is never scrolled)
			CombineRgn(hrgnUpdate, hrgnClient, hrgnUpdate, RGN_XOR);

			DeleteObject(hrgnClient);

			return hrgnUpdate;
		}
	}

	if(dy != 0)
	{
		GetClientRect(m_hWnd, &clip);
		clip.right = LeftMarginWidth();
		//ScrollWindow(m_hWnd, 0, -dy * m_nLineHeight, 0, &clip);
		InvalidateRect(m_hWnd, &clip, 0);
	}

	return NULL;
}

//
//	Scroll viewport in specified direction
//
VOID TextView::Scroll(int dx, int dy)
{
	// do a "normal" scroll - don't worry about invalid regions,
	// just scroll the whole window 
	ScrollRgn(dx, dy, false);
}

//
//	Ensure that the specified file-location is visible within
//  the window-viewport, Scrolling the viewport as necessary
//
VOID TextView::ScrollToPosition(int xpos, ULONG lineno)
{
	bool fRefresh = false;
	RECT rect;
	int  marginWidth = LeftMarginWidth();

	GetClientRect(m_hWnd, &rect);

	xpos -= m_nHScrollPos * m_nFontWidth;
	xpos += marginWidth;
	
	if(xpos < marginWidth)
	{
		m_nHScrollPos -= (marginWidth - xpos) / m_nFontWidth;
		fRefresh = true;
	}

	if(xpos >= rect.right)
	{
		m_nHScrollPos += (xpos - rect.right) / m_nFontWidth + 1;
		fRefresh = true;
	}
	
	if(lineno < m_nVScrollPos)
	{
		m_nVScrollPos = lineno;
		fRefresh = true;
	}
	else if(lineno > m_nVScrollPos + m_nWindowLines - 1)
	{
		m_nVScrollPos = lineno - m_nWindowLines + 1;
		fRefresh = true;
	}


	if(fRefresh)
	{
		SetupScrollbars();
		RefreshWindow();
		RepositionCaret();
	}
}

VOID TextView::ScrollToCaret()
{
	ScrollToPosition(m_nCaretPosX, m_nCurrentLine);
}

LONG GetTrackPos32(HWND hwnd, int nBar)
{
	SCROLLINFO si = { sizeof(si), SIF_TRACKPOS };
	GetScrollInfo(hwnd, nBar, &si);
	return si.nTrackPos;
}

//
//	Vertical scrollbar support
//
LONG TextView::OnVScroll(UINT nSBCode, UINT nPos)
{
	ULONG oldpos = m_nVScrollPos;

	switch(nSBCode)
	{
	case SB_TOP:
		m_nVScrollPos = 0;
		RefreshWindow();
		break;

	case SB_BOTTOM:
		m_nVScrollPos = m_nVScrollMax;
		RefreshWindow();
		break;

	case SB_LINEUP:
		Scroll(0, -1);
		break;

	case SB_LINEDOWN:
		Scroll(0, 1);
		break;

	case SB_PAGEDOWN:
		Scroll(0, m_nWindowLines);
		break;

	case SB_PAGEUP:
		Scroll(0, -m_nWindowLines);
		break;

	case SB_THUMBPOSITION:
	case SB_THUMBTRACK:

		m_nVScrollPos = GetTrackPos32(m_hWnd, SB_VERT);
		RefreshWindow();

		break;
	}

	if(oldpos != m_nVScrollPos)
	{
		SetupScrollbars();
		RepositionCaret();
	}


	return 0;
}

//
//	Horizontal scrollbar support
//
LONG TextView::OnHScroll(UINT nSBCode, UINT nPos)
{
	int oldpos = m_nHScrollPos;

	switch(nSBCode)
	{
	case SB_LEFT:
		m_nHScrollPos = 0;
		RefreshWindow();
		break;

	case SB_RIGHT:
		m_nHScrollPos = m_nHScrollMax;
		RefreshWindow();
		break;

	case SB_LINELEFT:
		Scroll(-1, 0);
		break;

	case SB_LINERIGHT:
		Scroll(1, 0);
		break;

	case SB_PAGELEFT:
		Scroll(-m_nWindowColumns, 0);
		break;

	case SB_PAGERIGHT:
		Scroll(m_nWindowColumns, 0);
		break;

	case SB_THUMBPOSITION:
	case SB_THUMBTRACK:

		m_nHScrollPos = GetTrackPos32(m_hWnd, SB_HORZ);
		RefreshWindow();
		break;
	}

	if(oldpos != m_nHScrollPos)
	{
		SetupScrollbars();
		RepositionCaret();
	}

	return 0;
}

LONG TextView::OnMouseWheel(int nDelta)
{
#ifndef	SPI_GETWHEELSCROLLLINES	
#define SPI_GETWHEELSCROLLLINES   104
#endif

	if(!IsKeyPressed(VK_SHIFT))
	{
		int nScrollLines;

		SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &nScrollLines, 0);

		if(nScrollLines <= 1)
			nScrollLines = 3;

		Scroll(0, (-nDelta/120) * nScrollLines);
		RepositionCaret();
	}
	
	return 0;
}
