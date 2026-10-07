//
//	MODULE:		TextViewMouse.cpp
//
//	PURPOSE:	Mouse and caret support for the TextView control
//
//	NOTES:		www.catch22.net
//

#define STRICT
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <tchar.h>
#include "TextView.h"
#include "TextViewInternal.h"

int ScrollDir(int counter, int dir);
bool IsKeyPressed(UINT nVirtKey);

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

HMENU TextView::CreateContextMenu()
{
	HMENU hMenu = CreatePopupMenu();
	TextCoord selStart, selEnd;

	// do we have a selection?
	UINT fSelection = GetSelection(&selStart, &selEnd) ?
		MF_ENABLED : MF_DISABLED| MF_GRAYED;

	// is there text on the clipboard?
	UINT fClipboard = (IsClipboardFormatAvailable(CF_TEXT) || IsClipboardFormatAvailable(CF_UNICODETEXT)) ?
		MF_ENABLED : MF_GRAYED | MF_DISABLED;

	UINT fCanUndo = CanUndo() ? MF_ENABLED : MF_GRAYED | MF_DISABLED;
	UINT fCanRedo = CanRedo() ? MF_ENABLED : MF_GRAYED | MF_DISABLED;

	AppendMenu(hMenu, MF_STRING|fCanUndo,				WM_UNDO, _T("&Undo"));
	AppendMenu(hMenu, MF_STRING|fCanRedo,				TXM_REDO, _T("&Redo"));
	AppendMenu(hMenu, MF_SEPARATOR,						0, 0);
	AppendMenu(hMenu, MF_STRING|fSelection,				WM_CUT,    _T("Cu&t"));
	AppendMenu(hMenu, MF_STRING|fSelection,				WM_COPY,   _T("&Copy"));
	AppendMenu(hMenu, MF_STRING|fClipboard,				WM_PASTE,  _T("&Paste"));
	AppendMenu(hMenu, MF_STRING|fSelection,				WM_CLEAR, _T("&Delete"));
	AppendMenu(hMenu, MF_SEPARATOR,						0, 0);
	AppendMenu(hMenu, MF_STRING|MF_ENABLED,				TXM_SETSELALL, _T("&Select All"));
	AppendMenu(hMenu, MF_SEPARATOR,						0, 0);
	AppendMenu(hMenu, MF_STRING|MF_ENABLED,				WM_USER+7, _T("&Right to left Reading order"));
	AppendMenu(hMenu, MF_STRING|MF_ENABLED,				WM_USER+8, _T("&Show Unicode control characters"));

	return hMenu;
}

//
//	WM_CONTEXTMENU
//
//	Respond to right-click message
//
LONG TextView::OnContextMenu(HWND hwndParam, int x, int y)
{
	if(m_hUserMenu == 0)
	{
		HMENU hMenu = CreateContextMenu();
		UINT  uCmd  = TrackPopupMenu(hMenu, TPM_RETURNCMD, x, y, 0, m_hWnd, 0);

		if(uCmd != 0)
			PostMessage(m_hWnd, uCmd, 0, 0);

		return 0;
	}
	else
	{
		UINT uCmd = TrackPopupMenu(m_hUserMenu, TPM_RETURNCMD, x, y, 0, m_hWnd, 0);

		if(uCmd != 0)
			PostMessage(GetParent(m_hWnd), WM_COMMAND, MAKEWPARAM(uCmd, 0), (LPARAM)GetParent(m_hWnd));

		return 0;
	}
	//PostMessage(m_hWnd, WM_COMMAND, MAKEWPARAM(uCmd, 0), (LPARAM)m_hWnd);
	
	return DefWindowProc(m_hWnd, WM_CONTEXTMENU, (WPARAM)hwndParam, MAKELONG(x,y));
}


