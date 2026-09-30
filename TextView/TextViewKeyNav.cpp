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
//	Get the UspCache and logical attributes for specified line
//
bool TextView::GetLogAttr(ULONG nLineNo, USPCACHE **puspCache, CSCRIPT_LOGATTR **plogAttr, ULONG *pnOffset)
{
	if((*puspCache = GetUspCache(0, nLineNo, pnOffset)) == 0)
		return false;

	if(plogAttr && (*plogAttr = UspGetLogAttr((*puspCache)->uspData)) == 0)
		return false;

	return true;
}

//
//	Move caret up specified number of lines
//
VOID TextView::MoveLineUp(int numLines)
{
	USPDATA			* uspData;
	ULONG			  lineOffset;
	
	int				  charPos;
	BOOL			  trailing;

	m_nCurrentLine -= min(m_nCurrentLine, (unsigned)numLines);

	// get Uniscribe data for prev line
	uspData = GetUspData(0, m_nCurrentLine, &lineOffset);

	// move up to character nearest the caret-anchor positions
	UspXToOffset(uspData, m_nAnchorPosX, &charPos, &trailing, 0);

	m_nCursorOffset = lineOffset + charPos + trailing;
}

//
//	Move caret down specified number of lines
//
VOID TextView::MoveLineDown(int numLines)
{
	USPDATA			* uspData;
	ULONG			  lineOffset;
	
	int				  charPos;
	BOOL			  trailing;

	m_nCurrentLine += min(m_nLineCount-m_nCurrentLine-1, (unsigned)numLines);

	// get Uniscribe data for prev line
	uspData = GetUspData(0, m_nCurrentLine, &lineOffset);

	// move down to character nearest the caret-anchor position
	UspXToOffset(uspData, m_nAnchorPosX, &charPos, &trailing, 0);

	m_nCursorOffset = lineOffset + charPos + trailing;
}

