//
//	MODULE:		TextViewKeyInput.cpp
//
//	PURPOSE:	Keyboard input for TextView
//
//	NOTES:		www.catch22.net
//

#include <type_traits>

#include "TextViewInternal.h"
#include "../TextDocument/Unicode.h"

//
//	Whether text is one character: a unit, a UTF-16 surrogate pair, or one
//	UTF-8 sequence
//
static bool IsOneCharacter(const tchar_t *text, uint32_t length)
{
	UTF32 ch32;

	if(length == 1)
		return true;

	if(TCHAR_UTF8)
		return utf8_to_utf32((const UTF8 *)text, length, &ch32) == length;

	return length == 2 && (uint32_t)text[0] >= UNI_SUR_HIGH_START && (uint32_t)text[0] <= UNI_SUR_HIGH_END &&
		   (uint32_t)text[1] >= UNI_SUR_LOW_START && (uint32_t)text[1] <= UNI_SUR_LOW_END;
}

//
//	TextView::EnterText
//
//	Import the specified text into the TextView at the current
//	cursor position, replacing any text-selection in the process
//
uint32_t TextView::EnterText(tchar_t *szText, uint32_t nLength, bool forceInsert)
{
	TextCoord	selStart;
	TextCoord	selEnd;
	bool		fReplaceSelection = GetSelection(&selStart, &selEnd);
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
			LINECACHE *lineCache = GetLineCache(&m_cursorPos);
			size_w lineEnd;
			size_w endPos;

			if(lineCache == 0)
				return 0;

			lineEnd = lineCache->length_CRLF;

			// single-character overwrite - must behave like 'forward delete'
			// and remove a whole character-cluster (i.e. maybe more than 1 char)
			if(IsOneCharacter(szText, nLength))
			{
				MoveCharNext();
				endPos = m_cursorPos.line_begin == selStart.line_begin ? m_cursorPos.column : lineEnd;
				m_cursorPos = selStart;
			}
			else
			{
				endPos = selStart.column + nLength;
			}

			if(!m_pTextDoc->coord_from_line_pos(&selStart, min_of(endPos, lineEnd), &selEnd))
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

	UpdateViewState(true);

	return nLength;
}

bool TextView::ForwardDelete()
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
	UpdateViewState(false);

	return true;
}

bool TextView::BackDelete()
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
	UpdateViewState(false);

	return true;
}

//
//	After an edit (or select-all): refresh the metrics, put the caret in view
//	and tell the parent where it is now
//
void TextView::UpdateViewState(bool fAdvancing)
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

bool TextView::Undo()
{
	TextChange change;

	if(m_nEditMode == MODE_READONLY)
		return false;

	if(!m_pTextDoc->undo(&change))
		return false;

	AdjustCoordsForChange(&change);

	// select the text that came back
	m_pTextDoc->coord_from_pos(change.at, &m_selAnchor);
	m_pTextDoc->coord_from_pos(TextPos(change.at.byte_offset + change.inserted), &m_cursorPos);

	ResetLineCache();
	RefreshWindow();

	UpdateViewState(change.inserted != 0);

	return true;
}

bool TextView::Redo()
{
	TextChange change;

	if(m_nEditMode == MODE_READONLY)
		return false;

	if(!m_pTextDoc->redo(&change))
		return false;

	AdjustCoordsForChange(&change);

	// select the text that came back
	m_pTextDoc->coord_from_pos(change.at, &m_selAnchor);
	m_pTextDoc->coord_from_pos(TextPos(change.at.byte_offset + change.inserted), &m_cursorPos);

	ResetLineCache();
	RefreshWindow();
	UpdateViewState(change.inserted != 0);

	return true;
}

bool TextView::CanUndo()
{
	return m_pTextDoc->can_undo() ? true : false;
}

bool TextView::CanRedo()
{
	return m_pTextDoc->can_redo() ? true : false;
}

// Changing tab stops changes layout, but never the document's characters.
uint32_t TextView::SetTabWidth(uint32_t width)
{
    uint32_t previous = (uint32_t)m_nTabWidthChars;

    m_nTabWidthChars = (int)max_of(1U, min_of(width, (uint32_t)TV_MAX_TAB_WIDTH));
    if ((uint32_t)m_nTabWidthChars != previous)
    {
        m_nLongestLine = 0;
        ResetLineCache();
        UpdateViewState(true);
        RefreshWindow();
    }

    return previous;
}

