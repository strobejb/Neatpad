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


bool IsKeyPressed(UINT nVirtKey)
{
	return GetKeyState(nVirtKey) < 0 ? true : false;
}

//
//	Get the Uniscribe layout and logical attributes for the line containing coord
//
bool TextView::GetLineLayout(TextCoord *line, USPCACHE **uspCache, CSCRIPT_LOGATTR **logAttr)
{
	if((*uspCache = GetUspCache(0, line)) == 0)
		return false;

	if(logAttr && (*logAttr = UspGetLogAttr((*uspCache)->uspData)) == 0)
		return false;

	return true;
}

//
//	The position at the end of the previous line's text (before its CR/LF)
//
bool TextView::CoordAtPreviousLineEnd(TextCoord *coord, TextCoord *result)
{
	USPCACHE *uspCache;
	TextCoord target;

	if(coord->line_begin == 0 || !m_pTextDoc->previous_line_from_coord(coord, 1, &target))
		return false;

	if(!GetLineLayout(&target, &uspCache, 0))
		return false;

	return m_pTextDoc->coord_from_line_pos(&target, uspCache->length_CRLF, result);
}

//
//	The position at the start of the next line
//
bool TextView::CoordAtNextLineStart(TextCoord *coord, TextCoord *result)
{
	TextCoord target;

	if(!m_pTextDoc->next_line_from_coord(coord, 1, &target) || target.line_begin == coord->line_begin)
		return false;

	return m_pTextDoc->coord_from_line_pos(&target, 0, result);
}

//
//	Move the caret to a UTF-16 position within its current line
//
VOID TextView::SetCursorLinePos(int charPos)
{
	TextCoord coord;

	if(m_pTextDoc->coord_from_line_pos(&m_cursorPos, charPos, &coord))
		SetCursorCoord(&coord);
}

//
//	Move caret up specified number of lines
//
VOID TextView::MoveLineUp(int numLines)
{
	USPDATA			* uspData;
	TextCoord		  target;
	TextCoord		  coord;

	int				  charPos;
	BOOL			  trailing;

	if(numLines <= 0 || m_pTextDoc->byte_length() == 0)
		return;

	if(!m_pTextDoc->previous_line_from_coord(&m_cursorPos, numLines, &target))
		return;

	// get Uniscribe data for target line
	if((uspData = GetUspData(0, &target)) == 0)
		return;

	// move up to character nearest the caret-anchor positions
	UspXToOffset(uspData, m_nAnchorPosX, &charPos, &trailing, 0);

	if(m_pTextDoc->coord_from_line_pos(&target, charPos + trailing, &coord))
		SetCursorCoord(&coord);
}

//
//	Move caret down specified number of lines
//
VOID TextView::MoveLineDown(int numLines)
{
	USPDATA			* uspData;
	TextCoord		  target;
	TextCoord		  coord;

	int				  charPos;
	BOOL			  trailing;

	if(numLines <= 0 || m_pTextDoc->byte_length() == 0)
		return;

	if(!m_pTextDoc->next_line_from_coord(&m_cursorPos, numLines, &target))
		return;

	// get Uniscribe data for target line
	if((uspData = GetUspData(0, &target)) == 0)
		return;

	// move down to character nearest the caret-anchor position
	UspXToOffset(uspData, m_nAnchorPosX, &charPos, &trailing, 0);

	if(m_pTextDoc->coord_from_line_pos(&target, charPos + trailing, &coord))
		SetCursorCoord(&coord);
}

