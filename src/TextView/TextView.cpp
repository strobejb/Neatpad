// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

//
//	MODULE:		TextView.cpp
//
//	PURPOSE:	Implementation of the TextView control
//
//	NOTES:		www.catch22.net
//

#include <stdio.h>

#include "TextViewInternal.h"

//
//	Constructor for TextView class. Its host owns it, and it calls the host
//	from here on.
//
TextView::TextView(TextViewHost *host)
{
	m_pHost = host;

	// Font-related data
	m_nHeightAbove	= 0;
	m_nHeightBelow	= 0;
	
	// File-related data
	m_nLineCount   = 0;
	m_nLongestLine = 0;	
	

	// Scrollbar related data
	m_nHScrollPos = 0;
	m_nHScrollMax = 0;

	// Display-related data
	m_nTabWidthChars = 4;
	m_uStyleFlags	 = TXS_CHANGEMARKERS | TXS_HIGHLIGHTCHANGES;
	m_nCaretWidth	 = 0;
	m_nLongLineLimit = 80;
	m_nLineInfoCount = 0;
	m_nCRLFMode		 = DEFAULT_LINE_FORMAT;

	// allocate the line-layout cache
	m_lineCache		= new LINECACHE[LINE_CACHE_SIZE];

	for(int i = 0; i < LINE_CACHE_SIZE; i++)
	{
		m_lineCache[i].usage   = 0;
		m_lineCache[i].line_begin = TextPos();
		m_lineCache[i].layout  = m_pHost->CreateLayout();
	}

	m_nCaretWidth = m_pHost->CaretWidth();

	if(m_nCaretWidth == 0)
		m_nCaretWidth = 2;

	// Default display colours
	m_rgbColourList[TXC_FOREGROUND]		= SYSCOL(TVSC_WINDOWTEXT);
	m_rgbColourList[TXC_BACKGROUND]		= SYSCOL(TVSC_WINDOW);			// RGB(34,54,106)
	m_rgbColourList[TXC_HIGHLIGHTTEXT]	= SYSCOL(TVSC_HIGHLIGHTTEXT);
	m_rgbColourList[TXC_HIGHLIGHT]		= SYSCOL(TVSC_HIGHLIGHT);
	m_rgbColourList[TXC_HIGHLIGHTTEXT2]	= SYSCOL(TVSC_WINDOWTEXT);//INACTIVECAPTIONTEXT);
	m_rgbColourList[TXC_HIGHLIGHT2]		= SYSCOL(TVSC_3DFACE);//INACTIVECAPTION);
	m_rgbColourList[TXC_SELMARGIN1]		= SYSCOL(TVSC_3DFACE);
	m_rgbColourList[TXC_SELMARGIN2]		= SYSCOL(TVSC_3DHIGHLIGHT);
	m_rgbColourList[TXC_LINENUMBERTEXT]	= SYSCOL(TVSC_3DSHADOW);
	m_rgbColourList[TXC_LINENUMBER]		= SYSCOL(TVSC_3DFACE);
	m_rgbColourList[TXC_LONGLINETEXT]	= SYSCOL(TVSC_3DSHADOW);
	m_rgbColourList[TXC_LONGLINE]		= SYSCOL(TVSC_3DFACE);
	m_rgbColourList[TXC_CURRENTLINETEXT] = SYSCOL(TVSC_WINDOWTEXT);
	m_rgbColourList[TXC_CURRENTLINE]	 = TV_RGB(230,240,255);
	m_rgbColourList[TXC_SEARCHMATCHTEXT] = SYSCOL(TVSC_WINDOWTEXT);
	m_rgbColourList[TXC_SEARCHMATCH]	 = TV_RGB(255,230,128);
    m_rgbColourList[TXC_CHANGEDTEXT]   = TV_RGB(190, 55, 55);
    m_rgbColourList[TXC_CHANGEDLINE]   = TV_RGB(88, 132, 164);
    m_rgbColourList[TXC_CHANGEDLINEBG] = SYSCOL(TVSC_3DFACE);

	// Runtime data
	m_nSelectionMode	= SEL_NONE;
	m_nEditMode			= MODE_INSERT;
	m_nScrollTimer		= 0;
	m_fIndexing			= false;
	m_nIndexPercent		= 100;
	m_fHideCaret		= false;
	
	m_nSelectionType	= SEL_NONE;

	m_nLinenoWidth		= 0;
	m_nCaretPosX		= 0;
	m_nAnchorPosX		= 0;

	//SetRect(&m_rcBorder, 2, 2, 2, 2);

	m_pTextDoc = new TextDocument();

	//
	//	The TextView state must be fully initialized before we
	//	start calling member-functions
	//

	// Measure the host's fonts
	FontsChanged();

	UpdateMetrics();
	UpdateMarginWidth();
}

