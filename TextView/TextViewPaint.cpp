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

USPCACHE *TextView::GetUspCache(HDC hdc, ULONG nLineNo, ULONG *nOffset/*=0*/)
{
	TCHAR	 buff[TEXTBUFSIZE];
	ATTR	 attr[TEXTBUFSIZE];
	ULONG	 colno = 0;
	ULONG	 off_chars = 0;
	int		 len;
	HDC		 hdcTemp;
	
	USPDATA *uspData;
	ULONG    lru_usage = -1;
	int		 lru_index = 0;

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

		// match the line#
		if(m_uspCache[i].usage > 0 && m_uspCache[i].lineno == nLineNo)
		{
			if(nOffset)
				*nOffset = m_uspCache[i].offset;

			m_uspCache[i].usage++;
			return &m_uspCache[i];
		}
	}

	//
	// not found? overwrite the "least-recently-used" entry
	//
	m_uspCache[lru_index].lineno	= nLineNo;
	m_uspCache[lru_index].usage		= 1;
	uspData = m_uspCache[lru_index].uspData;

	if(hdc == 0)	hdcTemp = GetDC(m_hWnd);
	else			hdcTemp = hdc;
	
	//
	// get the text for the entire line and apply style attributes
	//
	len = m_pTextDoc->getline(nLineNo, buff, TEXTBUFSIZE, &off_chars);
	
	// cache the line's offset and length information
	m_uspCache[lru_index].offset		= off_chars;
	m_uspCache[lru_index].length		= len;
	m_uspCache[lru_index].length_CRLF	= len - CRLF_size(buff, len);

	len = ApplyTextAttributes(nLineNo, off_chars, colno, buff, len, attr);
	
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
	//	Modify CR/LF so cursor cannot traverse into them
	//
	//MarkCRLF(uspData, buff, len, attr);	


	//
	//	Apply the selection
	//
	ApplySelection(uspData, nLineNo, off_chars, len);

	if(hdc == 0)
		ReleaseDC(m_hWnd, hdcTemp);

	if(nOffset) 
		*nOffset = off_chars;

	return &m_uspCache[lru_index];
}


