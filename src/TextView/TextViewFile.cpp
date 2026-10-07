//
//	MODULE:		TextViewFile.cpp
//
//	PURPOSE:	TextView file input routines
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
//	
//
LONG TextView::OpenFile(TCHAR *szFileName)
{
	ClearFile();

	if(m_pTextDoc->init(szFileName))
	{
		TextCoord start;
		ULONG linebreak;

		// the first line's ending sets the line format
		m_pTextDoc->coord_from_byte_anchor(0, &start);

		if((linebreak = m_pTextDoc->linebreak_from_coord(&start)) != 0)
			m_nCRLFMode = linebreak;

		m_nLineCount   = m_pTextDoc->linecount();

		m_nHScrollPos  = 0;

		SetCursorCoord(&start);

		m_selAnchor = m_cursorPos;
		m_scrollVPos = m_cursorPos;

		UpdateMarginWidth();
		UpdateMetrics();
		ResetLineCache();
		NotifyCursorChange();
		return TRUE;
	}

	return FALSE;
}

//
//
//
LONG TextView::ClearFile()
{
	if(m_pTextDoc)
	{
		m_pTextDoc->clear();
	}

	ResetLineCache();

	m_nLineCount		= m_pTextDoc->linecount();
	m_nLongestLine		= 0;	// grows as lines are laid out

	m_nHScrollPos		= 0;

	m_cursorPos			= TextCoord();
	m_scrollVPos		= TextCoord();
	m_selAnchor			= TextCoord();
	m_nCaretPosX		= 0;

	UpdateMetrics();
	NotifyCursorChange();

	return TRUE;
}
