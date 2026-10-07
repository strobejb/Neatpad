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
#include <limits.h>
#include <tchar.h>
#include "TextView.h"
#include "TextViewInternal.h"

bool IsKeyPressed(UINT nVirtKey);

static int ScrollIntFromSize(size_w value)
{
	return value > (size_w)INT_MAX ? INT_MAX : (int)value;
}

static UINT ScrollPageFromSize(size_w value)
{
	return value > (size_w)UINT_MAX ? UINT_MAX : (UINT)value;
}

static int ScrollPosFromOffset(size_w offset, size_w docBytes)
{
	if(docBytes <= (size_w)INT_MAX)
		return ScrollIntFromSize(offset);

	return (int)((long double)offset * INT_MAX / (docBytes - 1));
}

static UINT ScrollPageFromBytes(size_w pageBytes, size_w docBytes)
{
	if(docBytes <= (size_w)INT_MAX)
		return ScrollPageFromSize(pageBytes);

	size_w page = (size_w)((long double)pageBytes * INT_MAX / docBytes);
	return ScrollPageFromSize(max(page, (size_w)1));
}

static size_w ScrollOffsetFromPos(int pos, size_w docBytes)
{
	if(docBytes <= (size_w)INT_MAX)
		return (size_w)max(pos, 0);

	return (size_w)((long double)max(pos, 0) * (docBytes - 1) / INT_MAX);
}

//
//	Set scrollbar positions and range
//
VOID TextView::SetupScrollbars()
{
	SCROLLINFO si = { sizeof(si) };
	ULONG windowColumns = m_nWindowColumns > 0 ? (ULONG)m_nWindowColumns : 0;
	ULONG longestLine = m_nLongestLine > 0 ? (ULONG)m_nLongestLine : 0;
	size_w docBytes = m_pTextDoc->byte_length();
	TextCoord belowWindow;
	size_w pageEnd;

	m_nHScrollMax = longestLine > windowColumns ? (int)(longestLine - windowColumns) : 0;

	if(m_nHScrollPos > m_nHScrollMax)
		m_nHScrollPos = m_nHScrollMax;

	si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE | SIF_DISABLENOSCROLL;

	//
	//	Vertical scrollbar, in bytes: the thumb is the top line's offset and the
	//	page is the text shown in the window, so a document that fits needs no scrolling
	//
	pageEnd = ViewportLineFromRow(m_nWindowLines, &belowWindow) ? belowWindow.line_begin : docBytes;

	si.nPos  = ScrollPosFromOffset(m_scrollVPos.line_begin, docBytes);
	si.nPage = ScrollPageFromBytes(max(pageEnd - m_scrollVPos.line_begin, (size_w)1), docBytes);
	si.nMin  = 0;
	si.nMax  = docBytes > (size_w)INT_MAX ? INT_MAX : (docBytes ? ScrollIntFromSize(docBytes - 1) : 0);

	SetScrollInfo(m_hWnd, SB_VERT, &si, TRUE);

	//
	//	Horizontal scrollbar
	//
	si.nPos  = m_nHScrollPos;		// scrollbar thumb position
	si.nPage = ScrollPageFromSize(windowColumns);	// number of lines in a page
	si.nMin  = 0;
	si.nMax  = longestLine ? ScrollIntFromSize(longestLine - 1) : 0;	// total number of lines in file

	SetScrollInfo(m_hWnd, SB_HORZ, &si, TRUE);
}

//
//	Ensure that we never scroll off the end of the file
//
bool TextView::PinToBottomCorner()
{
	bool repos = false;
	size_w topLine = m_scrollVPos.line_begin;
	ULONG windowColumns = m_nWindowColumns > 0 ? (ULONG)m_nWindowColumns : 0;
	ULONG longestLine = m_nLongestLine > 0 ? (ULONG)m_nLongestLine : 0;

	if(longestLine <= windowColumns)
	{
		if(m_nHScrollPos != 0)
		{
			m_nHScrollPos = 0;
			repos = true;
		}
	}
	else if((ULONG)m_nHScrollPos + windowColumns > longestLine)
	{
		m_nHScrollPos = (int)(longestLine - windowColumns);
		repos = true;
	}

	// re-apply the end-of-document limit for the new window size
	SetScrollCoord(&m_scrollVPos);

	if(m_scrollVPos.line_begin != topLine)
		repos = true;

	return repos;
}

//
//	Make the line containing coord the top line of the view. The view never
//	scrolls further than the end of the document sitting on the last row.
//
bool TextView::SetScrollCoord(TextCoord *coord)
{
	TextCoord lineStart;
	TextCoord last;

	if(coord == 0)
		return false;

	if(!m_pTextDoc->coord_from_line_pos(coord, 0, &lineStart))
		lineStart = *coord;

	if(LastScrollCoord(&last) && lineStart.line_begin > last.line_begin)
		lineStart = last;

	m_scrollVPos = lineStart;
	return true;
}

//
//	The furthest the view can scroll: the top line that puts the end of the
//	document on the last whole row
//
bool TextView::LastScrollCoord(TextCoord *top)
{
	TextCoord eofCoord;

	if(!m_pTextDoc->coord_from_document_end(&eofCoord))
		return false;

	return m_pTextDoc->previous_line_from_coord(&eofCoord, m_nWindowLines > 1 ? m_nWindowLines - 1 : 0, top);
}

//
//	Keep the viewport on the same text after the document changes. The scroll
//	anchor's line bounds are re-resolved even when the anchor itself does not
//	move, because an edit on the top line changes where that line ends.
//
VOID TextView::AdjustCoordsForChange(TextChange *change)
{
	TextCoord scrollPos = m_scrollVPos;

	if(m_pTextDoc->coord_after_change(&scrollPos, change))
		SetScrollCoord(&scrollPos);
}

