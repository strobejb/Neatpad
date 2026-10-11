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
LONG TextView::OpenFile(TCHAR *szFileName, int nEncoding)
{
	bool opened;

	ClearFile();

	if(nEncoding < 0)
		opened = m_pTextDoc->init(szFileName);
	else
		opened = m_pTextDoc->init(szFileName, (TEXT_ENCODING)nEncoding);

	if(opened)
	{
		TextCoord start;
		ULONG linebreak;

		// the first line's ending sets the line format
		m_pTextDoc->coord_from_pos(TextPos(0), &start);

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
		NotifyIndexChange();
		return TRUE;
	}

	return FALSE;
}

//
//	Save the text to a file. Kept as it is, nothing in the view changes.
//	Converted to another encoding or line format, the file no longer matches
//	the text, so it's opened again, in the encoding it was written in.
//
LONG TextView::SaveFile(TCHAR *szFileName, UINT nEncoding, BOOL fBom, UINT nLineBreak)
{
	BOOL fConvert = nEncoding != (UINT)m_pTextDoc->getformat() || nLineBreak != 0;

	if(!m_pTextDoc->save(szFileName, (TEXT_ENCODING)nEncoding, fBom != FALSE, nLineBreak))
		return FALSE;

	return fConvert ? OpenFile(szFileName, (int)nEncoding) : TRUE;
}

//
//
//
LONG TextView::ClearFile()
{
	// whatever was being indexed has gone
	if(m_fIndexing)
	{
		KillTimer(m_hWnd, INDEX_TIMER);
		m_fIndexing = false;
	}

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
	NotifyIndexChange();

	return TRUE;
}

// Indexing in the background: a megabyte at a time, for this long each tick
static const size_w INDEX_CHUNK = 0x100000;
static const int    INDEX_SLICE_MS = 15;

//
//	Start or stop indexing the rest of the document. It's done a slice at a
//	time on a timer, so the window stays responsive meanwhile.
//
LONG TextView::IndexFile(BOOL fIndex)
{
	TVNINDEXINFO info;

	FillIndexInfo(&info);

	if(fIndex && !m_fIndexing && info.nPercent < 100)
	{
		m_fIndexing = SetTimer(m_hWnd, INDEX_TIMER, USER_TIMER_MINIMUM, 0) != 0;
	}
	else if(!fIndex && m_fIndexing)
	{
		KillTimer(m_hWnd, INDEX_TIMER);
		m_fIndexing = false;
	}

	NotifyIndexChange();
	return m_fIndexing == (fIndex != FALSE);
}

//
//	Index for a few milliseconds, then show how far it's got
//
LONG TextView::OnIndexTimer()
{
	LARGE_INTEGER freq, start, now;
	bool          more;

	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&start);

	do
	{
		more = m_pTextDoc->index_next(INDEX_CHUNK);
		QueryPerformanceCounter(&now);
	}
	while(more && (now.QuadPart - start.QuadPart) * 1000 < freq.QuadPart * INDEX_SLICE_MS);

	// done, or no more can be read
	if(!more)
	{
		KillTimer(m_hWnd, INDEX_TIMER);
		m_fIndexing = false;
	}

	IndexChanged();
	return 0;
}

//
//	Show how far indexing has got, after the timer or an edit message has
//	indexed more: line numbers become exact as indexing passes them, and the
//	line count nearer the truth
//
VOID TextView::IndexChanged()
{
	TVNINDEXINFO info;

	FillIndexInfo(&info);

	if(info.nPercent != m_nIndexPercent || !m_fIndexing)
	{
		m_nLineCount = m_pTextDoc->linecount();

		// the count is exact once done, and may need a wider margin
		if(m_fIndexing)
		{
			SetupScrollbars();
		}
		else
		{
			UpdateMarginWidth();
			UpdateMetrics();
		}

		NotifyCursorChange();
		NotifyIndexChange();
	}
}

//
//	How much of the document is indexed, from the top, and whether more is
//	being indexed
//
VOID TextView::FillIndexInfo(TVNINDEXINFO *info)
{
	size_w length  = m_pTextDoc->byte_length();
	size_w indexed = m_pTextDoc->indexed_length();

	// under 100 until every byte is
	info->nPercent   = indexed >= length ? 100 : (UINT)(indexed * 100 / length);
	info->fIndexing  = m_fIndexing;
	info->nLineCount = (unsigned __int64)m_pTextDoc->linecount();
}

//
//	Tell the parent how far indexing has got
//
VOID TextView::NotifyIndexChange()
{
	TVNINDEXINFO info;

	FillIndexInfo(&info);
	m_nIndexPercent = info.nPercent;
	NotifyParent(TVN_INDEX_CHANGE, (NMHDR *)&info);
}