// TextCoord columns count text units. Expand existing tabs while counting the
// prefix so a typed Tab still reaches the correct tab stop after earlier tabs.
// A UTF-8 character is one column, however many bytes it takes.
static size_w TabColumn(TextDocument *document, const TextCoord &at, uint32_t width)
{
    TextReader reader = document->text_from_range(at.line_begin, at);
    tchar_t      buffer[128];
    uint32_t   length;
    size_w     column = 0;

    while ((length = reader.read(buffer, sizeof(buffer) / sizeof(buffer[0]))) != 0)
    {
        for (uint32_t i = 0; i < length; ++i)
        {
            if (buffer[i] == '\t')
                column += width - column % width;
            else if (!TCHAR_UTF8 || ((uint8_t)buffer[i] & 0xC0) != 0x80)
                column++;
        }
    }

    return column;
}

//
//	Typed text that comes as a string, as an input method gives it: a single
//	unit is a character, which OnChar sees, so Tab and Enter work as typed
//
int32_t TextView::OnText(const tchar_t *text, uint32_t length)
{
	typedef std::make_unsigned<tchar_t>::type unit;

	if(length == 1)
		return OnChar((unit)text[0], 0);

	if(m_nEditMode == MODE_READONLY || length == 0)
		return 0;

	if(EnterText((tchar_t *)text, length))
		NotifyParent(TVNOTIFY_CHANGED);

	return 0;
}

int32_t TextView::OnChar(uint32_t nChar, uint32_t nFlags)
{
    tchar_t ch = (tchar_t)nChar;

    if (m_nEditMode == MODE_READONLY || (nChar < 32 && nChar != '\t' && nChar != '\r' && nChar != '\n'))
        return 0;

    if (nChar == '\t' && CheckStyle(TXS_INSERTSPACES))
    {
        TextCoord start, end;
        GetSelection(&start, &end);
        uint32_t                     width  = (uint32_t)m_nTabWidthChars;
        size_w                   column = TabColumn(m_pTextDoc, start, width);
        std::basic_string<tchar_t> spaces(width - column % width, TSTR(' '));

        if (EnterText(&spaces[0], (uint32_t)spaces.size()))
            NotifyParent(TVNOTIFY_CHANGED);
        return 0;
    }

    // Enter inserts a line break in the document's format, followed by any
    // leading whitespace before the selection's start. Treat it as one edit.
    if (nChar == '\r' || nChar == '\n')
    {
        tchar_t                    crlf[] = TSTR("\r\n");
        tchar_t                   *brk    = m_nCRLFMode == TXL_LF ? crlf + 1 : crlf;
        uint32_t                    length = m_nCRLFMode == TXL_LF || m_nCRLFMode == TXL_CR ? 1 : 2;
        std::basic_string<tchar_t> text(brk, length);

        if (CheckStyle(TXS_AUTOINDENT))
        {
            TextCoord start, end;
            GetSelection(&start, &end);
            TextReader reader = m_pTextDoc->text_from_range(start.line_begin, start);
            tchar_t      buffer[128];
            uint32_t   read;
            bool       leading = true;

            while (leading && (read = reader.read(buffer, sizeof(buffer) / sizeof(buffer[0]))) != 0)
            {
                for (uint32_t i = 0; i < read; ++i)
                {
                    if (buffer[i] == '\t' && CheckStyle(TXS_INSERTSPACES))
                        text.append(m_nTabWidthChars - (text.size() - length) % m_nTabWidthChars, TSTR(' '));
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
        if (EnterText(&text[0], (uint32_t)text.size(), true))
        {
            m_pTextDoc->undo_group_break();
            NotifyParent(TVNOTIFY_CHANGED);
        }

        return 0;
    }

    if (EnterText(&ch, 1))
        NotifyParent(TVNOTIFY_CHANGED);

    return 0;
}

//
//	Make every line break in the selection, or in the whole document if nothing
//	is selected, nLineBreak (TXL_CRLF, TXL_LF or TXL_CR), as one undo step.
//	Converting the whole document gives it that line format, too, and keeps the
//	caret and the view on their lines: converting changes no line's number.
//
bool TextView::ConvertLineBreaks(uint32_t nLineBreak)
{
	TextCoord  start, end, line;
	TextChange change;
	size_w     caretLine = 0;
	size_w     topLine   = 0;
	size_w     column    = m_cursorPos.column;
	bool       whole     = !GetSelection(&start, &end);

	if(m_nEditMode == MODE_READONLY || (nLineBreak != TXL_CRLF && nLineBreak != TXL_LF && nLineBreak != TXL_CR))
		return false;

	if(whole)
	{
		if(!LineFromPos(m_cursorPos, &caretLine) || !LineFromPos(m_scrollVPos, &topLine))
			return false;

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
	UpdateViewState(true);
	NotifyParent(TVNOTIFY_CHANGED);
	return true;
}