bool TextView::ViewportLineFromRow(ULONG row, TextCoord *coord)
{
	TextCoord lineCoord;

	if(coord == 0)
		return false;

	lineCoord = m_scrollVPos;

	while(row-- > 0)
	{
		TextCoord nextCoord;

		if(!m_pTextDoc->next_line_from_coord(&lineCoord, 1, &nextCoord))
			return false;

		if(nextCoord.line_begin == lineCoord.line_begin)
			return false;

		lineCoord = nextCoord;
	}

	*coord = lineCoord;
	return true;
}

//
//	Find the viewport row showing a line, including the partly visible row
//	below the last whole one
//
bool TextView::ViewportRowFromLine(TextCoord *line, ULONG *row)
{
	ULONG windowLines = m_nWindowLines > 0 ? (ULONG)m_nWindowLines : 0;
	TextCoord rowCoord = m_scrollVPos;

	for(ULONG r = 0; r <= windowLines; r++)
	{
		TextCoord nextCoord;

		if(rowCoord.line_begin == line->line_begin)
		{
			*row = r;
			return true;
		}

		if(rowCoord.line_begin > line->line_begin)
			return false;

		if(!m_pTextDoc->next_line_from_coord(&rowCoord, 1, &nextCoord) || nextCoord.line_begin == rowCoord.line_begin)
			return false;

		rowCoord = nextCoord;
	}

	return false;
}

//
//	Scroll the view up or down by whole lines, stopping at either end of the
//	document. Returns the number of lines actually scrolled.
//
int TextView::ScrollVByLines(int dy)
{
	TextCoord target = m_scrollVPos;
	TextCoord last;
	int step = dy < 0 ? -1 : 1;
	int moved = 0;

	if(dy == 0 || (dy > 0 && !LastScrollCoord(&last)))
		return 0;

	while(dy != 0)
	{
		TextCoord nextCoord;

		if(step < 0)
		{
			if(!m_pTextDoc->previous_line_from_coord(&target, 1, &nextCoord))
				break;
		}
		else
		{
			if(target.line_begin >= last.line_begin || !m_pTextDoc->next_line_from_coord(&target, 1, &nextCoord))
				break;
		}

		if(nextCoord.line_begin == target.line_begin)
			break;

		target = nextCoord;
		moved += step;
		dy -= step;
	}

	if(moved != 0)
		SetScrollCoord(&target);

	return moved;
}

//
//	The window has changed size - update the scrollbars
//
LONG TextView::OnSize(UINT nFlags, int width, int height)
{
	int margin = LeftMarginWidth();

	m_nWindowLines   = height / m_nLineHeight;
	m_nWindowColumns = (width - margin) / m_nFontWidth;

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

	dy = ScrollVByLines(dy);

	// scroll up
	if(dy < 0)
	{
		clip.top = -dy * m_nLineHeight;
	}
	// scroll down
	else if(dy > 0)
	{
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
VOID TextView::ScrollToCoord(int xpos, TextCoord *coord)
{
	bool fRefresh = false;
	RECT rect;
	int  marginWidth = LeftMarginWidth();
	ULONG windowLines = m_nWindowLines > 0 ? (ULONG)m_nWindowLines : 0;
	bool coordVisible = false;

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

	if(coord && windowLines > 0)
	{
		TextCoord rowCoord = m_scrollVPos;

		for(ULONG row = 0; row < windowLines; row++)
		{
			if(rowCoord.line_begin == coord->line_begin)
			{
				coordVisible = true;
				break;
			}

			if(rowCoord.line_begin > coord->line_begin)
				break;

			if(row + 1 < windowLines)
			{
				TextCoord nextCoord;

				if(!m_pTextDoc->next_line_from_coord(&rowCoord, 1, &nextCoord))
					break;

				if(nextCoord.line_begin == rowCoord.line_begin)
					break;

				rowCoord = nextCoord;
			}
		}

		if(!coordVisible)
		{
			if(coord->line_begin < m_scrollVPos.line_begin)
			{
				SetScrollCoord(coord);
			}
			else if(windowLines > 1)
			{
				TextCoord topCoord;

				if(m_pTextDoc->previous_line_from_coord(coord, windowLines - 1, &topCoord))
					SetScrollCoord(&topCoord);
				else
					SetScrollCoord(coord);
			}
			else
			{
				SetScrollCoord(coord);
			}

			fRefresh = true;
		}
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
	ScrollToCoord(m_nCaretPosX, &m_cursorPos);
}

VOID TextView::ScrollToDocumentEnd()
{
	TextCoord eofCoord;

	if(m_pTextDoc->coord_from_document_end(&eofCoord))
		SetScrollCoord(&eofCoord);

	SetupScrollbars();
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
	size_w oldpos = m_scrollVPos.line_begin;
	TextCoord coord;

	switch(nSBCode)
	{
	case SB_TOP:
		if(m_pTextDoc->coord_from_byte_anchor(0, &coord))
			SetScrollCoord(&coord);

		RefreshWindow();
		break;

	case SB_BOTTOM:
		ScrollToDocumentEnd();
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

		// the thumb is a byte offset: show the line containing it
		if(m_pTextDoc->coord_from_byte_anchor(ScrollOffsetFromPos(GetTrackPos32(m_hWnd, SB_VERT), m_pTextDoc->byte_length()), &coord))
			SetScrollCoord(&coord);

		RefreshWindow();
		break;
	}

	if(oldpos != m_scrollVPos.line_begin)
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
