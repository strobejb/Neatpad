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
	TextCoord	selStart;
	TextCoord	selEnd;
	BOOL		fReplaceSelection = GetSelection(&selStart, &selEnd);
	TextChange	change;

	switch(m_nEditMode)
	{
	case MODE_READONLY:
		return 0;

	case MODE_INSERT:

		// if there is a selection then remove it
		if(fReplaceSelection)
		{
			// group this erase with the insert/replace operation
			m_pTextDoc->undo_group_begin();
			m_pTextDoc->erase_text(&selStart, &selEnd, &change);
			AdjustCoordsForChange(&change);
		}

		if(!m_pTextDoc->insert_text(&selStart, szText, nLength, &change))
			return 0;

		AdjustCoordsForChange(&change);

		if(fReplaceSelection)
			m_pTextDoc->undo_group_end();

		break;

	case MODE_OVERWRITE:

		// without a selection, overwrite the text after the caret but never the CR/LF
		if(!fReplaceSelection)
		{
			USPCACHE *uspCache = GetUspCache(0, &m_cursorPos);
			ULONG lineEnd;
			ULONG endPos;

			if(uspCache == 0)
				return 0;

			lineEnd = uspCache->length_CRLF;

			// single-character overwrite - must behave like 'forward delete'
			// and remove a whole character-cluster (i.e. maybe more than 1 char)
			if(nLength == 1)
			{
				MoveCharNext();
				endPos = m_cursorPos.line_begin == selStart.line_begin ? m_cursorPos.line_offset_chars : lineEnd;
				m_cursorPos = selStart;
			}
			else
			{
				endPos = selStart.line_offset_chars + nLength;
			}

			if(!m_pTextDoc->coord_from_line_pos(&selStart, min(endPos, lineEnd), &selEnd))
				return 0;
		}

		if(!m_pTextDoc->replace_text(&selStart, &selEnd, szText, nLength, &change))
			return 0;

		AdjustCoordsForChange(&change);
		break;

	default:
		return 0;
	}

	// the caret follows the inserted text
	m_pTextDoc->coord_from_byte_anchor(change.offset + change.inserted, &m_cursorPos);
	m_selAnchor = m_cursorPos;

	// we altered the document, recalculate line+scrollbar information
	ResetLineCache();
	RefreshWindow();

	UpdateViewState(TRUE);
	NotifyParent(TVN_CURSOR_CHANGE);

	return nLength;
}

BOOL TextView::ForwardDelete()
{
	TextCoord	selStart;
	TextCoord	selEnd;
	TextChange	change;

	if(GetSelection(&selStart, &selEnd))
	{
		m_pTextDoc->erase_text(&selStart, &selEnd, &change);
		m_pTextDoc->undo_group_break();
	}
	else
	{
		// erase the whole character-cluster after the caret
		MoveCharNext();
		m_pTextDoc->erase_text(&selStart, &m_cursorPos, &change);
	}

	AdjustCoordsForChange(&change);

	m_pTextDoc->coord_from_byte_anchor(change.offset, &m_cursorPos);
	m_selAnchor = m_cursorPos;

	ResetLineCache();
	RefreshWindow();
	UpdateViewState(FALSE);

	return TRUE;
}

BOOL TextView::BackDelete()
{
	TextCoord	selStart;
	TextCoord	selEnd;
	TextChange	change = { m_cursorPos.byte_anchor, 0, 0 };

	// if there's a selection then delete it
	if(GetSelection(&selStart, &selEnd))
	{
		m_pTextDoc->erase_text(&selStart, &selEnd, &change);
		m_pTextDoc->undo_group_break();
	}
	// otherwise erase the character-cluster before the caret
	else if(m_cursorPos.byte_anchor > 0)
	{
		MoveCharPrev();
		m_pTextDoc->erase_text(&m_cursorPos, &selEnd, &change);
	}

	AdjustCoordsForChange(&change);

	m_pTextDoc->coord_from_byte_anchor(change.offset, &m_cursorPos);
	m_selAnchor = m_cursorPos;

	ResetLineCache();
	RefreshWindow();
	UpdateViewState(FALSE);

	return TRUE;
}

void TextView::UpdateViewState(BOOL fAdvancing)
{
	m_nLineCount   = m_pTextDoc->linecount();

	UpdateMetrics();
	UpdateMarginWidth();
	SetupScrollbars();

	UpdateCaretCoord(&m_cursorPos, fAdvancing, &m_nCaretPosX);
	m_nAnchorPosX = m_nCaretPosX;

	ScrollToCaret();
	RepositionCaret();
}

BOOL TextView::Undo()
{
	TextChange change;

	if(m_nEditMode == MODE_READONLY)
		return FALSE;

	if(!m_pTextDoc->undo(&change))
		return FALSE;

	AdjustCoordsForChange(&change);

	// select the text that came back
	m_pTextDoc->coord_from_byte_anchor(change.offset, &m_selAnchor);
	m_pTextDoc->coord_from_byte_anchor(change.offset + change.inserted, &m_cursorPos);

	ResetLineCache();
	RefreshWindow();

	UpdateViewState(change.inserted != 0);

	return TRUE;
}

BOOL TextView::Redo()
{
	TextChange change;

	if(m_nEditMode == MODE_READONLY)
		return FALSE;

	if(!m_pTextDoc->redo(&change))
		return FALSE;

	AdjustCoordsForChange(&change);

	// select the text that came back
	m_pTextDoc->coord_from_byte_anchor(change.offset, &m_selAnchor);
	m_pTextDoc->coord_from_byte_anchor(change.offset + change.inserted, &m_cursorPos);

	ResetLineCache();
	RefreshWindow();
	UpdateViewState(change.inserted != 0);

	return TRUE;
}

BOOL TextView::CanUndo()
{
	return m_pTextDoc->can_undo() ? TRUE : FALSE;
}

BOOL TextView::CanRedo()
{
	return m_pTextDoc->can_redo() ? TRUE : FALSE;
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
			m_pTextDoc->undo_group_break();

		NotifyParent(TVN_CHANGED);
	}

	return 0;
}
