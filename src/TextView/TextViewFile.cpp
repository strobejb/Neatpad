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
		ULONG lineoff = 0;
		ULONG linelen = m_pTextDoc->getline(0, linebuf, TEXTBUFSIZE, &lineoff);

		if(linelen >= 2 && linebuf[linelen - 2] == '\r' && linebuf[linelen - 1] == '\n')
			m_nCRLFMode = TXL_CRLF;
		else if(linelen >= 1 && linebuf[linelen - 1] == '\n')
			m_nCRLFMode = TXL_LF;
		else if(linelen >= 1 && linebuf[linelen - 1] == '\r')
			m_nCRLFMode = TXL_CR;

		m_nLineCount   = m_pTextDoc->linecount();
		m_nLongestLine = m_pTextDoc->longestline(m_nTabWidthChars);

		m_nVScrollPos  = 0;
		m_nHScrollPos  = 0;

		m_nSelectionStart	= 0;
		m_nSelectionEnd		= 0;
		m_nCursorOffset		= 0;

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

	m_nVScrollPos		= 0;
	m_nHScrollPos		= 0;

	m_nSelectionStart	= 0;
	m_nSelectionEnd		= 0;
	m_nCursorOffset		= 0;

	m_nCurrentLine		= 0;
	m_nCaretPosX		= 0;

	UpdateMetrics();

	

	return TRUE;
}