//
//	WM_LBUTTONDOWN
//
//  Position caret to nearest text character under mouse
//
LONG TextView::OnLButtonDown(UINT nFlags, int mx, int my)
{
	TextCoord coord;
	TextCoord selStart, selEnd;
	bool hadSelection = GetSelection(&selStart, &selEnd);

	// regular mouse input - mouse is within
	if(mx >= LeftMarginWidth())
	{
		// map the mouse-coordinates to a document coordinate
		MouseCoordToTextCoord(mx, my, &coord, &m_nCaretPosX);
		m_nAnchorPosX = m_nCaretPosX;

		UpdateCaretXY(m_nCaretPosX, &coord);

		// Any key but <shift>
		if(IsKeyPressed(VK_SHIFT) == false)
		{
			// remove any existing selection
			InvalidateRange(&selStart, &selEnd);

			// reset cursor and selection to the same location
			m_selAnchor = coord;

			if(hadSelection)
				RefreshWindow();
		}
		else
		{
			// redraw to cursor; the selection extends from its anchor
			InvalidateRange(&m_cursorPos, &coord);
		}

		if(IsKeyPressed(VK_MENU))
		{
			m_cpBlockStart.line_begin = coord.line_begin;
			m_cpBlockStart.xpos = m_nCaretPosX;
			m_nSelectionType	= SEL_BLOCK;
		}
		else
		{
			m_nSelectionType	= SEL_NORMAL;
		}

		// set capture for mouse-move selections
		m_nSelectionMode = IsKeyPressed(VK_MENU) ? SEL_BLOCK : SEL_NORMAL;
	}
	// mouse clicked within margin: select the whole line
	else
	{
		// remove any existing selection
		InvalidateRange(&selStart, &selEnd);

		if(!ViewportLineFromRow(my / m_nLineHeight, &m_selMarginLine))
			m_selMarginLine = m_scrollVPos;

		//
		// if we click in the margin then jump back to start of line
		//
		if(m_nHScrollPos != 0)
		{
			m_nHScrollPos = 0;
			SetupScrollbars();
			RefreshWindow();
		}

		m_pTextDoc->coord_from_byte_anchor(m_selMarginLine.line_begin, &m_selAnchor);
		m_pTextDoc->coord_from_byte_anchor(m_selMarginLine.line_next, &coord);

		InvalidateRange(&m_selAnchor, &coord);

		if(hadSelection)
			RefreshWindow();

		// set capture for mouse-move selections
		m_nSelectionMode = SEL_MARGIN;
	}

	UpdateLine(&coord);

	if(m_nSelectionMode == SEL_MARGIN)
	{
		UpdateCaretCoord(&coord, FALSE, &m_nCaretPosX, &m_nCurrentLine);
		m_nAnchorPosX = m_nCaretPosX;
	}
	else
	{
		SetCursorCoord(&coord);
	}

	SetCapture(m_hWnd);

	TVNCURSORINFO ci = { { 0 }, coord.line.index, 0, coord.offset_chars };
	NotifyParent(TVN_CURSOR_CHANGE, (NMHDR *)&ci);
	return 0;
}

//
//	WM_LBUTTONUP 
//
//	Release capture and cancel any mouse-scrolling
//
LONG TextView::OnLButtonUp(UINT nFlags, int mx, int my)
{
	if(m_nSelectionMode)
	{
		// cancel the scroll-timer if it is still running
		if(m_nScrollTimer != 0)
		{
			KillTimer(m_hWnd, m_nScrollTimer);
			m_nScrollTimer = 0;
		}

		m_nSelectionMode = SEL_NONE;
		ReleaseCapture();
	}

	return 0;
}

