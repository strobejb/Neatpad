//
//	MODULE:		TextViewFont.cpp
//
//	PURPOSE:	Font support for the TextView control
//
//	NOTES:		www.catch22.net
//

#define STRICT
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <tchar.h>
#include "TextView.h"
#include "TextViewInternal.h"

//
//	TextView::
//
int TextView::NeatTextYOffset(USPFONT *font)
{
	return m_nMaxAscent + m_nHeightAbove - font->tm.tmAscent;
}

int TextView::TextWidth(HDC hdc, TCHAR *buf, int len)
{
	SIZE sz;
	if(len == -1)
		len = lstrlen(buf);
	GetTextExtentPoint32(hdc, buf, len, &sz);
	return sz.cx;
}

//
//	Update the lineheight based on current font settings
//
VOID TextView::RecalcLineHeight()
{
	m_nLineHeight	= 0;
	m_nMaxAscent	= 0;

	// find the tallest font in the TextView
	for(int i = 0; i < m_nNumFonts; i++)
	{
		// always include a font's external-leading
		int fontheight = m_uspFontList[i].tm.tmHeight +
						 m_uspFontList[i].tm.tmExternalLeading;

		m_nLineHeight = max(m_nLineHeight, fontheight);
		m_nMaxAscent  = max(m_nMaxAscent, m_uspFontList[i].tm.tmAscent);
	}

	// add on the above+below spacings
	m_nLineHeight += m_nHeightAbove + m_nHeightBelow;

	// force caret resize if we've got the focus
	if(GetFocus() == m_hWnd)
	{
		OnKillFocus(0);
		OnSetFocus(0);
	}
}

//
//	Set a font for the TextView
//
LONG TextView::SetFont(HFONT hFont, int idx)
{
	USPFONT *uspFont = &m_uspFontList[idx];

	// need a DC to query font data
	HDC hdc  = GetDC(m_hWnd);

	// Initialize the font for USPLIB
	UspFreeFont(uspFont);
	UspInitFont(uspFont, hdc, hFont);

	ReleaseDC(m_hWnd, hdc);

	// calculate new line metrics
	m_nFontWidth = m_uspFontList[0].tm.tmAveCharWidth;

	// lines are measured again in the new font
	m_nLongestLine = 0;

	RecalcLineHeight();
	UpdateMarginWidth();

	ResetLineCache();

	return 0;
}

//
//	Add a secondary font to the TextView
//
LONG TextView::AddFont(HFONT hFont)
{
	int idx;

	if(m_nNumFonts >= MAX_FONTS)
		return -1;

	idx = m_nNumFonts++;
	SetFont(hFont, idx);
	UpdateMetrics();

	return idx;
}

LONG TextView::AddFontFallback(int slot, HFONT hFont)
{
	HDC hdc;
	LONG result;

	if(slot < 0 || slot >= m_nNumFonts)
		return FALSE;

	hdc = GetDC(m_hWnd);
	result = UspAddFontFallback(&m_uspFontList[slot], hdc, hFont);
	ReleaseDC(m_hWnd, hdc);

	if(result)
	{
		RecalcLineHeight();
		UpdateMetrics();
		ResetLineCache();
	}

	return result;
}

LONG TextView::ClearFontFallbacks(int slot)
{
	if(slot < 0 || slot >= m_nNumFonts)
		return FALSE;

	UspClearFontFallbacks(&m_uspFontList[slot]);
	RecalcLineHeight();
	UpdateMetrics();
	ResetLineCache();
	return TRUE;
}
//
//	WM_SETFONT handler: set a new default font
//
LONG TextView::OnSetFont(HFONT hFont)
{
	// default font is always #0
	SetFont(hFont, 0);
	UpdateMetrics();

	return 0;
}

//
//	Set spacing (in pixels) above and below each line - 
//  this is in addition to the external-leading of a font
//
LONG TextView::SetLineSpacing(int nAbove, int nBelow)
{
	m_nHeightAbove = nAbove;
	m_nHeightBelow = nBelow;
	RecalcLineHeight();
	return TRUE;
}
