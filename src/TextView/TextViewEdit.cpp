//
//	MODULE:		TextViewEdit.cpp
//
//	PURPOSE:	The Win32 EDIT control messages, so a tool that reads or drives an
//				edit control can use a TextView. They work in UTF-16 character
//				offsets and line numbers, from the document's index.
//
//	NOTES:		www.catch22.net
//
//				An edit control always answers, so these messages never wait for
//				background indexing: a lookup past the indexed part of the
//				document indexes as far as it needs, there and then.
//

#define STRICT
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <tchar.h>
#include "TextView.h"
#include "TextViewInternal.h"

// How much more of the document to index at a time, while a lookup waits
static const size_w EDIT_INDEX_CHUNK = 0x1000000;

//
//	An edit message's offset or line number. They're 32-bit, so one past that
//	reads as -1, the messages' own error value.
//
static LRESULT EditResult(size_w value)
{
	return value > 0x7FFFFFFF ? -1 : (LRESULT)value;
}

//
//	The UTF-16 units in a line break: CR and LF are one each
//
static size_w BreakChars(uint32_t linebreak)
{
	return linebreak == TXL_CRLF ? 2 : linebreak != 0 ? 1 : 0;
}

//
//	Run a lookup, indexing more of the document while it's pending
//
TEXT_RESULT TextView::IndexUntilKnown(const std::function<TEXT_RESULT()> &lookup)
{
	size_w      before = m_pTextDoc->indexed_length();
	TEXT_RESULT result;
	bool        more = true;

	// until the lookup can answer, or there's no more to index
	while((result = lookup()) == TDR_PENDING_INDEX && more)
		more = m_pTextDoc->index_next(EDIT_INDEX_CHUNK);

	// a line lookup a little past the indexed part indexes there itself, too
	if(m_pTextDoc->indexed_length() != before)
		IndexChanged();

	return result;
}

bool TextView::CharFromPos(const TextPos &pos, size_w *chars)
{
	return IndexUntilKnown([&] { return m_pTextDoc->charoffset_from_pos(pos, chars); }) == TDR_OK;
}

//
//	A character offset past the end of the text is its end, as an edit
//	control has it
//
bool TextView::PosFromChar(size_w chars, TextCoord *coord)
{
	if(IndexUntilKnown([&] { return m_pTextDoc->coord_from_charoffset(chars, coord); }) == TDR_OK)
		return true;

	return m_pTextDoc->coord_from_document_end(coord);
}

bool TextView::LineFromPos(const TextPos &pos, size_w *lineno)
{
	return IndexUntilKnown([&] { return m_pTextDoc->lineno_from_pos(pos, lineno); }) == TDR_OK;
}

bool TextView::LineStart(size_w lineno, TextCoord *coord)
{
	return IndexUntilKnown([&] { return m_pTextDoc->coord_from_lineno(lineno, coord); }) == TDR_OK;
}

