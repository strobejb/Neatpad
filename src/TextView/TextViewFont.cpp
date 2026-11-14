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
//	How far below a row's top text in a font starts, to share the row's baseline
//
int TextView::NeatTextYOffset(int font)
{
	TextViewHost::FontMetrics metrics;

	m_pHost->GetFontMetrics(font, &metrics);
	return m_nMaxAscent + m_nHeightAbove - metrics.ascent;
}

//
//	Update the lineheight based on current font settings
//
VOID TextView::RecalcLineHeight()
{
	m_nLineHeight	= 0;
	m_nMaxAscent	= 0;

	// find the tallest font in the TextView
	for(int i = 0; i < m_pHost->FontCount(); i++)
	{
		TextViewHost::FontMetrics metrics;

		m_pHost->GetFontMetrics(i, &metrics);

		m_nLineHeight = max(m_nLineHeight, metrics.height);
		m_nMaxAscent  = max(m_nMaxAscent, metrics.ascent);
	}

	// add on the above+below spacings
	m_nLineHeight += m_nHeightAbove + m_nHeightBelow;

	// force caret resize if we've got the focus
	if(m_pHost->HasFocus())
	{
		OnKillFocus();
		OnSetFocus();
	}
}

//
//	The host's fonts have changed: measure the lines and the margin again
//
VOID TextView::FontsChanged()
{
	TextViewHost::FontMetrics metrics;

	// calculate new line metrics
	m_pHost->GetFontMetrics(0, &metrics);
	m_nFontWidth = metrics.averageWidth;

	// lines are measured again in the new font
	m_nLongestLine = 0;

	RecalcLineHeight();
	UpdateMarginWidth();
	ResetLineCache();
	UpdateMetrics();
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
