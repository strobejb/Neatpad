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
	ULONG windowLines = m_nWindowLines > 0 ? (ULONG)m_nWindowLines : 0;
	ULONG windowColumns = m_nWindowColumns > 0 ? (ULONG)m_nWindowColumns : 0;
	ULONG longestLine = m_nLongestLine > 0 ? (ULONG)m_nLongestLine : 0;

	m_nVScrollMax = m_nLineCount > windowLines ? m_nLineCount - windowLines : 0;
	m_nHScrollMax = longestLine > windowColumns ? (int)(longestLine - windowColumns) : 0;

	if(m_nVScrollPos > m_nVScrollMax)
		m_nVScrollPos = m_nVScrollMax;

	if(m_nHScrollPos > m_nHScrollMax)
		m_nHScrollPos = m_nHScrollMax;

	si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE | SIF_DISABLENOSCROLL;

	//
	//	Vertical scrollbar
	//
	si.nPos  = m_nVScrollPos;		// scrollbar thumb position
	si.nPage = m_nWindowLines;		// number of lines in a page
	si.nMin  = 0;					
	si.nMax  = m_nLineCount ? m_nLineCount - 1 : 0;	// total number of lines in file
	
	SetScrollInfo(m_hWnd, SB_VERT, &si, TRUE);

	//
	//	Horizontal scrollbar
	//
	si.nPos  = m_nHScrollPos;		// scrollbar thumb position
	si.nPage = m_nWindowColumns;	// number of lines in a page
	si.nMin  = 0;
	si.nMax  = longestLine ? longestLine - 1 : 0;	// total number of lines in file

	SetScrollInfo(m_hWnd, SB_HORZ, &si, TRUE);

	// m_nVScrollMax/m_nHScrollMax are our clamped internal ranges; the
	// scrollbars use nMax/nPage above.
}

//
//	Ensure that we never scroll off the end of the file
//
bool TextView::PinToBottomCorner()
{
	bool repos = false;
	ULONG windowLines = m_nWindowLines > 0 ? (ULONG)m_nWindowLines : 0;
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

	if(m_nLineCount <= windowLines)
	{
		if(m_nVScrollPos != 0)
		{
			m_nVScrollPos = 0;
			repos = true;
		}
	}
	else if(m_nVScrollPos + windowLines > m_nLineCount)
	{
		m_nVScrollPos = m_nLineCount - windowLines;
		repos = true;
	}

	return repos;
}

bool TextView::SetScrollCoord(TextCoord *coord)
{
	TextCoord lineStart;

	if(coord == 0)
		return false;

	if(!m_pTextDoc->coord_from_line_pos(coord, 0, &lineStart))
		lineStart = *coord;

	m_scrollVPos = lineStart;

	if(lineStart.line.index_known)
		m_nVScrollPos = lineStart.line.index;
	else if(m_nVScrollPos > m_nVScrollMax)
		m_nVScrollPos = m_nVScrollMax;

	return true;
}

bool TextView::SetScrollLineIndex(ULONG lineno)
{
	DocLine line;
	TextCoord coord;

	if(!m_pTextDoc->line_from_index(lineno, &line))
		return false;

	if(!m_pTextDoc->coord_from_line_offset(&line, line.offset_chars, &coord))
		return false;

	return SetScrollCoord(&coord);
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

int TextView::ScrollVByLines(int dy)
{
	TextCoord target = m_scrollVPos;
	ULONG oldScrollPos = m_nVScrollPos;
	int step = dy < 0 ? -1 : 1;
	int moved = 0;

	if(dy == 0)
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
			if(!m_pTextDoc->next_line_from_coord(&target, 1, &nextCoord))
				break;
		}

		if(nextCoord.line_begin == target.line_begin)
			break;

		target = nextCoord;
		moved += step;
		dy -= step;
	}

	if(moved == 0)
		return 0;

	SetScrollCoord(&target);

	if(!target.line.index_known)
	{
		if(moved < 0)
			m_nVScrollPos = oldScrollPos > (ULONG)-moved ? oldScrollPos - (ULONG)-moved : 0;
		else
			m_nVScrollPos = min(oldScrollPos + (ULONG)moved, m_nVScrollMax);
	}

	return moved;
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
		SetScrollLineIndex(m_nVScrollPos);
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
		dy = ScrollVByLines(dy);
		clip.top = -dy * m_nLineHeight;
	}
	// scroll down
	else if(dy > 0)
	{
		if(m_nVScrollPos >= m_nVScrollMax)
			dy = 0;
		else
			dy = min((ULONG)dy, m_nVScrollMax - m_nVScrollPos);

		dy = ScrollVByLines(dy);
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

VOID TextView::ScrollToPosition(int xpos, ULONG lineno)
{
	DocLine line;
	TextCoord coord;

	if(m_pTextDoc->line_from_index(lineno, &line) && m_pTextDoc->coord_from_line_offset(&line, line.offset_chars, &coord))
		ScrollToCoord(xpos, &coord);
}

VOID TextView::ScrollToCaret()
{
	ScrollToCoord(m_nCaretPosX, &m_cursorPos);
}

VOID TextView::ScrollToDocumentEnd()
{
	ULONG docLength = m_pTextDoc->text_length();
	TextCoord eofCoord;

	if(docLength == 0)
	{
		m_nVScrollPos = 0;
		m_pTextDoc->coord_from_byte_anchor(0, &m_scrollVPos);
		return;
	}

	if(!m_pTextDoc->coord_from_document_end(&eofCoord))
	{
		m_nLineCount = m_pTextDoc->linecount();
		SetupScrollbars();
		m_nVScrollPos = m_nVScrollMax;
		SetScrollLineIndex(m_nVScrollPos);
		return;
	}

	ScrollToDocumentEnd(&eofCoord);
}

VOID TextView::ScrollToDocumentEnd(TextCoord *eofCoord)
{
	TextCoord topCoord;
	ULONG windowLines = m_nWindowLines > 0 ? (ULONG)m_nWindowLines : 0;
	ULONG estimatedLines;
	ULONG visibleBottom;

	if(eofCoord == 0)
		return;

	estimatedLines = m_pTextDoc->linecount();
	m_nLineCount = max(m_nLineCount, estimatedLines);
	m_nLineCount = max(m_nLineCount, eofCoord->line.index + 1);

	if(windowLines > 1)
	{
		if(m_pTextDoc->previous_line_from_coord(eofCoord, windowLines - 1, &topCoord))
			SetScrollCoord(&topCoord);
		else
			SetScrollCoord(eofCoord);
	}
	else
	{
		SetScrollCoord(eofCoord);
	}

	visibleBottom = m_scrollVPos.line.index + windowLines;
	m_nLineCount = max(m_nLineCount, visibleBottom);
	m_nLineCount = max(m_nLineCount, eofCoord->line.index + 1);

	if(m_nLineCount <= windowLines)
	{
		m_nVScrollPos = 0;

		if(!m_pTextDoc->coord_from_byte_anchor(0, &m_scrollVPos))
			m_scrollVPos = *eofCoord;
	}
	else if(m_nVScrollPos + windowLines > m_nLineCount)
	{
		m_nVScrollPos = m_nLineCount - windowLines;
	}

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
	ULONG oldpos = m_nVScrollPos;

	switch(nSBCode)
	{
	case SB_TOP:
		m_nVScrollPos = 0;
		SetScrollLineIndex(0);
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

		m_nVScrollPos = GetTrackPos32(m_hWnd, SB_VERT);
		if(m_nVScrollPos >= m_nVScrollMax)
			ScrollToDocumentEnd();
		else
			SetScrollLineIndex(m_nVScrollPos);

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