//
//	Move to start of previous word (to the left)
//
VOID TextView::MoveWordPrev()
{
	USPCACHE		* uspCache;
	CSCRIPT_LOGATTR * logAttr;
	ULONG			  lineOffset;
	int				  charPos;

	// get Uniscribe data for current line
	if(!GetLogAttr(m_nCurrentLine, &uspCache, &logAttr, &lineOffset))
		return;

	// move 1 character to left
	charPos = m_nCursorOffset - lineOffset - 1; 

	// skip to end of *previous* line if necessary
	if(charPos < 0)
	{
		charPos = 0;
		
		if(m_nCurrentLine > 0)
		{
			MoveLineEnd(m_nCurrentLine-1);		
			return;
		}
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

	m_nCursorOffset = lineOffset + charPos;
}

//
//	Move to start of next word
//
VOID TextView::MoveWordNext()
{
	USPCACHE		* uspCache;
	CSCRIPT_LOGATTR * logAttr;
	ULONG			  lineOffset;
	int				  charPos;

	// get Uniscribe data for current line
	if(!GetLogAttr(m_nCurrentLine, &uspCache, &logAttr, &lineOffset))
		return;

	charPos = m_nCursorOffset - lineOffset;

	// if already at end-of-line, skip to next line
	if(charPos == uspCache->length_CRLF)
	{
		if(m_nCurrentLine + 1 < m_nLineCount)
			MoveLineStart(m_nCurrentLine+1);

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

	m_nCursorOffset = lineOffset + charPos;
}

//
//	Move to start of current word
//
VOID TextView::MoveWordStart()
{
	USPCACHE		* uspCache;
	CSCRIPT_LOGATTR * logAttr;
	ULONG			  lineOffset;
	int				  charPos;

	// get Uniscribe data for current line
	if(!GetLogAttr(m_nCurrentLine, &uspCache, &logAttr, &lineOffset))
		return;

	charPos  = m_nCursorOffset - lineOffset;

	while(charPos > 0 && !logAttr[charPos-1].fWhiteSpace)
		charPos--;

	m_nCursorOffset = lineOffset + charPos;
}

//
//	Move to end of current word
//
VOID TextView::MoveWordEnd()
{
	USPCACHE		* uspCache;
	CSCRIPT_LOGATTR * logAttr;
	ULONG			  lineOffset;
	int				  charPos;

	// get Uniscribe data for current line
	if(!GetLogAttr(m_nCurrentLine, &uspCache, &logAttr, &lineOffset))
		return;

	charPos  = m_nCursorOffset - lineOffset;

	while(charPos < uspCache->length_CRLF && !logAttr[charPos].fWhiteSpace)
		charPos++;

	m_nCursorOffset = lineOffset + charPos;
}

//
//	Move to previous character
//
VOID TextView::MoveCharPrev()
{
	USPCACHE		* uspCache;
	CSCRIPT_LOGATTR * logAttr;
	ULONG			  lineOffset;
	int				  charPos;

	// get Uniscribe data for current line
	if(!GetLogAttr(m_nCurrentLine, &uspCache, &logAttr, &lineOffset))
		return;

	charPos = m_nCursorOffset - lineOffset;

	// find the previous valid character-position
	for( --charPos; charPos >= 0; charPos--)
	{
		if(logAttr[charPos].fCharStop)
			break;
	}

	// move up to end-of-last line if necessary
	if(charPos < 0)
	{
		charPos  = 0;

		if(m_nCurrentLine > 0)
		{
			MoveLineEnd(m_nCurrentLine-1);
			return;
		}
	}

	// update cursor position
	m_nCursorOffset = lineOffset + charPos;
}

//
//	Move to next character
//
VOID TextView::MoveCharNext()
{
	USPCACHE		* uspCache;
	CSCRIPT_LOGATTR * logAttr;
	ULONG			  lineOffset;
	int				  charPos;

	// get Uniscribe data for specified line
	if(!GetLogAttr(m_nCurrentLine, &uspCache, &logAttr, &lineOffset))
		return;

	charPos = m_nCursorOffset - lineOffset;

	// find the next valid character-position
	for( ++charPos; charPos <= uspCache->length_CRLF; charPos++)
	{
		if(logAttr[charPos].fCharStop)
			break;
	}

	// skip to beginning of next line if we hit the CR/LF
	if(charPos > uspCache->length_CRLF)
	{
		if(m_nCurrentLine + 1 < m_nLineCount)
			MoveLineStart(m_nCurrentLine+1);
	}
	// otherwise advance the character-position
	else
	{
		m_nCursorOffset = lineOffset + charPos;
	}
}

//
//	Move to start of specified line
//
VOID TextView::MoveLineStart(ULONG lineNo)
{
	ULONG			  lineOffset;
	USPCACHE		* uspCache;
	CSCRIPT_LOGATTR * logAttr;
	int				  charPos;
	
	// get Uniscribe data for current line
	if(!GetLogAttr(lineNo, &uspCache, &logAttr, &lineOffset))
		return;

	charPos  = m_nCursorOffset - lineOffset;
	
	// if already at start of line, skip *forwards* past any whitespace
	if(m_nCursorOffset == lineOffset)
	{
		// skip whitespace
		while(logAttr[m_nCursorOffset - lineOffset].fWhiteSpace)
			m_nCursorOffset++;
	}
	// if not at start, goto start
	else
	{
		m_nCursorOffset = lineOffset;
	}
}

//
//	Move to end of specified line
//
VOID TextView::MoveLineEnd(ULONG lineNo)
{
	USPCACHE *uspCache;
	
	if((uspCache = GetUspCache(0, lineNo)) == 0)
		return;

	m_nCursorOffset = uspCache->offset + uspCache->length_CRLF;
}

//
//	Move to start of file
//
VOID TextView::MoveFileStart()
{
	m_nCursorOffset = 0;
}

//
//	Move to end of file
//
VOID TextView::MoveFileEnd()
{
	m_nCursorOffset = m_pTextDoc->size();
}


//
//	Process keyboard-navigation keys
//
LONG TextView::OnKeyDown(UINT nKeyCode, UINT nFlags)
{
	bool fCtrlDown	= IsKeyPressed(VK_CONTROL);
	bool fShiftDown	= IsKeyPressed(VK_SHIFT);
	BOOL fAdvancing = FALSE;

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

	case VK_PRIOR:
		if(!fCtrlDown)	MoveLineUp(m_nWindowLines);
		break;

	case VK_NEXT:
		if(!fCtrlDown)	MoveLineDown(m_nWindowLines);
		break;

	case VK_HOME:
		if(fCtrlDown)	MoveFileStart();
		else			MoveLineStart(m_nCurrentLine);
		break;

	case VK_END:
		if(fCtrlDown)	MoveFileEnd();
		else			MoveLineEnd(m_nCurrentLine);
		break;

	default:
		return 0;
	}

	// Extend selection if <shift> is down
	if(fShiftDown)
	{		
		InvalidateRange(m_nSelectionEnd, m_nCursorOffset);
		m_nSelectionEnd	= m_nCursorOffset;
	}
	// Otherwise clear the selection
	else
	{
		if(m_nSelectionStart != m_nSelectionEnd)
			InvalidateRange(m_nSelectionStart, m_nSelectionEnd);

		m_nSelectionEnd		= m_nCursorOffset;
		m_nSelectionStart	= m_nCursorOffset;
	}

	// update caret-location (xpos, line#) from the offset
	UpdateCaretOffset(m_nCursorOffset, fAdvancing, &m_nCaretPosX, &m_nCurrentLine);
	
	// maintain the caret 'anchor' position *except* for up/down actions
	if(nKeyCode != VK_UP && nKeyCode != VK_DOWN)
	{
		m_nAnchorPosX = m_nCaretPosX;

		// scroll as necessary to keep caret within viewport
		ScrollToPosition(m_nCaretPosX, m_nCurrentLine);
	}
	else
	{
		// scroll as necessary to keep caret within viewport
		if(!fCtrlDown)
			ScrollToPosition(m_nCaretPosX, m_nCurrentLine);
	}

	NotifyParent(TVN_CURSOR_CHANGE);

	return 0;
}