//
//	Move to start of previous word (to the left)
//
VOID TextView::MoveWordPrev()
{
	USPCACHE		* uspCache;
	CSCRIPT_LOGATTR * logAttr;
	TextCoord		  coord;
	int				  charPos;

	if(!GetLineLayout(&m_cursorPos, &uspCache, &logAttr))
		return;

	// move 1 character to left
	charPos = (int)min(m_cursorPos.line_offset_chars, (ULONG)uspCache->length_CRLF) - 1;

	// skip to end of *previous* line if necessary
	if(charPos < 0)
	{
		if(CoordAtPreviousLineEnd(&m_cursorPos, &coord))
			SetCursorCoord(&coord);

		return;
	}

	// skip preceding whitespace
	while(charPos > 0 && logAttr[charPos].fWhiteSpace)
		charPos--;

	// skip whole characters until we hit a word-break/more whitespace
	for( ; charPos > 0 ; charPos--)
	{
		if(logAttr[charPos].fWordStop || logAttr[charPos-1].fWhiteSpace)
			break;
	}

	SetCursorLinePos(charPos);
}

//
//	Move to start of next word
//
VOID TextView::MoveWordNext()
{
	USPCACHE		* uspCache;
	CSCRIPT_LOGATTR * logAttr;
	TextCoord		  coord;
	int				  charPos;

	if(!GetLineLayout(&m_cursorPos, &uspCache, &logAttr))
		return;

	charPos = (int)min(m_cursorPos.line_offset_chars, (ULONG)uspCache->length_CRLF);

	// if already at end-of-line, skip to next line
	if(charPos == uspCache->length_CRLF)
	{
		if(CoordAtNextLineStart(&m_cursorPos, &coord))
			SetCursorCoord(&coord);

		return;
	}

	// if already on a word-break, go to next char
	if(logAttr[charPos].fWordStop)
		charPos++;

	// skip whole characters until we hit a word-break/more whitespace
	for( ; charPos < uspCache->length_CRLF; charPos++)
	{
		if(logAttr[charPos].fWordStop || logAttr[charPos].fWhiteSpace)
			break;
	}

	// skip trailing whitespace
	while(charPos < uspCache->length_CRLF && logAttr[charPos].fWhiteSpace)
		charPos++;

	SetCursorLinePos(charPos);
}

//
//	Move to start of current word
//
VOID TextView::MoveWordStart()
{
	USPCACHE		* uspCache;
	CSCRIPT_LOGATTR * logAttr;
	int				  charPos;

	if(!GetLineLayout(&m_cursorPos, &uspCache, &logAttr))
		return;

	charPos = (int)min(m_cursorPos.line_offset_chars, (ULONG)uspCache->length_CRLF);

	while(charPos > 0 && !logAttr[charPos-1].fWhiteSpace)
		charPos--;

	SetCursorLinePos(charPos);
}

//
//	Move to end of current word
//
VOID TextView::MoveWordEnd()
{
	USPCACHE		* uspCache;
	CSCRIPT_LOGATTR * logAttr;
	int				  charPos;

	if(!GetLineLayout(&m_cursorPos, &uspCache, &logAttr))
		return;

	charPos = (int)min(m_cursorPos.line_offset_chars, (ULONG)uspCache->length_CRLF);

	while(charPos < uspCache->length_CRLF && !logAttr[charPos].fWhiteSpace)
		charPos++;

	SetCursorLinePos(charPos);
}