//
//	The EDIT messages a TextView answers
//
LRESULT TextView::EditMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
	TextCoord start, end;
	size_w    from, to;
	size_w    chars, lineno;

	switch(msg)
	{
	// the length of the text, in characters
	case WM_GETTEXTLENGTH:
		return IndexUntilKnown([&] { return m_pTextDoc->charcount(&chars); }) == TDR_OK ? EditResult(chars) : 0;

	// the line the text ends on, and the lines before it
	case EM_GETLINECOUNT:
		return LineFromPos(TextPos(m_pTextDoc->byte_length()), &lineno) ? EditResult(lineno + 1) : 1;

	case EM_GETFIRSTVISIBLELINE:
		return LineFromPos(m_scrollVPos, &lineno) ? EditResult(lineno) : 0;

	// the selection's ends in document order, through either pointer, and
	// packed into the result while they fit in 16 bits each
	case EM_GETSEL:
		GetSelection(&start, &end);

		if(!CharFromPos(start, &from) || !CharFromPos(end, &to))
			return -1;

		if(wParam)
			*(DWORD *)wParam = (DWORD)EditResult(from);

		if(lParam)
			*(DWORD *)lParam = (DWORD)EditResult(to);

		return from > 0xFFFF || to > 0xFFFF ? -1 : MAKELRESULT(from, to);

	// select from wParam to lParam, with the caret at lParam. -1 for wParam
	// leaves just the caret; offsets past the end of the text are its end.
	case EM_SETSEL:
		if((int)wParam == -1)
		{
			m_selAnchor = m_cursorPos;
		}
		else
		{
			from = (DWORD)wParam;
			to   = (DWORD)lParam;

			if(!PosFromChar(from, &m_selAnchor) || !PosFromChar(to, &m_cursorPos))
				return 0;

			// a selection covers whole characters, so an end between the
			// halves of a surrogate pair moves forward past it
			TextCoord &last = from < to ? m_cursorPos : m_selAnchor;

			if(from != to && CharFromPos(last, &chars) && chars < max(from, to))
				PosFromChar(max(from, to) + 1, &last);
		}

		m_nSelectionType = SEL_NORMAL;

		UpdateViewState(TRUE);
		RefreshWindow();
		return 0;

	// the line a character is on: -1 for the selection's start, and the last
	// line for one past the end of the text
	case EM_LINEFROMCHAR:
		if((int)wParam == -1)
			GetSelection(&start, &end);
		else if(!PosFromChar((DWORD)wParam, &start))
			return 0;

		return LineFromPos(start, &lineno) ? EditResult(lineno) : 0;

	// the character a line starts with: -1 for the caret's line, and -1 back
	// for a line the text doesn't have
	case EM_LINEINDEX:
		lineno = (DWORD)wParam;

		if((int)wParam == -1 && !LineFromPos(m_cursorPos, &lineno))
			return -1;

		return LineStart(lineno, &start) && CharFromPos(start, &chars) ? EditResult(chars) : -1;

	// the characters on the line holding a character, not counting its line
	// break. -1 for the characters on the selection's lines that aren't selected.
	case EM_LINELENGTH:
		if((int)wParam == -1)
		{
			size_w line_begin, line_end;

			GetSelection(&start, &end);

			if(!CharFromPos(start.line_begin, &line_begin) || !CharFromPos(start, &from) ||
			   !CharFromPos(end, &to) || !CharFromPos(end.line_next, &line_end))
				return 0;

			line_end -= BreakChars(m_pTextDoc->linebreak_from_coord(&end));
			return EditResult((from - line_begin) + (line_end > to ? line_end - to : 0));
		}

		if(!PosFromChar((DWORD)wParam, &start) || !CharFromPos(start.line_begin, &from) || !CharFromPos(start.line_next, &to))
			return 0;

		return EditResult(to - from - BreakChars(m_pTextDoc->linebreak_from_coord(&start)));

	// the text from the start, as much as fits in wParam characters with a
	// null after it. A character is never split, so it can stop one short.
	case WM_GETTEXT:
	{
		TCHAR     *buf    = (TCHAR *)lParam;
		TextReader reader = m_pTextDoc->text_from_range(TextPos(0), TextPos(m_pTextDoc->byte_length()));
		uint32_t   len;

		if(buf == 0 || wParam == 0)
			return 0;

		len = reader.read(buf, (uint32_t)min(wParam - 1, (WPARAM)UINT32_MAX));
		buf[len] = 0;
		return len;
	}

	// the text of line wParam, without its line break, in the buffer whose
	// first WORD says how big it is. No null: the count says how much.
	case EM_GETLINE:
	{
		TCHAR     *buf = (TCHAR *)lParam;
		TextReader reader;
		uint32_t   len;

		if(buf == 0 || !LineStart((DWORD)wParam, &start))
			return 0;

		reader = m_pTextDoc->text_from_range(start.line_begin, start.line_next);
		len    = reader.read(buf, *(WORD *)lParam);

		// the only CR or LF a line holds is its line break
		while(len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n'))
			len--;

		return len;
	}

	// where a character is in the window: x across its line, y down from the
	// top line, so one scrolled out of view is outside the window. -1 for no
	// character there, at or past the end of the text.
	case EM_POSFROMCHAR:
	{
		size_w top;
		int    x;

		if(IndexUntilKnown([&] { return m_pTextDoc->coord_from_charoffset((DWORD)wParam, &start); }) != TDR_OK ||
		   start.byte_offset == m_pTextDoc->byte_length())
			return -1;

		if(!LineFromPos(start, &lineno) || !LineFromPos(m_scrollVPos, &top) || !TextCoordToX(&start, FALSE, &x))
			return -1;

		x += LeftMarginWidth() - m_nHScrollPos * m_nFontWidth;
		return MAKELRESULT((short)x, (short)((int)(lineno - top) * m_nLineHeight));
	}

	// the character nearest a point in the window, and its line, 16 bits each
	case EM_CHARFROMPOS:
		MouseCoordToTextCoord((short)LOWORD(lParam), (short)HIWORD(lParam), &start, 0);

		if(!CharFromPos(start, &chars) || !LineFromPos(start, &lineno))
			return -1;

		return MAKELRESULT(chars, lineno);

	// put lParam's text in place of the selection, with the caret after it, as
	// its own undo step, and always undoable. A program can do this whatever
	// the edit mode, as with a read-only edit control, and it always inserts:
	// an edit control has no overwrite mode.
	case EM_REPLACESEL:
	{
		TCHAR *text = (TCHAR *)lParam;
		UINT   mode = m_nEditMode;

		if(text == 0)
			return 0;

		m_nEditMode = MODE_INSERT;
		m_pTextDoc->undo_group_break();

		if(*text == 0)
			OnClear();
		else
			EnterText(text, lstrlen(text));

		m_pTextDoc->undo_group_break();
		m_nEditMode = mode;

		NotifyParent(TVN_CHANGED);
		return 0;
	}

	case EM_SCROLLCARET:
		ScrollToCaret();
		return TRUE;

	// scroll wParam characters across and lParam lines down
	case EM_LINESCROLL:
		Scroll((int)wParam, (int)lParam);
		SetupScrollbars();
		RepositionCaret();
		return TRUE;

	default:
		return 0;
	}
}
