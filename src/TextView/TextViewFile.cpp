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
		TCHAR linebuf[TEXTBUFSIZE];
		TextCoord start;
		ULONG linelen;

		m_pTextDoc->coord_from_byte_anchor(0, &start);
		linelen = m_pTextDoc->getline(start, linebuf, TEXTBUFSIZE);

		if(linelen >= 2 && linebuf[linelen - 2] == '\r' && linebuf[linelen - 1] == '\n')
			m_nCRLFMode = TXL_CRLF;
		else if(linelen >= 1 && linebuf[linelen - 1] == '\n')
			m_nCRLFMode = TXL_LF;
		else if(linelen >= 1 && linebuf[linelen - 1] == '\r')
			m_nCRLFMode = TXL_CR;

		m_nLineCount   = m_pTextDoc->linecount();
		m_nLongestLine = m_pTextDoc->longestline(m_nTabWidthChars);

		m_nHScrollPos  = 0;

		SetCursorCoord(&start);

		m_selAnchor = m_cursorPos;
		m_scrollVPos = m_cursorPos;

		UpdateMarginWidth();
		UpdateMetrics();
		ResetLineCache();
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
	m_nLongestLine		= m_pTextDoc->longestline(m_nTabWidthChars);

	m_nHScrollPos		= 0;

	m_cursorPos			= TextCoord();
	m_scrollVPos		= TextCoord();
	m_selAnchor			= TextCoord();
	m_nCaretPosX		= 0;

	UpdateMetrics();

	

	return TRUE;
}