//
//	Move to previous character
//
VOID TextView::MoveCharPrev()
{
	USPCACHE		* uspCache;
	CSCRIPT_LOGATTR * logAttr;
	TextCoord		  coord;
	int				  charPos;

	if(!GetLineLayout(&m_cursorPos, &uspCache, &logAttr))
		return;

	charPos = (int)min(m_cursorPos.line_offset_chars, (ULONG)uspCache->length_CRLF);

	// find the previous valid character-position
	for( --charPos; charPos >= 0; charPos--)
	{
		if(logAttr[charPos].fCharStop)
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
VOID TextView::MoveCharNext()
{
	USPCACHE		* uspCache;
	CSCRIPT_LOGATTR * logAttr;
	TextCoord		  coord;
	int				  charPos;

	if(!GetLineLayout(&m_cursorPos, &uspCache, &logAttr))
		return;

	charPos = (int)min(m_cursorPos.line_offset_chars, (ULONG)uspCache->length_CRLF);

	// find the next valid character-position
	for( ++charPos; charPos <= uspCache->length_CRLF; charPos++)
	{
		if(logAttr[charPos].fCharStop)
			break;
	}

	// skip to beginning of next line if we hit the CR/LF
	if(charPos > uspCache->length_CRLF)
	{
		if(CoordAtNextLineStart(&m_cursorPos, &coord))
			SetCursorCoord(&coord);

		return;
	}

	SetCursorLinePos(charPos);
}

VOID TextView::MoveCurrentLineStart()
{
	SetCursorLinePos(0);
}

VOID TextView::MoveCurrentLineEnd()
{
	USPCACHE *uspCache;

	if(GetLineLayout(&m_cursorPos, &uspCache, 0))
		SetCursorLinePos(uspCache->length_CRLF);
}

//
//	Move to start of file
//
VOID TextView::MoveFileStart()
{
	TextCoord coord;

	if(m_pTextDoc->coord_from_byte_anchor(0, &coord))
		SetCursorCoord(&coord);
}

//
//	Move to end of file
//
VOID TextView::MoveFileEnd()
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
LONG TextView::OnKeyDown(UINT nKeyCode, UINT nFlags)
{
	bool fCtrlDown	= IsKeyPressed(VK_CONTROL);
	bool fShiftDown	= IsKeyPressed(VK_SHIFT);
	BOOL fAdvancing = FALSE;
	TextCoord oldCursor = m_cursorPos;

	//
	//	Process the key-press. Cursor movement is different depending
	//	on if <ctrl> is held down or not, so act accordingly
	//
	switch(nKeyCode)
	{
	case VK_SHIFT: case VK_CONTROL:
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
	case VK_INSERT:

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

	case VK_DELETE:

		if(m_nEditMode != MODE_READONLY)
		{
			if(fShiftDown)
				OnCut();
			else
				ForwardDelete();

			NotifyParent(TVN_CHANGED);
		}
		return 0;

	case VK_BACK:

		if(m_nEditMode != MODE_READONLY)
		{
			BackDelete();
			fAdvancing = FALSE;

			NotifyParent(TVN_CHANGED);
		}
		return 0;

	case VK_LEFT:

		if(fCtrlDown)	MoveWordPrev();
		else			MoveCharPrev();

		fAdvancing = FALSE;
		break;

	case VK_RIGHT:
		
		if(fCtrlDown)	MoveWordNext();
		else			MoveCharNext();
			
		fAdvancing = TRUE;
		break;

	case VK_UP:
		if(fCtrlDown)	Scroll(0, -1);
		else			MoveLineUp(1);
		break;

	case VK_DOWN:
		if(fCtrlDown)	Scroll(0, 1);
		else			MoveLineDown(1);
		break;

	// page up/down scroll the view and the caret together, so the caret keeps its row
	case VK_PRIOR:
		if(!fCtrlDown)
		{
			Scroll(0, -m_nWindowLines);
			MoveLineUp(m_nWindowLines);
		}
		break;

	case VK_NEXT:
		if(!fCtrlDown)
		{
			Scroll(0, m_nWindowLines);
			MoveLineDown(m_nWindowLines);
		}
		break;

	case VK_HOME:
		if(fCtrlDown)	MoveFileStart();
		else			MoveCurrentLineStart();
		break;

	case VK_END:
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
		bool hadSelection = m_selAnchor.byte_anchor != oldCursor.byte_anchor;

		if(hadSelection)
			InvalidateRange(&m_selAnchor, &oldCursor);

		m_selAnchor = m_cursorPos;

		if(hadSelection)
			RefreshWindow();
	}

	// update caret-location (xpos, line#)
	UpdateCaretCoord(&m_cursorPos, fAdvancing, &m_nCaretPosX);
	
	// maintain the caret 'anchor' position *except* for up/down actions
	if(nKeyCode != VK_UP && nKeyCode != VK_DOWN)
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

	NotifyParent(TVN_CURSOR_CHANGE);

	return 0;
}