//
//	WM_LBUTTONDBKCLK
//
//	Select the word under the mouse
//
LONG TextView::OnLButtonDblClick(UINT nFlags, int mx, int my)
{
	TextCoord selStart, selEnd;

	// remove any existing selection
	GetSelection(&selStart, &selEnd);
	InvalidateRange(&selStart, &selEnd);

	// regular mouse input - mouse is within scrolling viewport
	if(mx >= LeftMarginWidth())
	{
		TextCoord coord;
		int       xpos;

		// map the mouse-coordinates to a document coordinate
		MouseCoordToTextCoord(mx, my, &coord, &xpos);
		SetCursorCoord(&coord);
		m_nCaretPosX = xpos;
		m_nAnchorPosX = xpos;

		// move selection-start to start of word
		MoveWordStart();
		m_selAnchor = m_cursorPos;

		// move selection-end to end of word
		MoveWordEnd();

		// update caret position
		InvalidateRange(&m_selAnchor, &m_cursorPos);
		UpdateCaretCoord(&m_cursorPos, TRUE, &m_nCaretPosX, &m_nCurrentLine);
		m_nAnchorPosX = m_nCaretPosX;

		NotifyParent(TVN_CURSOR_CHANGE);
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
		BOOL	fCurChanged = FALSE;
		TextCoord coord;

		RECT	rect;
		POINT	pt = { mx, my };

		//
		//	First thing we must do is switch from margin-mode to normal-mode 
		//	if the mouse strays into the main document area
		//
		if(m_nSelectionMode == SEL_MARGIN && mx > LeftMarginWidth())
		{
			m_nSelectionMode = SEL_NORMAL;
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
				m_nScrollTimer   = SetTimer(m_hWnd, 1, 30, 0);
			}
		}

		// get new cursor coordinate
		MouseCoordToTextCoord(mx, my, &coord, &m_nCaretPosX);
		m_nAnchorPosX = m_nCaretPosX;

		m_cpBlockEnd.line_begin = coord.line_begin;
		m_cpBlockEnd.xpos = mx + m_nHScrollPos * m_nFontWidth - LeftMarginWidth();//m_nCaretPosX;

		// a margin selection covers whole lines, anchored on the far side of the clicked line
		if(m_nSelectionMode == SEL_MARGIN)
		{
			if(coord.line_begin >= m_selMarginLine.line_begin)
			{
				m_pTextDoc->coord_from_byte_anchor(m_selMarginLine.line_begin, &m_selAnchor);
				m_pTextDoc->coord_from_byte_anchor(coord.line_next, &coord);
			}
			else
			{
				m_pTextDoc->coord_from_byte_anchor(m_selMarginLine.line_next, &m_selAnchor);
			}
		}

		// redraw the old and new lines if they are different
		UpdateLine(&coord);

		// update the region of text that has changed selection state
		fCurChanged = coord.byte_anchor != m_cursorPos.byte_anchor;

		if(fCurChanged)
		{
			// redraw from old selection-pos to new position
			InvalidateRange(&m_cursorPos, &coord);
			InvalidateLine(&coord, false);
		}

		SetCursorCoord(&coord);

		if(m_nSelectionMode == SEL_BLOCK)
			RefreshWindow();

		//m_nCaretPosX = mx+m_nHScrollPos*m_nFontWidth-LeftMarginWidth();
		// always set the caret position because we might be scrolling
		UpdateCaretXY(m_nCaretPosX, &m_cursorPos);

		if(fCurChanged)
		{
			NotifyParent(TVN_CURSOR_CHANGE);
		}
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
//	Convert mouse(client) coordinates to a document coordinate
//
//	Currently only uses the main font so will not support other
//	fonts introduced by syntax highlighting
//
BOOL TextView::MouseCoordToTextCoord(	int		 mx,			// [in]  mouse x-coord
										int		 my,			// [in]  mouse x-coord
										TextCoord *coord,		// [out] document coordinate
										int		*psnappedX		// [out] adjusted x coord of caret
										)
{
	ULONG off_chars = 0;
	RECT  rect;
	int	  cp;
	TextCoord lineCoord;

	if(coord == 0)
		return FALSE;

	if(psnappedX)
		*psnappedX = 0;

	// get scrollable area
	GetClientRect(m_hWnd, &rect);
	rect.bottom -= rect.bottom % m_nLineHeight;

	if(rect.bottom <= 0 || rect.right <= 0 || m_nLineCount == 0)
	{
		m_pTextDoc->coord_from_document_end(coord);
		return FALSE;
	}

	// take left margin into account
	mx -= LeftMarginWidth();
		
	// clip mouse to edge of window
	if(mx < 0)				mx = 0;
	if(my < 0)				my = 0;
	if(my >= rect.bottom)	my = rect.bottom - 1;
	if(mx >= rect.right)	mx = rect.right  - 1;

	if(!ViewportLineFromRow(my / m_nLineHeight, &lineCoord))
	{
		m_pTextDoc->coord_from_document_end(coord);
		return FALSE;
	}

	mx += m_nHScrollPos * m_nFontWidth;

	// get the USPDATA object for the selected line!!
		USPDATA *uspData = GetUspData(0, &lineCoord, &off_chars);

	if(uspData == 0)
	{
		*coord = lineCoord;

		return FALSE;
	}

	// convert mouse-x coordinate to a character-offset relative to start of line
	UspSnapXToOffset(uspData, mx, &mx, &cp, 0);

	if(!m_pTextDoc->coord_from_line_pos(&lineCoord, cp, coord))
	{
		*coord = lineCoord;
		return FALSE;
	}

	if(psnappedX)
		*psnappedX = mx;// - m_nHScrollPos * m_nFontWidth;
	//*psnappedX		+= LeftMarginWidth();

	return TRUE;
}

LONG TextView::InvalidateLine(TextCoord *line, bool forceAnalysis)
{
	ULONG row;

	if(ViewportRowFromLine(line, &row))
	{
		RECT rect;

		GetClientRect(m_hWnd, &rect);

		rect.top    = row * m_nLineHeight;
		rect.bottom = rect.top + m_nLineHeight;

		InvalidateRect(m_hWnd, &rect, FALSE);
	}

	if(forceAnalysis)
	{
		for(int i = 0; i < USP_CACHE_SIZE; i++)
		{
			if(m_uspCache[i].usage > 0 && m_uspCache[i].line_begin == line->line_begin)
			{
				m_uspCache[i].usage = 0;
				break;
			}
		}
	}

	return 0;
}
//
//	Redraw any line which spans the specified range of text
//
LONG TextView::InvalidateRange(TextCoord *startCoord, TextCoord *finishCoord)
{
	ULONG start  = min(startCoord->byte_anchor, finishCoord->byte_anchor);
	ULONG finish = max(startCoord->byte_anchor, finishCoord->byte_anchor);

	ULONG row;
	RECT  rect;
	RECT  client;
	TextCoord lineCoord;

	// nothing to do?
	if(start == finish)
		return 0;

	if(!ViewportLineFromRow(0, &lineCoord))
		return 0;

	GetClientRect(m_hWnd, &client);

	// invalidate *whole* lines. don't care about flickering anymore because
	// all output is double-buffered now, and this method is much simpler
	for(row = 0; row < (ULONG)m_nWindowLines; row++)
	{
		ULONG lineStart = lineCoord.line_begin;
		ULONG lineEnd = lineCoord.line_next;

		if(lineEnd >= start && lineStart <= finish)
		{
			SetRect(&rect, 0, row * m_nLineHeight, client.right, (row + 1) * m_nLineHeight);
			rect.left -= m_nHScrollPos * m_nFontWidth;
			rect.left += LeftMarginWidth();

			InvalidateRect(m_hWnd, &rect, FALSE);
		}

		if(lineEnd > finish)
			break;

		if(row + 1 < (ULONG)m_nWindowLines)
		{
			TextCoord nextCoord;

			if(!m_pTextDoc->next_line_from_coord(&lineCoord, 1, &nextCoord))
				break;

			if(nextCoord.line_begin == lineCoord.line_begin)
				break;

			lineCoord = nextCoord;
		}
	}

	return 0;
}
/*
//
//	Wrapper around SetCaretPos, hides the caret when it goes
//  off-screen (this protects against x/y wrap around due to integer overflow)
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
}*/

//
//	x		- x-coord relative to start of line
//	coord	- position whose line the caret is on
//
VOID TextView::UpdateCaretXY(int xpos, TextCoord *coord)
{
	bool visible = false;
	ULONG row = 0;

	// convert x-coord to window-relative
	xpos -= m_nHScrollPos * m_nFontWidth;
	xpos += LeftMarginWidth();

	// only show caret if it is visible within viewport
	if(coord && xpos >= LeftMarginWidth())
		visible = ViewportRowFromLine(coord, &row);

	// hide caret if it was previously visible
	if(visible == false && m_fHideCaret == false)
	{
		m_fHideCaret = true;
		HideCaret(m_hWnd);
	}
	// show caret if it was previously hidden
	else if(visible == true && m_fHideCaret == true)
	{
		m_fHideCaret = false;
		ShowCaret(m_hWnd);
	}

	// set caret position if within window viewport
	if(m_fHideCaret == false)
		SetCaretPos(xpos, row * m_nLineHeight);
}

VOID TextView::UpdateCaretCoord(TextCoord *coord, BOOL fTrailing, int *outx, ULONG *outlineno)
{
	ULONG		lineno = 0;
	int			xpos = 0;
	ULONG		off_chars;
	USPDATA	  * uspData;

	if(coord)
	{
		lineno = coord->line.index;
		off_chars = coord->line_offset_chars;
		m_cursorPos = *coord;
		m_nCurrentLine = lineno;

		// locate the USPDATA for this line
		if((uspData = GetUspData(NULL, coord)) != 0)
		{	
			// Provisional lazy offsets can disagree slightly; keep the caret
			// position relative to the analyzed line instead of wrapping.
			off_chars = min(off_chars, (ULONG)uspData->stringLen);
			
			if(fTrailing && off_chars > 0)
				UspOffsetToX(uspData, off_chars-1, TRUE, &xpos);
			else
				UspOffsetToX(uspData, off_chars, FALSE, &xpos);

			// update caret position
			UpdateCaretXY(xpos, coord);
		}
	}
	
	if(outx)	  *outx = xpos;
	if(outlineno) *outlineno = lineno;
}

bool TextView::SetCursorCoord(TextCoord *coord)
{
	if(coord == 0)
		return false;

	m_cursorPos = *coord;
	m_nCurrentLine = coord->line.index;

	return true;
}

VOID TextView::RepositionCaret()
{
	UpdateCaretXY(m_nCaretPosX, &m_cursorPos);
}

//
//	The caret is moving to 'line': redraw the old and new current lines if they differ
//
void TextView::UpdateLine(TextCoord *line)
{
	if(line->line_begin != m_cursorPos.line_begin && CheckStyle(TXS_HIGHLIGHTCURLINE))
	{
		InvalidateLine(&m_cursorPos, false);
		InvalidateLine(line, false);
	}
}

//
//	return direction to scroll (+ve, -ve or 0) based on 
//  distance of mouse from window edge
//
//	note: counter now redundant, we scroll multiple lines at
//  a time (with a slower timer than before) to achieve
//	variable-speed scrolling
//
int ScrollDir(int counter, int distance)
{
	if(distance > 48)		return 5;
	if(distance > 16)		return 2;
	if(distance > 3)		return 1;
	if(distance > 0)		return counter % 5 == 0 ? 1 : 0;
	
	if(distance < -48)		return -5;
	if(distance < -16)		return -2;
	if(distance < -3)		return -1;
	if(distance < 0)		return counter % 5 == 0 ? -1 : 0;

	return 0;
}