//
//	Destructor for TextView class
//
TextView::~TextView()
{
	EndFilter(false);

	if(m_pTextDoc)
		delete m_pTextDoc;

	for(int i = 0; i < LINE_CACHE_SIZE; i++)
		delete m_lineCache[i].layout;

	delete[] m_lineCache;
}

intptr_t TextView::NotifyParent(uint32_t nNotifyCode, void *info)
{
	return m_pHost->Notify(nNotifyCode, info);
}

//
//	Describe the caret: its line (an estimate past the counted part of a large
//	file), its column, and its offset in the document where that is cheap
//
void TextView::FillCursorInfo(TextViewCursorInfo *info)
{
	size_w lineno = 0;
	size_w offset_chars = 0;

	info->fLineNoKnown	= m_pTextDoc->lineno_from_pos(m_cursorPos, &lineno) == TDR_OK;
	info->nLineNo		= lineno;
	info->nColumnNo		= m_cursorPos.column;
	info->fOffsetKnown	= m_pTextDoc->charoffset_from_pos(m_cursorPos, &offset_chars) == TDR_OK;
	info->nOffset		= offset_chars;
}

//
//	Tell the parent the caret has moved, and where to
//
void TextView::NotifyCursorChange()
{
	TextViewCursorInfo ci;

	FillCursorInfo(&ci);
	NotifyParent(TVNOTIFY_CURSOR_CHANGE, &ci);
}

void TextView::UpdateMetrics()
{
	TextRect rect;
	ClientRect(&rect);

	OnSize(0, rect.right, rect.bottom);
	RefreshWindow();

	RepositionCaret();
}

int32_t TextView::OnSetFocus()
{
	m_pHost->CreateCaret(m_nCaretWidth, m_nLineHeight);
	RepositionCaret();

	m_pHost->ShowCaret();
	RefreshWindow();
	return 0;
}

int32_t TextView::OnKillFocus()
{
	// if we are making a selection when we lost focus then
	// stop the selection logic
	if(m_nSelectionMode != SEL_NONE)
	{
		OnLButtonUp(0, 0, 0);
	}

	m_pHost->HideCaret();
	m_pHost->DestroyCaret();
	RefreshWindow();
	return 0;
}

uint32_t TextView::SetStyle(uint32_t uMask, uint32_t uStyles)
{
	uint32_t oldstyle = m_uStyleFlags;

	m_uStyleFlags  = (m_uStyleFlags & ~uMask) | uStyles;

	ResetLineCache();

	// update display here
	UpdateMetrics();
	RefreshWindow();

	return oldstyle;
}

uint32_t TextView::SetVar(uint32_t nVar, uint32_t nValue)
{
	return 0;//oldval;
}

uint32_t TextView::GetVar(uint32_t nVar)
{
	return 0;
}

uint32_t TextView::GetStyleMask(uint32_t uMask)
{
	return m_uStyleFlags & uMask;
}
	
bool TextView::CheckStyle(uint32_t uMask)
{
	return (m_uStyleFlags & uMask) ? true : false;
}

int TextView::SetCaretWidth(int nWidth)
{
	int oldwidth = m_nCaretWidth;
	m_nCaretWidth  = nWidth;

	return oldwidth;
}

