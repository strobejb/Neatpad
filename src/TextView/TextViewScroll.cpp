// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

//
//	MODULE:		TextView.cpp
//
//	PURPOSE:	Implementation of the TextView control
//
//	NOTES:		www.catch22.net
//

#include "TextViewInternal.h"

//
//	Set scrollbar positions and range
//
void TextView::SetupScrollbars()
{
	uint32_t windowColumns = m_nWindowColumns > 0 ? (uint32_t)m_nWindowColumns : 0;
	uint32_t longestLine = m_nLongestLine > 0 ? (uint32_t)m_nLongestLine : 0;
	size_w docBytes = m_pTextDoc->byte_length();
	TextCoord belowWindow;
	size_w pageEnd;

	m_nHScrollMax = longestLine > windowColumns ? (int)(longestLine - windowColumns) : 0;

	if(m_nHScrollPos > m_nHScrollMax)
		m_nHScrollPos = m_nHScrollMax;

	//
	//	Vertical scrollbar, in bytes: the thumb is the top line's offset and the
	//	page is the text shown in the window, so a document that fits needs no scrolling
	//
	pageEnd = ViewportLineFromRow(m_nWindowLines, &belowWindow) ? belowWindow.line_begin.byte_offset : docBytes;

	m_pHost->SetVScroll(docBytes ? docBytes - 1 : 0, m_scrollVPos.line_begin.byte_offset,
		max_of(pageEnd - m_scrollVPos.line_begin.byte_offset, (size_w)1));

	//
	//	Horizontal scrollbar: the thumb is the first column shown, the page the
	//	columns in the window, and the range the widest line laid out so far
	//
	m_pHost->SetHScroll(longestLine ? (int)longestLine - 1 : 0, m_nHScrollPos, (int)windowColumns);
}

//
//	Ensure that we never scroll off the end of the file
//
bool TextView::PinToBottomCorner()
{
	bool repos = false;
	TextPos topLine = m_scrollVPos.line_begin;
	uint32_t windowColumns = m_nWindowColumns > 0 ? (uint32_t)m_nWindowColumns : 0;
	uint32_t longestLine = m_nLongestLine > 0 ? (uint32_t)m_nLongestLine : 0;

	if(longestLine <= windowColumns)
	{
		if(m_nHScrollPos != 0)
		{
			m_nHScrollPos = 0;
			repos = true;
		}
	}
	else if((uint32_t)m_nHScrollPos + windowColumns > longestLine)
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
void TextView::AdjustCoordsForChange(TextChange *change)
{
	TextCoord scrollPos = m_scrollVPos;

	if(m_pTextDoc->coord_after_change(&scrollPos, change))
		SetScrollCoord(&scrollPos);
}

bool TextView::ViewportLineFromRow(uint32_t row, TextCoord *coord)
{
	LineIterator line = m_pTextDoc->line_at(m_scrollVPos);

	if(coord == 0)
		return false;

	while(row-- > 0)
	{
		if(!line.next())
			return false;
	}

	*coord = line.line();
	return true;
}

//
//	Find the viewport row showing a line, including the partly visible row
//	below the last whole one
//
bool TextView::ViewportRowFromLine(TextCoord *line, uint32_t *row)
{
	uint32_t windowLines = m_nWindowLines > 0 ? (uint32_t)m_nWindowLines : 0;
	LineIterator rowLine = m_pTextDoc->line_at(m_scrollVPos);

	for(uint32_t r = 0; r <= windowLines; r++)
	{
		if(rowLine.line().line_begin == line->line_begin)
		{
			*row = r;
			return true;
		}

		if(rowLine.line().line_begin > line->line_begin || !rowLine.next())
			return false;
	}

	return false;
}

//
//	Scroll the view up or down by whole lines, stopping at either end of the
//	document. Returns the number of lines actually scrolled.
//
int TextView::ScrollVByLines(int dy)
{
	LineIterator target = m_pTextDoc->line_at(m_scrollVPos);
	TextCoord last;
	int step = dy < 0 ? -1 : 1;
	int moved = 0;

	if(dy == 0 || (dy > 0 && !LastScrollCoord(&last)))
		return 0;

	while(dy != 0)
	{
		if(step < 0)
		{
			if(!target.prev())
				break;
		}
		else
		{
			if(target.line().line_begin >= last.line_begin || !target.next())
				break;
		}

		moved += step;
		dy -= step;
	}

	if(moved != 0)
	{
		TextCoord top = target.line();
		SetScrollCoord(&top);
	}

	return moved;
}

//
//	The view's client area, from its top-left corner
//
void TextView::ClientRect(TextRect *rect)
{
	int width, height;

	m_pHost->ClientSize(&width, &height);
	*rect = { 0, 0, width, height };
}

//
//	The window has changed size - update the scrollbars
//
int32_t TextView::OnSize(uint32_t nFlags, int width, int height)
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
//	it returns true when it scrolled, with the rectangle that scrolled in
//	*scrolled: everything outside it must be redrawn by the caller, with
//	InvalidateOutside.
//
//  Otherwise ScrollRgn returns false and updates the entire window
//
bool TextView::ScrollRgn(int dx, int dy, bool fReturnUpdateRgn, TextRect *scrolled)
{
	TextRect clip;

	ClientRect(&clip);

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
		dx = -(int)min_of(-dx, m_nHScrollPos);
		clip.left = -dx * m_nFontWidth * 4;
	}
	// scroll right
	else if(dx > 0)
	{
		dx = min_of((unsigned)dx, (unsigned)m_nHScrollMax-m_nHScrollPos);
		clip.right = (m_nWindowColumns - dx - 4) * m_nFontWidth ;
	}

	// adjust the scrollbar thumb position
	m_nHScrollPos += dx;

	// ignore clipping rectangle if its a whole-window scroll
	if(fReturnUpdateRgn == false)
		ClientRect(&clip);

	// take margin into account
	clip.left += LeftMarginWidth();

	// perform the scroll
	if(dx != 0 || dy != 0)
	{
		// do the scroll! scaled up to pixels, and clipped to the part that scrolls
		m_pHost->ScrollPixels(-dx * m_nFontWidth, -dy * m_nLineHeight, clip.left, clip.top, clip.right, clip.bottom);

		SetupScrollbars();

		if(fReturnUpdateRgn)
		{
			*scrolled = clip;
			return true;
		}
	}

	if(dy != 0)
	{
		ClientRect(&clip);
		clip.right = LeftMarginWidth();
		m_pHost->InvalidateRect(clip.left, clip.top, clip.right, clip.bottom);
	}

	return false;
}

