//
//	MODULE:		TextViewKeyInput.cpp
//
//	PURPOSE:	Keyboard input for TextView
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
//	TextView::EnterText
//
//	Import the specified text into the TextView at the current
//	cursor position, replacing any text-selection in the process
//
ULONG TextView::EnterText(TCHAR *szText, ULONG nLength)
{
	ULONG selstart = min(m_nSelectionStart, m_nSelectionEnd);
	ULONG selend   = max(m_nSelectionStart, m_nSelectionEnd);

	BOOL  fReplaceSelection = (selstart == selend) ? FALSE : TRUE;
	ULONG erase_len = nLength;

	switch(m_nEditMode)
	{
	case MODE_READONLY:
		return 0;

	case MODE_INSERT:

		// if there is a selection then remove it
		if(fReplaceSelection)
		{
			// group this erase with the insert/replace operation
			m_pTextDoc->m_seq.group();
			m_pTextDoc->erase_text(selstart, selend-selstart);
			m_nCursorOffset = selstart;
		}

		if(!m_pTextDoc->insert_text(m_nCursorOffset, szText, nLength))
			return 0;

		if(fReplaceSelection)
			m_pTextDoc->m_seq.ungroup();
	
		break;

	case MODE_OVERWRITE:

		if(fReplaceSelection)
		{
			erase_len = selend - selstart;
			m_nCursorOffset = selstart;
		}
		else
		{
			ULONG lineoff;
			USPCACHE *uspCache = GetUspCache(0, m_nCurrentLine, &lineoff);

			// single-character overwrite - must behave like 'forward delete'
			// and remove a whole character-cluster (i.e. maybe more than 1 char)
			if(nLength == 1)
			{
				ULONG oldpos = m_nCursorOffset;
				MoveCharNext();
				erase_len = m_nCursorOffset - oldpos;
				m_nCursorOffset = oldpos;
			}

			// if we are at the end of a line (just before the CRLF) then we must
			// not erase any text - instead we act like a regular insertion
			if(m_nCursorOffset == lineoff + uspCache->length_CRLF)
				erase_len = 0;

			
		}

		if(!m_pTextDoc->replace_text(m_nCursorOffset, szText, nLength, erase_len))
			return 0;
		
		break;

	default:
		return 0;
	}

	// update cursor+selection positions
	m_nCursorOffset  += nLength;
	m_nSelectionStart = m_nCursorOffset;
	m_nSelectionEnd   = m_nCursorOffset;

	// we altered the document, recalculate line+scrollbar information
	ResetLineCache();
	RefreshWindow();
	
	Smeg(TRUE);
	NotifyParent(TVN_CURSOR_CHANGE);

	return nLength;
}

BOOL TextView::ForwardDelete()
{
	ULONG selstart = min(m_nSelectionStart, m_nSelectionEnd);
	ULONG selend   = max(m_nSelectionStart, m_nSelectionEnd);

	if(selstart != selend)
	{
		m_pTextDoc->erase_text(selstart, selend-selstart);
		m_nCursorOffset = selstart;

		m_pTextDoc->m_seq.breakopt();
	}
	else
	{
		BYTE tmp[2];
		//USPCACHE		* uspCache;
		//CSCRIPT_LOGATTR * logAttr;
		//ULONG			  lineOffset;
		//ULONG			  index;

		m_pTextDoc->m_seq.render(m_nCursorOffset, tmp, 2);

		/*GetLogAttr(m_nCurrentLine, &uspCache, &logAttr, &lineOffset);
	
		index = m_nCursorOffset - lineOffset;

		do
		{
			m_pTextDoc->seq.erase(m_nCursorOffset, 1);
			index++;
		}
		while(!logAttr[index].fCharStop);*/

		ULONG oldpos = m_nCursorOffset;
		MoveCharNext();

		m_pTextDoc->erase_text(oldpos, m_nCursorOffset - oldpos);
		m_nCursorOffset = oldpos;
		

		//if(tmp[0] == '\r')
		//	m_pTextDoc->erase_text(m_nCursorOffset, 2);
		//else
		//	m_pTextDoc->erase_text(m_nCursorOffset, 1);
	}

	m_nSelectionStart = m_nCursorOffset;
	m_nSelectionEnd   = m_nCursorOffset;

	ResetLineCache();
	RefreshWindow();
	Smeg(FALSE);

	return TRUE;
}

BOOL TextView::BackDelete()
{
	ULONG selstart = min(m_nSelectionStart, m_nSelectionEnd);
	ULONG selend   = max(m_nSelectionStart, m_nSelectionEnd);

	// if there's a selection then delete it
	if(selstart != selend)
	{
		m_pTextDoc->erase_text(selstart, selend-selstart);
		m_nCursorOffset = selstart;
		m_pTextDoc->m_seq.breakopt();
	}
	// otherwise do a back-delete
	else if(m_nCursorOffset > 0)
	{
		//m_nCursorOffset--;
		ULONG oldpos = m_nCursorOffset;
		MoveCharPrev();
		//m_pTextDoc->erase_text(m_nCursorOffset, 1);
		m_pTextDoc->erase_text(m_nCursorOffset, oldpos - m_nCursorOffset);
	}

	m_nSelectionStart = m_nCursorOffset;
	m_nSelectionEnd   = m_nCursorOffset;

	ResetLineCache();
	RefreshWindow();
	Smeg(FALSE);

	return TRUE;
}

void TextView::Smeg(BOOL fAdvancing)
{
	m_pTextDoc->init_linebuffer();

	m_nLineCount   = m_pTextDoc->linecount();

	UpdateMetrics();
	UpdateMarginWidth();
	SetupScrollbars();

	UpdateCaretOffset(m_nCursorOffset, fAdvancing, &m_nCaretPosX, &m_nCurrentLine);
	
	m_nAnchorPosX = m_nCaretPosX;
	ScrollToPosition(m_nCaretPosX, m_nCurrentLine);
	RepositionCaret();
}

BOOL TextView::Undo()
{
	if(m_nEditMode == MODE_READONLY)
		return FALSE;

	if(!m_pTextDoc->Undo(&m_nSelectionStart, &m_nSelectionEnd))
		return FALSE;

	m_nCursorOffset = m_nSelectionEnd;

	ResetLineCache();
	RefreshWindow();

	Smeg(m_nSelectionStart != m_nSelectionEnd);

	return TRUE;
}

BOOL TextView::Redo()
{
	if(m_nEditMode == MODE_READONLY)
		return FALSE;

	if(!m_pTextDoc->Redo(&m_nSelectionStart, &m_nSelectionEnd))
		return FALSE;

	m_nCursorOffset = m_nSelectionEnd;
				
	ResetLineCache();
	RefreshWindow();
	Smeg(m_nSelectionStart != m_nSelectionEnd);

	return TRUE;
}

BOOL TextView::CanUndo()
{
	return m_pTextDoc->m_seq.canundo() ? TRUE : FALSE;
}

BOOL TextView::CanRedo()
{
	return m_pTextDoc->m_seq.canredo() ? TRUE : FALSE;
}

LONG TextView::OnChar(UINT nChar, UINT nFlags)
{
	WCHAR ch = (WCHAR)nChar;

	if(nChar < 32 && nChar != '\t' && nChar != '\r' && nChar != '\n')
		return 0;

	// change CR into a CR/LF sequence
	if(nChar == '\r')
		PostMessage(m_hWnd, WM_CHAR, '\n', 1);

	if(EnterText(&ch, 1))
	{
		if(nChar == '\n')
			m_pTextDoc->m_seq.breakopt();

		NotifyParent(TVN_CHANGED);
	}

	return 0;
}