int32_t TextView::SetLongLine(int nLength)
{
	int oldlen = m_nLongLineLimit;
	m_nLongLineLimit = nLength;
	return oldlen;
}

static int CompareLineInfo(const void *e1, const void *e2)
{
	const LINEINFO *elem1 = (const LINEINFO *)e1;
	const LINEINFO *elem2 = (const LINEINFO *)e2;

	if(elem1->nLineNo < elem2->nLineNo)
		return -1;
	if(elem1->nLineNo > elem2->nLineNo)
		return 1;
	else
		return 0;
}

int TextView::SetLineImage(uint32_t nLineNo, uint32_t nImageIdx)
{
	LINEINFO *linfo = GetLineInfo(nLineNo);

	// if already a line with an image
	if(linfo)
	{
		linfo->nImageIdx = nImageIdx;
	}
	else
	{
		linfo = &m_LineInfo[m_nLineInfoCount++];
		linfo->nLineNo = nLineNo;
		linfo->nImageIdx = nImageIdx;

		// sort the array
		qsort(
			m_LineInfo, 
			m_nLineInfoCount, 
			sizeof(LINEINFO), 
			CompareLineInfo
			);

	}
	return 0;
}

LINEINFO* TextView::GetLineInfo(uint32_t nLineNo)
{
	LINEINFO key = { nLineNo, 0 };

	// perform the binary search
	return (LINEINFO *)	bsearch(
							&key, 
							m_LineInfo,
							m_nLineInfoCount, 
							sizeof(LINEINFO), 
							CompareLineInfo
							);
}

//
//	Return the selection's ends in document order; true if anything is selected
//
bool TextView::GetSelection(TextCoord *start, TextCoord *end)
{
	bool forward = m_selAnchor <= m_cursorPos;

	*start = forward ? m_selAnchor : m_cursorPos;
	*end   = forward ? m_cursorPos : m_selAnchor;

	return start->byte_offset != end->byte_offset;
}

//
//	The UTF-16 column of pos within a line, clamped to the line
//
static uint32_t LineColumn(TextCoord *line, TextCoord *pos, uint32_t lineLen)
{
	if(*pos <= line->line_begin)
		return 0;

	if(pos->line_begin == line->line_begin)
		return (uint32_t)min_of(pos->column, (size_w)lineLen);

	return lineLen;
}

//
//	Return the selected UTF-16 columns [start, end) of a line lineLen columns long
//
void TextView::SelectionColumns(TextCoord *line, uint32_t lineLen, uint32_t *start, uint32_t *end)
{
	TextCoord selStart, selEnd;

	GetSelection(&selStart, &selEnd);

	*start = LineColumn(line, &selStart, lineLen);
	*end   = LineColumn(line, &selEnd, lineLen);
}

//
//	Return the size of the selection in document bytes
//
size_w TextView::SelectionSize()
{
	TextCoord start, end;

	GetSelection(&start, &end);
	return end - start;
}

uint32_t TextView::SelectAll()
{
	m_pTextDoc->coord_from_pos(TextPos(0), &m_selAnchor);
	m_pTextDoc->coord_from_document_end(&m_cursorPos);

	UpdateViewState(true);
	RefreshWindow();
	return 0;
}