//
//	Redraw everything in the window outside a rectangle (the part that is never
//	scrolled): above, below, and to either side of it
//
void TextView::InvalidateOutside(const TextRect &inside)
{
	TextRect client;

	ClientRect(&client);

	if(inside.top > client.top)
		m_pHost->InvalidateRect(client.left, client.top, client.right, inside.top);

	if(inside.bottom < client.bottom)
		m_pHost->InvalidateRect(client.left, inside.bottom, client.right, client.bottom);

	if(inside.left > client.left)
		m_pHost->InvalidateRect(client.left, inside.top, inside.left, inside.bottom);

	if(inside.right < client.right)
		m_pHost->InvalidateRect(inside.right, inside.top, client.right, inside.bottom);
}

//
//	Scroll viewport in specified direction
//
void TextView::Scroll(int dx, int dy)
{
	// do a "normal" scroll - don't worry about invalid regions,
	// just scroll the whole window 
	ScrollRgn(dx, dy, false, 0);
}

//
//	Ensure that the specified file-location is visible within
//  the window-viewport, Scrolling the viewport as necessary
//
void TextView::ScrollToCoord(int xpos, TextCoord *coord)
{
	bool fRefresh = false;
	TextRect rect;
	int  marginWidth = LeftMarginWidth();
	uint32_t windowLines = m_nWindowLines > 0 ? (uint32_t)m_nWindowLines : 0;
	bool coordVisible = false;

	ClientRect(&rect);

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
		LineIterator rowLine = m_pTextDoc->line_at(m_scrollVPos);

		for(uint32_t row = 0; row < windowLines; row++)
		{
			if(rowLine.line().line_begin == coord->line_begin)
			{
				coordVisible = true;
				break;
			}

			if(rowLine.line().line_begin > coord->line_begin)
				break;

			if(row + 1 < windowLines && !rowLine.next())
				break;
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

void TextView::ScrollToCaret()
{
	ScrollToCoord(m_nCaretPosX, &m_cursorPos);
}

void TextView::ScrollToDocumentEnd()
{
	TextCoord eofCoord;

	if(m_pTextDoc->coord_from_document_end(&eofCoord))
		SetScrollCoord(&eofCoord);

	SetupScrollbars();
}

//
//	Vertical scrollbar support
//
int32_t TextView::OnVScroll(uint32_t nSBCode, uint32_t nPos)
{
	TextPos oldpos = m_scrollVPos.line_begin;
	size_w docBytes = m_pTextDoc->byte_length();
	TextCoord coord;

	switch(nSBCode)
	{
	case TVSB_TOP:
		if(m_pTextDoc->coord_from_pos(TextPos(0), &coord))
			SetScrollCoord(&coord);

		RefreshWindow();
		break;

	case TVSB_BOTTOM:
		ScrollToDocumentEnd();
		RefreshWindow();
		break;

	case TVSB_LINEUP:
		Scroll(0, -1);
		break;

	case TVSB_LINEDOWN:
		Scroll(0, 1);
		break;

	case TVSB_PAGEDOWN:
		Scroll(0, m_nWindowLines);
		break;

	case TVSB_PAGEUP:
		Scroll(0, -m_nWindowLines);
		break;

	case TVSB_THUMBPOSITION:
	case TVSB_THUMBTRACK:

		// the thumb is a byte offset, which can fall part way through a character:
		// only the line containing it is used, to show that line
		if(m_pTextDoc->coord_from_pos(TextPos(m_pHost->VScrollTrackPos(docBytes ? docBytes - 1 : 0)), &coord))
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
int32_t TextView::OnHScroll(uint32_t nSBCode, uint32_t nPos)
{
	int oldpos = m_nHScrollPos;

	switch(nSBCode)
	{
	case TVSB_LEFT:
		m_nHScrollPos = 0;
		RefreshWindow();
		break;

	case TVSB_RIGHT:
		m_nHScrollPos = m_nHScrollMax;
		RefreshWindow();
		break;

	case TVSB_LINELEFT:
		Scroll(-1, 0);
		break;

	case TVSB_LINERIGHT:
		Scroll(1, 0);
		break;

	case TVSB_PAGELEFT:
		Scroll(-m_nWindowColumns, 0);
		break;

	case TVSB_PAGERIGHT:
		Scroll(m_nWindowColumns, 0);
		break;

	case TVSB_THUMBPOSITION:
	case TVSB_THUMBTRACK:

		m_nHScrollPos = m_pHost->HScrollTrackPos();
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

int32_t TextView::OnMouseWheel(int nDelta)
{
	if((m_pHost->Modifiers() & TextViewHost::TVMOD_SHIFT) == 0)
	{
		int nScrollLines = m_pHost->WheelScrollLines();

		if(nScrollLines <= 1)
			nScrollLines = 3;

		Scroll(0, (-nDelta/120) * nScrollLines);
		RepositionCaret();
	}
	
	return 0;
}
