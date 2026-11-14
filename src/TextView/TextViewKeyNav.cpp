//
//	MODULE:		TextViewKeyNav.cpp
//
//	PURPOSE:	Keyboard navigation for TextView
//
//	NOTES:		www.catch22.net
//

#define STRICT
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <tchar.h>
#include "TextView.h"
#include "TextViewInternal.h"

/*struct SCRIPT_LOGATTR
{ 
  BYTE fSoftBreak	:1; 
  BYTE fWhiteSpace	:1; 
  BYTE fCharStop	:1; 
  BYTE fWordStop	:1; 
  BYTE fInvalid		:1; 
  BYTE fReserved	:3; 
};*/


//
//	Get the layout for the line containing coord
//
bool TextView::GetLineLayout(TextCoord *line, LINECACHE **lineCache)
{
	return (*lineCache = GetLineCache(line)) != 0;
}

//
//	The position at the end of the previous line's text (before its CR/LF)
//
bool TextView::CoordAtPreviousLineEnd(TextCoord *coord, TextCoord *result)
{
	LINECACHE *lineCache;
	LineIterator line = m_pTextDoc->line_at(*coord);
	TextCoord target;

	if(!line.prev())
		return false;

	target = line.line();

	if(!GetLineLayout(&target, &lineCache))
		return false;

	return m_pTextDoc->coord_from_line_pos(&target, lineCache->length_CRLF, result);
}

//
//	The position at the start of the next line
//
bool TextView::CoordAtNextLineStart(TextCoord *coord, TextCoord *result)
{
	LineIterator line = m_pTextDoc->line_at(*coord);

	if(!line.next())
		return false;

	*result = line.line();
	return true;
}

//
//	Move the caret to a UTF-16 position within its current line
//
void TextView::SetCursorLinePos(int charPos)
{
	TextCoord coord;

	if(m_pTextDoc->coord_from_line_pos(&m_cursorPos, charPos, &coord))
		SetCursorCoord(&coord);
}

//
//	Move caret up specified number of lines
//
void TextView::MoveLineUp(int numLines)
{
	TextLayout		* layout;
	TextCoord		  target;
	TextCoord		  coord;

	int				  charPos;
	bool			  trailing;

	if(numLines <= 0 || m_pTextDoc->byte_length() == 0)
		return;

	if(!m_pTextDoc->previous_line_from_coord(&m_cursorPos, numLines, &target))
		return;

	// get the layout for the target line
	if((layout = GetLayout(&target)) == 0)
		return;

	// move up to character nearest the caret-anchor positions
	layout->XToOffset(m_nAnchorPosX, &charPos, &trailing);

	if(m_pTextDoc->coord_from_line_pos(&target, charPos + trailing, &coord))
		SetCursorCoord(&coord);
}

//
//	Move caret down specified number of lines
//
void TextView::MoveLineDown(int numLines)
{
	TextLayout		* layout;
	TextCoord		  target;
	TextCoord		  coord;

	int				  charPos;
	bool			  trailing;

	if(numLines <= 0 || m_pTextDoc->byte_length() == 0)
		return;

	if(!m_pTextDoc->next_line_from_coord(&m_cursorPos, numLines, &target))
		return;

	// get the layout for the target line
	if((layout = GetLayout(&target)) == 0)
		return;

	// move down to character nearest the caret-anchor position
	layout->XToOffset(m_nAnchorPosX, &charPos, &trailing);

	if(m_pTextDoc->coord_from_line_pos(&target, charPos + trailing, &coord))
		SetCursorCoord(&coord);
}

//
//	Move to start of previous word (to the left)
//
void TextView::MoveWordPrev()
{
	LINECACHE		* lineCache;
	TextCoord		  coord;
	int				  charPos;

	if(!GetLineLayout(&m_cursorPos, &lineCache))
		return;

	TextLayout *layout = lineCache->layout;

	// move 1 character to left
	charPos = (int)min_of(m_cursorPos.column, (size_w)lineCache->length_CRLF) - 1;

	// skip to end of *previous* line if necessary
	if(charPos < 0)
	{
		if(CoordAtPreviousLineEnd(&m_cursorPos, &coord))
			SetCursorCoord(&coord);

		return;
	}

	// skip preceding whitespace
	while(charPos > 0 && layout->IsWhiteSpace(charPos))
		charPos--;

	// skip whole characters until we hit a word-break/more whitespace
	for( ; charPos > 0 ; charPos--)
	{
		if(layout->IsWordStop(charPos) || layout->IsWhiteSpace(charPos-1))
			break;
	}

	SetCursorLinePos(charPos);
}

