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
ULONG TextView::EnterText(TCHAR *szText, ULONG nLength, bool forceInsert)
{
	TextCoord	selStart;
	TextCoord	selEnd;
	BOOL		fReplaceSelection = GetSelection(&selStart, &selEnd);
	TextChange	change;

    switch (forceInsert && m_nEditMode == MODE_OVERWRITE ? MODE_INSERT : m_nEditMode)
    {
	case MODE_READONLY:
		return 0;

	case MODE_INSERT:

		// if there is a selection then remove it
		if(fReplaceSelection)
		{
			// group this erase with the insert/replace operation
			m_pTextDoc->undo_group_begin();
			m_pTextDoc->erase_text(selStart, selEnd, &change);
			AdjustCoordsForChange(&change);
		}

		if(!m_pTextDoc->insert_text(selStart, szText, nLength, &change))
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
			size_w lineEnd;
			size_w endPos;

			if(uspCache == 0)
				return 0;

			lineEnd = uspCache->length_CRLF;

			// single-character overwrite - must behave like 'forward delete'
			// and remove a whole character-cluster (i.e. maybe more than 1 char)
			if(nLength == 1)
			{
				MoveCharNext();
				endPos = m_cursorPos.line_begin == selStart.line_begin ? m_cursorPos.column : lineEnd;
				m_cursorPos = selStart;
			}
			else
			{
				endPos = selStart.column + nLength;
			}

			if(!m_pTextDoc->coord_from_line_pos(&selStart, min(endPos, lineEnd), &selEnd))
				return 0;
		}

		if(!m_pTextDoc->replace_text(selStart, selEnd, szText, nLength, &change))
			return 0;

		AdjustCoordsForChange(&change);
		break;

	default:
		return 0;
	}

	// the caret follows the inserted text
	m_pTextDoc->coord_from_pos(TextPos(change.at.byte_offset + change.inserted), &m_cursorPos);
	m_selAnchor = m_cursorPos;

	// we altered the document, recalculate line+scrollbar information
	ResetLineCache();
	RefreshWindow();

	UpdateViewState(TRUE);

	return nLength;
}

BOOL TextView::ForwardDelete()
{
	TextCoord	selStart;
	TextCoord	selEnd;
	TextChange	change;

	if(GetSelection(&selStart, &selEnd))
	{
		m_pTextDoc->erase_text(selStart, selEnd, &change);
		m_pTextDoc->undo_group_break();
	}
	else
	{
		// erase the whole character-cluster after the caret
		MoveCharNext();
		m_pTextDoc->erase_text(selStart, m_cursorPos, &change);
	}

	AdjustCoordsForChange(&change);

	m_pTextDoc->coord_from_pos(change.at, &m_cursorPos);
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
	TextChange	change = { m_cursorPos, 0, 0 };

	// if there's a selection then delete it
	if(GetSelection(&selStart, &selEnd))
	{
		m_pTextDoc->erase_text(selStart, selEnd, &change);
		m_pTextDoc->undo_group_break();
	}
	// otherwise erase the character-cluster before the caret
	else if(m_cursorPos.byte_offset > 0)
	{
		MoveCharPrev();
		m_pTextDoc->erase_text(m_cursorPos, selEnd, &change);
	}

	AdjustCoordsForChange(&change);

	m_pTextDoc->coord_from_pos(change.at, &m_cursorPos);
	m_selAnchor = m_cursorPos;

	ResetLineCache();
	RefreshWindow();
	UpdateViewState(FALSE);

	return TRUE;
}

//
//	After an edit (or select-all): refresh the metrics, put the caret in view
//	and tell the parent where it is now
//
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
	NotifyCursorChange();
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
	m_pTextDoc->coord_from_pos(change.at, &m_selAnchor);
	m_pTextDoc->coord_from_pos(TextPos(change.at.byte_offset + change.inserted), &m_cursorPos);

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
	m_pTextDoc->coord_from_pos(change.at, &m_selAnchor);
	m_pTextDoc->coord_from_pos(TextPos(change.at.byte_offset + change.inserted), &m_cursorPos);

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

// Changing tab stops changes layout, but never the document's characters.
UINT TextView::SetTabWidth(UINT width)
{
    UINT previous = (UINT)m_nTabWidthChars;

    m_nTabWidthChars = (int)max(1U, min(width, (UINT)TV_MAX_TAB_WIDTH));
    if ((UINT)m_nTabWidthChars != previous)
    {
        m_nLongestLine = 0;
        ResetLineCache();
        UpdateViewState(TRUE);
        RefreshWindow();
    }

    return previous;
}

