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

#define TEXTBUFSIZE 128

int		StripCRLF(TCHAR *szText, int nLength);
void	PaintRect(HDC hdc, int x, int y, int width, int height, COLORREF fill);

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
//	Draw a line of text into the TextView window
//
void TextView::PaintText(HDC hdc, ULONG nLineNo, RECT *rect)
{
	TCHAR	buff[TEXTBUFSIZE];
	ATTR	attr[TEXTBUFSIZE];

	ULONG	charoff = 0;
	int		len;

	int		xpos = rect->left;
	int		ypos = rect->top;
	int		xtab = rect->left;

	//
	//	TODO: Clip text to left side of window
	//

	//
	//	Keep drawing until we reach the edge of the window
	//
	while(charoff < m_nWindowColumns + m_nHScrollPos)
	{
		ULONG fileoff;
		int	  lasti = 0;
		int   i;

		//
		//	Get a block of text for drawing
		//
		if((len = m_pTextDoc->getline(nLineNo, charoff, buff, TEXTBUFSIZE, &fileoff)) == 0)
			break;

		// ready for the next block of characters (do this before stripping CR/LF)
		//fileoff += charoff;
		charoff += len;

		//
		//	Apply text attributes - 
		//	i.e. syntax highlighting, mouse selection colours etc.
		//
		len = ApplyTextAttributes(nLineNo, fileoff, buff, len, attr);

		//
		//	Display the text by breaking it into spans of colour/style
		//
		for(i = 0; i <= len; i++)
		{
			// if the colour or font changes, then need to output 
			if(i == len || 
				attr[i].fg != attr[lasti].fg || 
				attr[i].bg != attr[lasti].bg || 
				attr[i].style != attr[lasti].style)
			{
				xpos += NeatTextOut(hdc, xpos, ypos, buff + lasti, i - lasti, xtab, &attr[lasti]);

				lasti = i;
			}
		}
	}

	//
	// Erase to the end of the line
	//
	rect->left = xpos;
	SetBkColor(hdc, GetColour(TXC_BACKGROUND));
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
int TextView::ApplyTextAttributes(ULONG nLineNo, ULONG nOffset, TCHAR *szText, int nTextLen, ATTR *attr)
{
	// randomize this value to give different fonts
	int style = nLineNo % m_nNumFonts;	

	//
	//	TODO: 1. Apply a default single-colour first of all
	//

	//
	//	TODO: 2. Apply syntax colouring (overrides default text)
	//

	//
	//	TODO: 3. Apply bookmarks, line highlighting etc (overrides syntax colouring)
	//

	//
	//	STEP 4:  Now apply text-selection (overrides everything else)
	//
	for(int i = 0; i < nTextLen; i++)
	{
		// apply highlight colours 
		if(nOffset + i >= m_nSelectionStart && nOffset + i < m_nSelectionEnd)
		{
			attr[i].fg = GetColour(TXC_HIGHLIGHTTEXT);
			attr[i].bg = GetColour(TXC_HIGHLIGHT);
		}
		// normal text colours
		else
		{
			attr[i].fg = GetColour(TXC_FOREGROUND);
			attr[i].bg = GetColour(TXC_BACKGROUND);
		}

		attr[i].style = style;
	}

	//
	//	Turn any CR/LF at the end of a line into a single 'space' character
	//	when a selection goes past the end of this line, this extra 'space' will be 
	//  drawn using the highlight colours.
	//
	return StripCRLF(szText, nTextLen);
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

	// loop over each character
	for(i = 0; i <= nLen; i++)
	{
		int  yoff = m_nMaxAscent + m_nHeightAbove - font->tm.tmAscent;

		// output any "deferred" text before handling tab/control chars
		if(i == nLen || szText[i] == '\t' || szText[i] < 32)
		{
			RECT rect;

			// get size of text
			GetTextExtentPoint32(hdc, szText + lasti, i - lasti, &sz);
			SetRect(&rect, xpos, ypos, xpos+sz.cx, ypos+m_nLineHeight);

			// draw the text and erase it's background at the same time
			ExtTextOut(hdc, xpos, ypos+yoff, ETO_OPAQUE, &rect, szText + lasti, i - lasti, 0);
			
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
				PaintRect(hdc, xpos, ypos, width, m_nLineHeight, attr->bg);
				
				xpos += width;
				lasti = i + 1;
			}
			// ASCII-CONTROL characters
			else if(szText[i] < 32)
			{
				xpos += PaintCtrlChar(hdc, xpos, ypos, szText[i], font);
				lasti = i + 1;
			}
		}
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
int StripCRLF(TCHAR *szText, int nLength)
{
	if(nLength >= 2)
	{
		if(szText[nLength-2] == '\r' && szText[nLength-1] == '\n') 
		{
			szText[nLength-2] = ' ';
			return --nLength;
		}
	}

	if(nLength >= 1)
	{
		if(szText[nLength-1] == '\r' || szText[nLength-1] == '\n')
		{
			szText[nLength-1] = ' ';
			nLength--;
		}
	}

	return nLength;
}

//
//	Return an RGB value corresponding to the specified HVC_xxx index
//
COLORREF TextView::GetColour(UINT idx)
{
	switch(idx)
	{
	case TXC_BACKGROUND:	return GetSysColor(COLOR_WINDOW);
	case TXC_FOREGROUND:	return GetSysColor(COLOR_WINDOWTEXT);
	case TXC_HIGHLIGHT:		return GetSysColor(COLOR_HIGHLIGHT);
	case TXC_HIGHLIGHTTEXT:	return GetSysColor(COLOR_HIGHLIGHTTEXT);
	default:				return 0;
	}
}