//
//	Move to start of next word
//
void TextView::MoveWordNext()
{
	LINECACHE		* lineCache;
	TextCoord		  coord;
	int				  charPos;

	if(!GetLineLayout(&m_cursorPos, &lineCache))
		return;

	TextLayout *layout = lineCache->layout;

	charPos = (int)min_of(m_cursorPos.column, (size_w)lineCache->length_CRLF);

	// if already at end-of-line, skip to next line
	if(charPos == lineCache->length_CRLF)
	{
		if(CoordAtNextLineStart(&m_cursorPos, &coord))
			SetCursorCoord(&coord);

		return;
	}

	// if already on a word-break, go to next char
	if(layout->IsWordStop(charPos))
		charPos++;

	// skip whole characters until we hit a word-break/more whitespace
	for( ; charPos < lineCache->length_CRLF; charPos++)
	{
		if(layout->IsWordStop(charPos) || layout->IsWhiteSpace(charPos))
			break;
	}

	// skip trailing whitespace
	while(charPos < lineCache->length_CRLF && layout->IsWhiteSpace(charPos))
		charPos++;

	SetCursorLinePos(charPos);
}

//
//	Move to start of current word
//
void TextView::MoveWordStart()
{
	LINECACHE		* lineCache;
	int				  charPos;

	if(!GetLineLayout(&m_cursorPos, &lineCache))
		return;

	TextLayout *layout = lineCache->layout;

	charPos = (int)min_of(m_cursorPos.column, (size_w)lineCache->length_CRLF);

	while(charPos > 0 && !layout->IsWhiteSpace(charPos-1))
		charPos--;

	SetCursorLinePos(charPos);
}

//
//	Move to end of current word
//
void TextView::MoveWordEnd()
{
	LINECACHE		* lineCache;
	int				  charPos;

	if(!GetLineLayout(&m_cursorPos, &lineCache))
		return;

	TextLayout *layout = lineCache->layout;

	charPos = (int)min_of(m_cursorPos.column, (size_w)lineCache->length_CRLF);

	while(charPos < lineCache->length_CRLF && !layout->IsWhiteSpace(charPos))
		charPos++;

	SetCursorLinePos(charPos);
}

//
//	Move to previous character
//
void TextView::MoveCharPrev()
{
	LINECACHE		* lineCache;
	TextCoord		  coord;
	int				  charPos;

	if(!GetLineLayout(&m_cursorPos, &lineCache))
		return;

	TextLayout *layout = lineCache->layout;

	charPos = (int)min_of(m_cursorPos.column, (size_w)lineCache->length_CRLF);

	// find the previous valid character-position
	for( --charPos; charPos >= 0; charPos--)
	{
		if(layout->IsCursorStop(charPos))
			break;
	}

	// move up to end-of-last line if necessary
	if(charPos < 0)
	{
		if(CoordAtPreviousLineEnd(&m_cursorPos, &coord))
			SetCursorCoord(&coord);

		return;
	}

	SetCursorLinePos(charPos);
}

//
//	Move to next character
//
void TextView::MoveCharNext()
{
	LINECACHE		* lineCache;
	TextCoord		  coord;
	int				  charPos;

	if(!GetLineLayout(&m_cursorPos, &lineCache))
		return;

	TextLayout *layout = lineCache->layout;

	charPos = (int)min_of(m_cursorPos.column, (size_w)lineCache->length_CRLF);

	// find the next valid character-position
	for( ++charPos; charPos <= lineCache->length_CRLF; charPos++)
	{
		if(layout->IsCursorStop(charPos))
			break;
	}

	// skip to beginning of next line if we hit the CR/LF
	if(charPos > lineCache->length_CRLF)
	{
		if(CoordAtNextLineStart(&m_cursorPos, &coord))
			SetCursorCoord(&coord);

		return;
	}

	SetCursorLinePos(charPos);
}

void TextView::MoveCurrentLineStart()
{
	SetCursorLinePos(0);
}

void TextView::MoveCurrentLineEnd()
{
	LINECACHE *lineCache;

	if(GetLineLayout(&m_cursorPos, &lineCache))
		SetCursorLinePos(lineCache->length_CRLF);
}

//
//	Move to start of file
//
void TextView::MoveFileStart()
{
	TextCoord coord;

	if(m_pTextDoc->coord_from_pos(TextPos(0), &coord))
		SetCursorCoord(&coord);
}

//
//	Move to end of file
//
void TextView::MoveFileEnd()
{
	TextCoord coord;

	if(m_pTextDoc->coord_from_document_end(&coord))
		SetCursorCoord(&coord);

	ScrollToDocumentEnd();
	RefreshWindow();
}