// TextCoord columns count UTF-16 units. Expand existing tabs while counting the
// prefix so a typed Tab still reaches the correct tab stop after earlier tabs.
static size_w TabColumn(TextDocument *document, const TextCoord &at, UINT width)
{
    TextReader reader = document->text_from_range(at.line_begin, at);
    TCHAR      buffer[128];
    uint32_t   length;
    size_w     column = 0;

    while ((length = reader.read(buffer, ARRAYSIZE(buffer))) != 0)
    {
        for (uint32_t i = 0; i < length; ++i)
            column += buffer[i] == '\t' ? width - column % width : 1;
    }

    return column;
}

LONG TextView::OnChar(UINT nChar, UINT nFlags)
{
    WCHAR ch = (WCHAR)nChar;

    if (m_nEditMode == MODE_READONLY || (nChar < 32 && nChar != '\t' && nChar != '\r' && nChar != '\n'))
        return 0;

    if (nChar == '\t' && CheckStyle(TXS_INSERTSPACES))
    {
        TextCoord start, end;
        GetSelection(&start, &end);
        UINT                     width  = (UINT)m_nTabWidthChars;
        size_w                   column = TabColumn(m_pTextDoc, start, width);
        std::basic_string<TCHAR> spaces(width - column % width, _T(' '));

        if (EnterText(&spaces[0], (ULONG)spaces.size()))
            NotifyParent(TVN_CHANGED);
        return 0;
    }

    // Enter inserts a line break in the document's format, followed by any
    // leading whitespace before the selection's start. Treat it as one edit.
    if (nChar == '\r' || nChar == '\n')
    {
        TCHAR                    crlf[] = _T("\r\n");
        TCHAR                   *brk    = m_nCRLFMode == TXL_LF ? crlf + 1 : crlf;
        ULONG                    length = m_nCRLFMode == TXL_LF || m_nCRLFMode == TXL_CR ? 1 : 2;
        std::basic_string<TCHAR> text(brk, length);

        if (CheckStyle(TXS_AUTOINDENT))
        {
            TextCoord start, end;
            GetSelection(&start, &end);
            TextReader reader = m_pTextDoc->text_from_range(start.line_begin, start);
            TCHAR      buffer[128];
            uint32_t   read;
            bool       leading = true;

            while (leading && (read = reader.read(buffer, ARRAYSIZE(buffer))) != 0)
            {
                for (uint32_t i = 0; i < read; ++i)
                {
                    if (buffer[i] == '\t' && CheckStyle(TXS_INSERTSPACES))
                        text.append(m_nTabWidthChars - (text.size() - length) % m_nTabWidthChars, _T(' '));
                    else if (buffer[i] == ' ' || buffer[i] == '\t')
                        text.push_back(buffer[i]);
                    else
                    {
                        leading = false;
                        break;
                    }
                }
            }
        }

        m_pTextDoc->undo_group_break();
        if (EnterText(&text[0], (ULONG)text.size(), true))
        {
            m_pTextDoc->undo_group_break();
            NotifyParent(TVN_CHANGED);
        }

        return 0;
    }

    if (EnterText(&ch, 1))
        NotifyParent(TVN_CHANGED);

    return 0;
}

//
//	Make every line break in the selection, or in the whole document if nothing
//	is selected, nLineBreak (TXL_CRLF, TXL_LF or TXL_CR), as one undo step.
//	Converting the whole document gives it that line format, too, and keeps the
//	caret and the view on their lines: converting changes no line's number.
//
BOOL TextView::ConvertLineBreaks(UINT nLineBreak)
{
	TextCoord  start, end, line;
	TextChange change;
	size_w     caretLine = 0;
	size_w     topLine   = 0;
	size_w     column    = m_cursorPos.column;
	bool       whole     = !GetSelection(&start, &end);

	if(m_nEditMode == MODE_READONLY || (nLineBreak != TXL_CRLF && nLineBreak != TXL_LF && nLineBreak != TXL_CR))
		return FALSE;

	if(whole)
	{
		if(!LineFromPos(m_cursorPos, &caretLine) || !LineFromPos(m_scrollVPos, &topLine))
			return FALSE;

		m_pTextDoc->coord_from_pos(TextPos(0), &start);
		m_pTextDoc->coord_from_document_end(&end);
	}

	m_pTextDoc->replace_linebreaks(start, end, nLineBreak, &change);
	AdjustCoordsForChange(&change);
	ResetLineCache();

	if(whole)
	{
		m_nCRLFMode = nLineBreak;

		if(LineStart(topLine, &line))
			SetScrollCoord(&line);

		if(LineStart(caretLine, &line))
			m_pTextDoc->coord_from_line_pos(&line, column, &m_cursorPos);

		m_selAnchor = m_cursorPos;
	}
	else
	{
		// the converted text stays selected
		m_pTextDoc->coord_from_pos(change.at, &m_selAnchor);
		m_pTextDoc->coord_from_pos(TextPos(change.at.byte_offset + change.inserted), &m_cursorPos);
	}

	RefreshWindow();
	UpdateViewState(TRUE);
	NotifyParent(TVN_CHANGED);
	return TRUE;
}
