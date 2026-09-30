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
//	Painting procedure for TextView objects
//
LONG TextView::OnPaint()
{
	PAINTSTRUCT ps;

	ULONG i, first, last;

	BeginPaint(m_hWnd, &ps);

	// figure out which lines to redraw
	first = m_nVScrollPos + ps.rcPaint.top    / m_nLineHeight;
	last  = m_nVScrollPos + ps.rcPaint.bottom / m_nLineHeight;

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
//	Draw the specified line (including margins etc)
//
void TextView::PaintLine(HDC hdc, ULONG nLineNo)
{
	RECT  rect;

	// Get the area we want to update
	GetClientRect(m_hWnd, &rect);

	// calculate rectangle for entire length of line in window
	rect.left       = (long)(-m_nHScrollPos * m_nFontWidth);
	rect.top		= (long)((nLineNo - m_nVScrollPos) * m_nLineHeight);
	rect.right		= (long)(rect.right);
	rect.bottom		= (long)(rect.top + m_nLineHeight);

	//
	//	do things like margins, line numbers etc. here
	//
	if(LeftMarginWidth() > 0)
	{
		RECT margin = rect;
		
		margin.left		= 0;
		margin.right	= LeftMarginWidth();
		rect.left	   += LeftMarginWidth();

		PaintMargin(hdc, nLineNo, &margin);
	
		ExcludeClipRect(hdc, margin.left, margin.top, margin.right, margin.bottom);
	}	

	//
	//	check we have data to draw on this line
	//
	if(nLineNo >= m_nLineCount)
	{
		SetBkColor(hdc, GetColour(TXC_BACKGROUND));
		ExtTextOut(hdc, 0, 0, ETO_OPAQUE, &rect, 0, 0, 0);
		return;
	}
	
	//
	//	paint the line's text
	//
	PaintText(hdc, nLineNo, &rect);
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

		if(1)
			width += 1;

		if(0)
			width += 5;
		
		return width;
	}
	// selection margin by itself
	else if(CheckStyle(TXS_SELMARGIN))
	{
		width += cx + 4;

		if(0)
			width += 1;

		if(0)
			width += 5;

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
	HANDLE	hOldFont = SelectObject(hdc, m_FontAttr[0].hFont);

	TCHAR	buf[32];
	int len = wsprintf(buf, LINENO_FMT, m_nLineCount);

	m_nLinenoWidth = NeatTextWidth(hdc, buf, len, 0);

	SelectObject(hdc, hOldFont);
	ReleaseDC(m_hWnd, hdc);
}

//
//	Draw the specified line's margin into the area described by *margin*
//
int TextView::PaintMargin(HDC hdc, ULONG nLineNo, RECT *margin)
{
	RECT  rect = *margin;
	TCHAR ach[32];

	int imgWidth, imgHeight;
	int imgX, imgY;
	int selwidth = CheckStyle(TXS_SELMARGIN) ? 20 : 0;

	//int nummaxwidth = 60;
	int curx = margin->left;
	int cury = margin->right;
	
	if(m_hImageList && selwidth > 0)
	{
		// selection margin must include imagelists
		ImageList_GetIconSize(m_hImageList, &imgWidth, &imgHeight);

		imgX = rect.left + (selwidth - imgWidth) / 2;
		imgY = rect.top  + (margin->bottom - margin->top - imgHeight) / 2;
	}

	if(CheckStyle(TXS_LINENUMBERS))
	{
		rect  = *margin; 

		HANDLE hOldFont = SelectObject(hdc, m_FontAttr[0].hFont);
		
		int  len   = wsprintf(ach, LINENO_FMT, nLineNo + 1);
		int	 width = NeatTextWidth(hdc, ach, len, 0);

		// only draw line number if in-range
		if(nLineNo >= m_nLineCount)
			len = 0;

		rect.right  = rect.left + m_nLinenoWidth;

		if(CheckStyle(TXS_SELMARGIN) && m_hImageList)
		{
			imgX = rect.right;
			rect.right += imgWidth + 4;
		}
		
		SetTextColor(hdc, GetColour(TXC_LINENUMBERTEXT));
		SetBkColor(hdc,   GetColour(TXC_LINENUMBER));

		ExtTextOut(	hdc, 
					rect.left + m_nLinenoWidth - width,
					rect.top  + NeatTextYOffset(&m_FontAttr[0]),
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
	/*	rect.left   = rect.right;
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
	
	if(m_hImageList && linfo)
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
//	Draw a line of text into the TextView window
//
void TextView::PaintText(HDC hdc, ULONG nLineNo, RECT *rect)
{
	TCHAR		buff[TEXTBUFSIZE];
	ATTR		attr[TEXTBUFSIZE];

	ULONG		off_chars;

	int			xpos  = rect->left;
	int			ypos  = rect->top;
	int			xtab  = rect->left;

	ULONG		colno = 0;
	int			len;

	//
	//	TODO: Clip text to left side of window
	//

	TextIterator itor = m_pTextDoc->iterate_line(nLineNo, &off_chars);

	//
	//	Keep drawing until we reach the edge of the window
	//
	while((len = itor.gettext(buff, TEXTBUFSIZE)) > 0)
	{
		//
		//	Apply text attributes - 
		//	i.e. syntax highlighting, mouse selection colours etc.
		//
		len = ApplyTextAttributes(nLineNo, off_chars, colno, buff, len, attr);

		if(len == 0)
			Sleep(0);

		//
		//	Display the text by breaking it into spans of colour/style
		//
		for(int i = 0, lasti = 0; i <= len; i++)
		{
			// if the colour or font changes, then need to output 
			if(i == len || 
				attr[i].fg		!= attr[lasti].fg		|| 
				attr[i].bg		!= attr[lasti].bg		|| 
				attr[i].style	!= attr[lasti].style)
			{
				xpos += NeatTextOut(hdc, xpos, ypos, buff + lasti, i - lasti, xtab, &attr[lasti]);

				lasti = i;
			}
		}

		off_chars += len;
	}

	//
	// Erase to the end of the line
	//
	rect->left = xpos;
	SetBkColor(hdc, LineColour(nLineNo));
	ExtTextOut(hdc, 0, 0, ETO_OPAQUE, rect, 0, 0, 0);
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
	int	font	= nLineNo % m_nNumFonts;
	int i;

	ULONG selstart = min(m_nSelectionStart, m_nSelectionEnd);
	ULONG selend   = max(m_nSelectionStart, m_nSelectionEnd);

	//
	//	STEP 1. Apply the "base coat"
	//
	for(i = 0; i < nTextLen; i++)
	{
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
	for(i = 0; i < nTextLen; i++)
	{
		// apply highlight colours 
		if(nOffset + i >= selstart && nOffset + i < selend)
		{
			if(GetFocus() == m_hWnd)
			{
				attr[i].fg = GetColour(TXC_HIGHLIGHTTEXT);
				attr[i].bg = GetColour(TXC_HIGHLIGHT);
			}
			else
			{
				attr[i].fg = GetColour(TXC_HIGHLIGHTTEXT2);
				attr[i].bg = GetColour(TXC_HIGHLIGHT2);
			}
		}

		// just an example of how to set the font
		if(szText[i] == ' ')
			font = (font + 1) % m_nNumFonts;

		attr[i].style = font;
	}

	//
	//	Turn any CR/LF at the end of a line into a single 'space' character
	//
	return StripCRLF(szText, nTextLen, false);
}

//
//	Draw tabbed text using specified colours and font, return width of output text
//
//	We could just use TabbedTextOut - however, we need to parse the text because
//  it might contain ascii-control characters which must be output separately.
//  because of this we'll just handle the tabs at the same time.
//	
int TextView::NeatTextOut(HDC hdc, int xpos, int ypos, TCHAR *szText, int nLen, int nTabOrigin, ATTR *attr)
{
	int   i;
	int   xold = xpos;
	int   lasti = 0;
	SIZE  sz;
	
	const int TABWIDTHPIXELS = m_nTabWidthChars * m_nFontWidth;

	FONT *font = &m_FontAttr[attr->style];

	// configure the device context
	SetTextColor (hdc, attr->fg);
	SetBkColor   (hdc, attr->bg);
	SelectObject (hdc, font->hFont);

	DWORD flag;

	/*if(m_fTransparent && attr->bg == GetColour(TXC_BACKGROUND))
	{
		SetBkMode(hdc, TRANSPARENT);
		flag = ETO_CLIPPED;
	}
	else*/
	{
		SetBkMode(hdc, OPAQUE);
		flag = ETO_CLIPPED | ETO_OPAQUE;
	}

	// loop over each character
	for(i = 0; i <= nLen; )
	{
		int  yoff = NeatTextYOffset(font);

		// output any "deferred" text before handling tab/control chars
		if(i == nLen || szText[i] == '\t' || (TBYTE)szText[i] < 32)
		{
			RECT rect;

			// get size of text
			GetTextExtentPoint32(hdc, szText + lasti, i - lasti, &sz);
			SetRect(&rect, xpos, ypos, xpos+sz.cx, ypos+m_nLineHeight);

			// draw the text and erase it's background at the same time
			ExtTextOut(hdc, xpos, ypos+yoff, flag, &rect, szText + lasti, i - lasti, 0);
			
			xpos += sz.cx;
		}

		// special case for TAB and Control characters
		if(i < nLen)
		{
			// TAB characters
			if(szText[i] == '\t')
			{
				// calculate distance in pixels to the next tab-stop
				int width = TABWIDTHPIXELS - ((xpos - nTabOrigin) % TABWIDTHPIXELS);
				
				// draw a blank space 
				if(flag != ETO_CLIPPED)
					PaintRect(hdc, xpos, ypos, width, m_nLineHeight, attr->bg);
				
				xpos += width;
				lasti = i + 1;
			}
			// ASCII-CONTROL characters
			else if((TBYTE)szText[i] < 32)
			{
				xpos += PaintCtrlChar(hdc, xpos, ypos, szText[i], font);
				lasti = i + 1;
			}
		}

	/*	if(szText[i] >= 0xD800 && szText[i] <= 0xDBFF)
			i+=2;
		else*/
			i++;
	}

	// return the width of text drawn
	return xpos - xold;
}

void PaintRect(HDC hdc, int x, int y, int width, int height, COLORREF fill)
{
	RECT rect = { x, y, x+width, y+height };

	fill = SetBkColor(hdc, fill);
	ExtTextOut(hdc, 0, 0, ETO_OPAQUE, &rect, 0, 0, 0);
	SetBkColor(hdc, fill);
}

//
//	Strip CR/LF combinations from the end of a line and
//  replace with a single space character (for drawing purposes)
//
int TextView::StripCRLF(TCHAR *szText, int nLength, bool fAllow)
{
	if(nLength >= 2)
	{
		if(szText[nLength-2] == '\r' && szText[nLength-1] == '\n') 
		{
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
		if(szText[nLength-1] == '\r' && (m_nCRLFMode & TXL_CR) || 
			szText[nLength-1] == '\n' && (m_nCRLFMode & TXL_LF)   )
		{
			// convert CR or LF to a single space
			szText[nLength-1] = ' ';
			return nLength - (int)fAllow;
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

	//if(idx == 13)
	//	__asm int 3;
	
	rgbOld				 = m_rgbColourList[idx];
	m_rgbColourList[idx] = rgbColour;

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