//
//	Process keyboard-navigation keys
//
int32_t TextView::OnKeyDown(uint32_t nKeyCode, uint32_t nFlags)
{
	bool fCtrlDown	= (m_pHost->Modifiers() & TextViewHost::TVMOD_CTRL) != 0;
	bool fShiftDown	= (m_pHost->Modifiers() & TextViewHost::TVMOD_SHIFT) != 0;
	bool fAdvancing = false;
	TextCoord oldCursor = m_cursorPos;

	//
	//	Process the key-press. Cursor movement is different depending
	//	on if <ctrl> is held down or not, so act accordingly
	//
	switch(nKeyCode)
	{
	case TVK_SHIFT: case TVK_CONTROL:
		return 0;

	// CTRL+Z undo
	case 'z': case 'Z':
		
		if(fCtrlDown && Undo())
			NotifyParent(TVN_CHANGED);

		return 0;

	// CTRL+Y redo
	case 'y': case 'Y':
		
		if(fCtrlDown && Redo()) 
			NotifyParent(TVN_CHANGED);

		return 0;

	// Change insert mode / clipboard copy&paste
	case TVK_INSERT:

		if(fCtrlDown)
		{
			OnCopy();
			NotifyParent(TVN_CHANGED);
		}
		else if(fShiftDown)
		{
			OnPaste();
			NotifyParent(TVN_CHANGED);
		}
		else
		{
			if(m_nEditMode == MODE_INSERT)
				m_nEditMode = MODE_OVERWRITE;

			else if(m_nEditMode == MODE_OVERWRITE)
				m_nEditMode = MODE_INSERT;

			NotifyParent(TVN_EDITMODE_CHANGE);
		}

		return 0;

	case TVK_DELETE:

		if(m_nEditMode != MODE_READONLY)
		{
			if(fShiftDown)
				OnCut();
			else
				ForwardDelete();

			NotifyParent(TVN_CHANGED);
		}
		return 0;

	case TVK_BACK:

		if(m_nEditMode != MODE_READONLY)
		{
			BackDelete();
			fAdvancing = false;

			NotifyParent(TVN_CHANGED);
		}
		return 0;

	case TVK_LEFT:

		if(fCtrlDown)	MoveWordPrev();
		else			MoveCharPrev();

		fAdvancing = false;
		break;

	case TVK_RIGHT:
		
		if(fCtrlDown)	MoveWordNext();
		else			MoveCharNext();
			
		fAdvancing = true;
		break;

	case TVK_UP:
		if(fCtrlDown)	Scroll(0, -1);
		else			MoveLineUp(1);
		break;

	case TVK_DOWN:
		if(fCtrlDown)	Scroll(0, 1);
		else			MoveLineDown(1);
		break;

	// page up/down scroll the view and the caret together, so the caret keeps its row
	case TVK_PRIOR:
		if(!fCtrlDown)
		{
			Scroll(0, -m_nWindowLines);
			MoveLineUp(m_nWindowLines);
		}
		break;

	case TVK_NEXT:
		if(!fCtrlDown)
		{
			Scroll(0, m_nWindowLines);
			MoveLineDown(m_nWindowLines);
		}
		break;

	case TVK_HOME:
		if(fCtrlDown)	MoveFileStart();
		else			MoveCurrentLineStart();
		break;

	case TVK_END:
		if(fCtrlDown)	MoveFileEnd();
		else			MoveCurrentLineEnd();
		break;

	default:
		return 0;
	}

	// Extend selection if <shift> is down: the anchor stays where it is
	if(fShiftDown)
	{		
		InvalidateRange(&oldCursor, &m_cursorPos);
	}
	// Otherwise clear the selection
	else
	{
		bool hadSelection = m_selAnchor != oldCursor;

		if(hadSelection)
			InvalidateRange(&m_selAnchor, &oldCursor);

		m_selAnchor = m_cursorPos;

		if(hadSelection)
			RefreshWindow();
	}

	// update caret-location (xpos, line#)
	UpdateCaretCoord(&m_cursorPos, fAdvancing, &m_nCaretPosX);
	
	// maintain the caret 'anchor' position *except* for up/down actions
	if(nKeyCode != TVK_UP && nKeyCode != TVK_DOWN)
	{
		m_nAnchorPosX = m_nCaretPosX;

		// scroll as necessary to keep caret within viewport
		ScrollToCaret();
	}
	else
	{
		// scroll as necessary to keep caret within viewport
		if(!fCtrlDown)
			ScrollToCaret();
	}

	NotifyCursorChange();

	return 0;
}