//
//	Return a fully-analyzed USPDATA object for the specified line
//
USPDATA *TextView::GetUspData(HDC hdc, ULONG nLineNo, ULONG *nOffset/*=0*/)
{
	USPCACHE *uspCache = GetUspCache(hdc, nLineNo, nOffset);

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
	ULONG		i;
	ULONG		first;
	ULONG		last;
	
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
	first = m_nVScrollPos + ps.rcPaint.top    / m_nLineHeight;
	last  = m_nVScrollPos + ps.rcPaint.bottom / m_nLineHeight;

	// make sure we never wrap around the 4gb boundary
	if(last < first) 
		last = -1;

	//
	// draw the display line-by-line
	//
	for(i = first; i <= last; i++)
	{
		int sx		= 0;
		int sy		= (i - m_nVScrollPos) * m_nLineHeight;
		int width	= rect.right-rect.left;

		// prep the background
		PaintRect(hdcMem, 0, 0, width, m_nLineHeight, LineColour(i));
		//PaintRect(hdcMem, m_cpBlockStart.xpos+LeftMarginWidth(), 0, m_cpBlockEnd.xpos-m_cpBlockStart.xpos, m_nLineHeight,GetColour(TXC_HIGHLIGHT));

		// draw each line into the offscreen buffer
		PaintLine(hdcMem, i, -m_nHScrollPos * m_nFontWidth, 0, hrgnUpdate);

		// transfer to screen 
		BitBlt(	ps.hdc, sx, sy, width, m_nLineHeight, hdcMem, 0, 0, SRCCOPY);
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
//	Draw the specified line (including margins etc) to the specified location
//
void TextView::PaintLine(HDC hdc, ULONG nLineNo, int xpos, int ypos, HRGN hrgnUpdate)
{
	RECT	bounds;
	HRGN	hrgnBounds;

	GetClientRect(m_hWnd, &bounds);
	SelectClipRgn(hdc, NULL);

	// no point in drawing outside the window-update-region
	if(hrgnUpdate != NULL)
	{
		// work out where the line would have been on-screen
		bounds.left     = (long)(-m_nHScrollPos * m_nFontWidth + LeftMarginWidth());
		bounds.top		= (long)((nLineNo - m_nVScrollPos) * m_nLineHeight);
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

	PaintText(hdc, nLineNo, xpos + LeftMarginWidth(), ypos, &bounds);

	DeleteObject(hrgnBounds);
	SelectClipRgn(hdc, NULL);

	//
	//	draw the margin straight over the top
	//
	if(LeftMarginWidth() > 0)
	{
		PaintMargin(hdc, nLineNo, 0, 0);
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
	int len = wsprintf(buf, LINENO_FMT, m_nLineCount);

	m_nLinenoWidth = TextWidth(hdc, buf, len);

	SelectObject(hdc, hOldFont);
	ReleaseDC(m_hWnd, hdc);
}

//
//	Draw the specified line's margin into the area described by *margin*
//
int TextView::PaintMargin(HDC hdc, ULONG nLineNo, int xpos, int ypos)
{
	RECT	rect = { xpos, ypos, xpos + LeftMarginWidth(), ypos + m_nLineHeight };

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
		
		int  len   = wsprintf(ach, LINENO_FMT, nLineNo + 1);
		int	 width = TextWidth(hdc, ach, len);

		// only draw line number if in-range
		if(nLineNo >= m_nLineCount)
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
	LINEINFO *linfo = GetLineInfo(nLineNo);
	
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
void TextView::PaintText(HDC hdc, ULONG nLineNo, int xpos, int ypos, RECT *bounds)
{
	USPDATA * uspData;
	ULONG	  lineOffset;

	// grab the USPDATA for this line
	uspData = GetUspData(hdc, nLineNo, &lineOffset);

	// set highlight-colours depending on window-focus
	if(GetFocus() == m_hWnd)
		UspSetSelColor(uspData, GetColour(TXC_HIGHLIGHTTEXT), GetColour(TXC_HIGHLIGHT));
	else
		UspSetSelColor(uspData, GetColour(TXC_HIGHLIGHTTEXT2), GetColour(TXC_HIGHLIGHT2));

	// update selection-attribute information for the line
	UspApplySelection(uspData, m_nSelectionStart - lineOffset, m_nSelectionEnd - lineOffset);

	ApplySelection(uspData, nLineNo, lineOffset, uspData->stringLen);

	// draw the text!
	UspTextOut(uspData, hdc, xpos, ypos, m_nLineHeight, m_nHeightAbove, bounds);
}

int	TextView::ApplySelection(USPDATA *uspData, ULONG nLine, ULONG nOffset, ULONG nTextLen)
{
	int selstart = 0;
	int selend   = 0;

	if(m_nSelectionType != SEL_BLOCK)
		return 0;

	if(nLine >= m_cpBlockStart.line && nLine <= m_cpBlockEnd.line)
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
//	nLineNo	- line number
//	nOffset	- actual offset of line within file
//
//	Returns new length of buffer if text has been modified
//
int TextView::ApplyTextAttributes(ULONG nLineNo, ULONG nOffset, ULONG &nColumn, TCHAR *szText, int nTextLen, ATTR *attr)
{
	int i;

	ULONG selstart = min(m_nSelectionStart, m_nSelectionEnd);
	ULONG selend   = max(m_nSelectionStart, m_nSelectionEnd);

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
			attr[i].bg = LongColour(nLineNo);
		}
		else
		{
			attr[i].fg = GetColour(TXC_FOREGROUND);
			attr[i].bg = LineColour(nLineNo);//GetColour(TXC_BACKGROUND);
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
			if(nOffset + i >= selstart && nOffset + i < selend)
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
		if(szText[nLength-1] == '\r' || szText[nLength-1] == '\n' || 
			szText[nLength-1] == '\x0b' || szText[nLength-1] == '\x0c' ||
			szText[nLength-1] == '\x85' || szText[nLength-1] == 0x2028 || 
			szText[nLength-1] == 0x2029)
			return 1;
	}	

	return 0;
}

void TextView::MarkCRLF(USPDATA *uspData, TCHAR *szText, int nLength, ATTR *attr)
{
	SCRIPT_LOGATTR *logAttr = UspGetLogAttr(uspData);

	if(nLength >= 2)
	{
		if(szText[nLength-2] == '\r' && szText[nLength-1] == '\n') 
		{
			logAttr[nLength-1].fCharStop = 0;
			logAttr[nLength-2].fCharStop = 0;
		}
	}
	
	if(nLength >= 1)
	{
		if( szText[nLength-1] == '\n'	||
			szText[nLength-1] == '\r'	||
			szText[nLength-1] == '\x0b' ||
			szText[nLength-1] == '\x0c' ||
			szText[nLength-1] == 0x0085 || 
			szText[nLength-1] == 0x2029 || 
			szText[nLength-1] == 0x2028)
		{
			logAttr[nLength-1].fCharStop = 0;
		}
	}
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
		if( szText[nLength-1] == '\x0b' ||
			szText[nLength-1] == '\x0c' ||
			szText[nLength-1] == 0x0085 || 
			szText[nLength-1] == 0x2029 || 
			szText[nLength-1] == 0x2028)
		{
			attr[nLength-1].eol = TRUE;
			//szText[nLength-1] = ' ';
			return nLength - 0;//(int)fAllow;
		}

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
COLORREF TextView::LineColour(ULONG nLineNo)
{
	if(m_nCurrentLine == nLineNo && CheckStyle(TXS_HIGHLIGHTCURLINE))
		return GetColour(TXC_CURRENTLINE);
	else
		return GetColour(TXC_BACKGROUND);
}

COLORREF TextView::LongColour(ULONG nLineNo)
{
	if(m_nCurrentLine == nLineNo && CheckStyle(TXS_HIGHLIGHTCURLINE))
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
#include <tmschema.h>


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

	return DefWindowProc(m_hWnd, WM_NCPAINT, (WPARAM)hrgnClip, 0);	
}