uint32_t TextView::FindText(const TextViewFind *find)
{
	TextCoord selStart;
	TextCoord selEnd;
	TextPos docStart;
	TextPos docEnd;
	TextPos start;
	TextPos matchBegin;
	TextPos matchEnd;
	uint32_t result = TVFR_FOUND;

	if(find == 0 || find->text == 0 || find->length == 0)
		return TVFR_NOTFOUND;

	docEnd = TextPos(m_pTextDoc->byte_length());

	if(GetSelection(&selStart, &selEnd))
		start = find->backwards ? selStart : selEnd;
	else
		start = m_cursorPos;

	if(!m_pTextDoc->find_text(find->text, find->length, find->backwards ? docStart : start, find->backwards ? start : docEnd, find->backwards != false, find->matchcase != false, &matchBegin, &matchEnd))
	{
		if(!find->wrap || docEnd == docStart)
			return TVFR_NOTFOUND;

		if(!m_pTextDoc->find_text(find->text, find->length, find->backwards ? start : docStart, find->backwards ? docEnd : start, find->backwards != false, find->matchcase != false, &matchBegin, &matchEnd))
			return TVFR_NOTFOUND;

		result = TVFR_FOUNDWRAPPED;
	}

	if(!m_pTextDoc->coord_from_pos(matchBegin, &m_selAnchor))
		return TVFR_NOTFOUND;
	if(!m_pTextDoc->coord_from_pos(matchEnd, &m_cursorPos))
		return TVFR_NOTFOUND;

	m_nSelectionType = SEL_NORMAL;
	UpdateViewState(true);
	RefreshWindow();
	return result;
}

//
//	Highlight every match of find wherever the text is shown, or none, for 0 -
//	showing only the lines that hold one, with fFilter. Lines are laid out
//	again, to colour them.
//
int32_t TextView::ShowMatches(const TextViewFind *find, bool fFilter)
{
	// other matches, or none, start from the whole document again
	EndFilter(true);

	if(find && find->text && find->length)
	{
		m_sMatchText.assign(find->text, find->length);
		m_fMatchCase = find->matchcase;
	}
	else
	{
		m_sMatchText.clear();
	}

	m_fFilter = fFilter && !m_sMatchText.empty() && StartFilter();

	ResetLineCache();
	RefreshWindow();
	return true;
}

//
//	Put find->replace in place of the match the selection holds, as an undo step
//	of its own, with the caret after it - or, with fAll, in place of every match
//	in the document, as one undo step, keeping the caret and the view on their
//	lines. Returns how many matches it replaced.
//
size_w TextView::ReplaceText(const TextViewFind *find, bool fAll)
{
	TextCoord  start, end, line;
	TextPos    matchBegin, matchEnd;
	TextChange change;
	size_w     caretLine, topLine, count = 1;
	size_w     column = m_cursorPos.column;

	if(m_nEditMode == MODE_READONLY || find == 0 || find->text == 0 || find->length == 0)
		return 0;

	if(fAll)
	{
		if(!LineFromPos(m_cursorPos, &caretLine) || !LineFromPos(m_scrollVPos, &topLine))
			return 0;

		if((count = m_pTextDoc->replace_all(find->text, find->length, find->matchcase != false, find->replace, find->replacelength)) == 0)
			return 0;

		ResetLineCache();

		// the lines may be fewer now
		if(!LineStart(topLine, &line))
			m_pTextDoc->coord_from_pos(TextPos(0), &line);

		SetScrollCoord(&line);

		if(!LineStart(caretLine, &line) || !m_pTextDoc->coord_from_line_pos(&line, column, &m_cursorPos))
			m_pTextDoc->coord_from_document_end(&m_cursorPos);
	}
	else
	{
		// the selection holds a match if a match spans it exactly
		if(!GetSelection(&start, &end) ||
		   !m_pTextDoc->find_text(find->text, find->length, start, end, false, find->matchcase != false, &matchBegin, &matchEnd) ||
		   matchBegin != start || matchEnd != end)
			return 0;

		// replacing with nothing is erasing
		m_pTextDoc->undo_group_begin();

		if(find->replacelength != 0)
			m_pTextDoc->replace_text(start, end, find->replace, find->replacelength, &change);
		else
			m_pTextDoc->erase_text(start, end, &change);

		m_pTextDoc->undo_group_end();
		m_pTextDoc->undo_group_break();

		AdjustCoordsForChange(&change);
		m_pTextDoc->coord_from_pos(TextPos(change.at.byte_offset + change.inserted), &m_cursorPos);
		ResetLineCache();
	}

	m_selAnchor = m_cursorPos;

	RefreshWindow();
	UpdateViewState(true);
	NotifyParent(TVNOTIFY_CHANGED);
	return count;
}

