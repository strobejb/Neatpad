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

static const char *CtrlStr(DWORD ch)
{
	static const char *reps[] = 
	{
		"NUL", "SOH", "STX", "ETX", "EOT", "ENQ", "ACK", "BEL",
		"BS",  "HT",  "LF",  "VT",  "FF",  "CR",  "SO",  "SI",
		"DLE", "DC1", "DC2", "DC3", "DC4", "NAK", "SYN", "ETB",
		"CAN", "EM",  "SUB", "ESC", "FS",  "GS",  "RS",  "US"
	};

	return ch < ' ' ? reps[ch] : "???";
}

void PaintRect(HDC hdc, RECT *rect, COLORREF fill)
{
	fill = SetBkColor(hdc, fill);
	
	ExtTextOut(hdc, 0, 0, ETO_OPAQUE, rect, 0, 0, 0);
	
	SetBkColor(hdc, fill);
}

int TextView::GetCtrlCharWidth(HDC hdc, ULONG chValue, FONT_ATTR *fa)
{
	SIZE sz;
	const char *str = CtrlStr(chValue % 32);
	GetTextExtentPoint32(hdc, str, strlen(str), &sz);
	return sz.cx + 4;
}

void TextView::InitCtrlCharFontAttr(HDC hdc, FONT_ATTR *fa)
{
	// create a temporary off-screen bitmap
	HDC		hdcTemp = CreateCompatibleDC(hdc);
	HBITMAP hbmTemp = CreateBitmap(fa->tm.tmAveCharWidth, fa->tm.tmHeight, 1, 1, 0);
	HANDLE  hbmOld  = SelectObject(hdcTemp, hbmTemp);
	HANDLE  hfnOld	= SelectObject(hdcTemp, fa->hFont);

	SetBkColor(hdcTemp, RGB(255,255,255));
	SetTextColor(hdcTemp, RGB(0,0,0));
	TextOut(hdcTemp, 0, 0, "E", 1);

	fa->nInternalLeading = fa->tm.tmInternalLeading;
	fa->nDescent		 = fa->tm.tmDescent;


	// scan downwards looking for the top of the letter 'E'
	for(int y = 0; y < fa->tm.tmHeight; y++)
	{
		COLORREF col;

		if((col = GetPixel(hdcTemp, fa->tm.tmAveCharWidth / 2, y)) == RGB(0,0,0))
		{
			fa->nInternalLeading = y;
			break;
		}
	}

	// give bigger fonts a 2-pixel border
	if(fa->nInternalLeading > 1 && fa->nDescent > 1 && fa->tm.tmHeight > 18)
	{
		fa->nInternalLeading--;
		fa->nDescent--;
	}

	// scan upwards looking for the bottom of the letter 'E'
//	for(y = fa->tm.tmHeight; y >= 0; y--)
//	{
//	}

	// cleanup
	SelectObject(hdcTemp, hbmOld);
	SelectObject(hdcTemp, hfnOld);
	DeleteDC(hdcTemp);
	DeleteObject(hbmTemp);
}

int TextView::PaintCtrlChar(HDC hdc, int xpos, int ypos, ULONG chValue, BOOL fFillBackground, FONT_ATTR *fa)
{
	POINT pt;
	SIZE  sz;
	RECT  rect;
	const char *str = CtrlStr(chValue % 32);

	int yoff = m_nMaxAscent - fa->tm.tmAscent;

	COLORREF fg = GetTextColor(hdc);
	COLORREF bg = GetBkColor(hdc); 

	// remember current position
	//GetCurrentPositionEx(hdc, &pt);
	pt.x = xpos;
	pt.y = ypos;

	// find out how big the text will be
	GetTextExtentPoint32(hdc, str, strlen(str), &sz);

	SetRect(&rect, pt.x, pt.y, pt.x + sz.cx + 4, pt.y + m_nLineHeight);

	// paint the background white (optional)
	if(fFillBackground)
	{
		PaintRect(hdc, &rect, bg);
	}

	// adjust rectangle for first black block
	rect.right  -= 1;
	rect.top    += fa->nInternalLeading + yoff;
	rect.bottom =  rect.top + fa->tm.tmHeight - fa->nDescent - fa->nInternalLeading;

	// paint the first black block
	PaintRect(hdc, &rect, fg);
	
	// prepare device context
	fg = SetTextColor(hdc, bg);
	bg = SetBkColor(hdc, fg);
	MoveToEx(hdc, pt.x + 1, pt.y, 0);
	
	// paint the text and the second "black" block at the same time
	InflateRect(&rect, -1, 1);
	ExtTextOut(hdc, xpos+1, ypos+yoff, ETO_OPAQUE|ETO_CLIPPED, &rect, str, strlen(str), 0);
	
	// restore device context
	SetTextColor(hdc, fg);
	SetBkColor(hdc, bg);
	MoveToEx(hdc, pt.x + sz.cx + 4, pt.y, 0);
	
	return sz.cx + 4;
}