//
//	Move the caret to the start of a line, if the line can be found exactly:
//	one past the indexed part of the document is pending until indexing gets
//	there. Without fMove, only say whether it can.
//
TEXT_RESULT TextView::GotoLine(size_w lineno, bool fMove)
{
	TextCoord   line;
	size_w      before = m_pTextDoc->indexed_length();
	TEXT_RESULT result = m_pTextDoc->coord_from_lineno(lineno, &line);

	// a line a little past the indexed part is indexed there and then
	if(m_pTextDoc->indexed_length() != before)
		IndexChanged();

	if(result == TDR_OK && fMove)
	{
		m_cursorPos = line;
		m_selAnchor = line;
		m_nSelectionType = SEL_NORMAL;

		UpdateViewState(true);
		RefreshWindow();
	}

	return result;
}

//
//	The caret's offset in characters from the start of the document, which
//	isn't known past the indexed part of a UTF-8 or UTF-32 document
//
TEXT_RESULT TextView::CursorOffset(size_w *chars)
{
	return m_pTextDoc->charoffset_from_pos(m_cursorPos, chars);
}

//
//	The caret's line, which may be an estimate past the indexed part
//
TEXT_RESULT TextView::CursorLine(size_w *lineno)
{
	return m_pTextDoc->lineno_from_pos(m_cursorPos, lineno);
}

size_w TextView::CursorColumn()
{
	return m_cursorPos.column;
}

//
//	The caret's column in characters, as a status bar shows it: each unit that
//	starts a character counts, so a surrogate pair or a UTF-8 sequence is one
//
size_w TextView::CursorCharColumn()
{
	TextReader reader = m_pTextDoc->text_from_range(m_cursorPos.line_begin, m_cursorPos);
	tchar_t    buf[0x400];
	uint32_t   len;
	size_w     chars = 0;

	while((len = reader.read(buf, 0x400)) != 0)
	{
		for(uint32_t i = 0; i < len; i++)
		{
			uint32_t unit = (uint32_t)buf[i] & (TCHAR_UTF8 ? 0xff : 0xffff);

			if(TCHAR_UTF8 ? (unit & 0xc0) != 0x80 : unit < 0xdc00 || unit > 0xdfff)
				chars++;
		}
	}

	return chars;
}

int TextView::LineFormat()
{
	return m_nCRLFMode;
}

//
//	Filtered, the view is read-only, but the document may not be
//
uint32_t TextView::GetEditMode()
{
	return m_pSourceDoc ? m_nFilterEditMode : m_nEditMode;
}

uint32_t TextView::SetEditMode(uint32_t mode)
{
	uint32_t oldMode = m_nEditMode;

	m_nEditMode = mode;
	return oldMode;
}

//
//	The document's size and the selection's, in bytes and in characters where
//	those are counted
//
bool TextView::GetDocStats(TextViewDocStats *stats)
{
	TextCoord selStart, selEnd;
	size_w chars, startChars, endChars;

	if(stats == 0)
		return false;

	stats->byte_count = (uint64_t)FileDoc()->file_length();
	stats->char_count = 0;
	stats->char_count_known = false;
	stats->sel_byte_count = 0;
	stats->sel_char_count = 0;
	stats->linebreaks_seen = FileDoc()->linebreaks_seen();

	if(FileDoc()->charcount(&chars) == TDR_OK)
	{
		stats->char_count = (uint64_t)chars;
		stats->char_count_known = true;
	}

	// the selection, counted the same way
	if(GetSelection(&selStart, &selEnd))
	{
		stats->sel_byte_count = (uint64_t)(selEnd - selStart);

		if(stats->char_count_known &&
			m_pTextDoc->charoffset_from_pos(selStart, &startChars) == TDR_OK &&
			m_pTextDoc->charoffset_from_pos(selEnd, &endChars) == TDR_OK)
		{
			stats->sel_char_count = (uint64_t)(endChars - startChars);
		}
	}

	return true;
}
