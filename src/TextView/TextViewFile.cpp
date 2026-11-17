// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

//
//	MODULE:		TextViewFile.cpp
//
//	PURPOSE:	TextView file input routines
//
//	NOTES:		www.catch22.net
//

#include <chrono>
#include <memory>
#include "TextViewInternal.h"

//
//	
//
int32_t TextView::OpenFile(const seqfilechar *szFileName, int nEncoding, bool fReadOnly)
{
	bool opened;
    std::unique_ptr<TextDocument> document(new TextDocument);
    document->set_in_memory_limit(FileDoc()->in_memory_limit());

    if(nEncoding < 0)
        opened = document->init(szFileName, fReadOnly);
    else
        opened = document->init(szFileName, (TEXT_ENCODING)nEncoding);

    if(opened)
	{
        ClearFile();
        delete m_pTextDoc;
        m_pTextDoc = document.release();
        TextCoord start;
		uint32_t linebreak;

		// the first line's ending sets the line format
		m_pTextDoc->coord_from_pos(TextPos(0), &start);

		if((linebreak = m_pTextDoc->linebreak_from_coord(&start)) != 0)
			m_nCRLFMode = linebreak;

		m_nLineCount   = m_pTextDoc->linecount();

		m_nHScrollPos  = 0;

		// shared with a program writing it, the text is read-only; otherwise it isn't
		if(m_pTextDoc->read_only())
			m_nEditMode = MODE_READONLY;
		else if(m_nEditMode == MODE_READONLY)
			m_nEditMode = MODE_INSERT;

		SetCursorCoord(&start);

		m_selAnchor = m_cursorPos;
		m_scrollVPos = m_cursorPos;

		UpdateMarginWidth();
		UpdateMetrics();
		ResetLineCache();
		NotifyCursorChange();
		NotifyIndexChange();

		// still filtering: the lines that match in this one
		if(m_fFilter)
			m_fFilter = StartFilter();

		return true;
	}

    {
        KeepLastError keep;	// it says why the file didn't open

        document.reset();
    }

    return false;
}

//
//	Save the text to a file. Kept as it is, nothing in the view changes.
//	Converted to another encoding or line format, the file no longer matches
//	the text, so it's opened again, in the encoding it was written in.
//
int32_t TextView::SaveFile(const seqfilechar *szFileName, uint32_t nEncoding, bool fBom, uint32_t nLineBreak)
{
	bool fConvert = nEncoding != (uint32_t)FileDoc()->getformat() || nLineBreak != 0;

	if(!FileDoc()->save(szFileName, (TEXT_ENCODING)nEncoding, fBom != false, nLineBreak))
		return false;

    if (fConvert)
    {
        return OpenFile(szFileName, (int)nEncoding, false);
    }

    ResetLineCache();
    RefreshWindow();
    return true;
}

//
//	Open a file to follow as another program appends to it: read-only, with
//	the caret at the end, where UpdateTail adds what's appended. It's indexed
//	as it opens, so the lines in view, at the end, have their numbers.
//
int32_t TextView::TailFile(const seqfilechar *szFileName)
{
	if(!OpenFile(szFileName, -1, true))
		return false;

	IndexFile(true);

	MoveFileEnd();
	m_selAnchor = m_cursorPos;

	UpdateCaretCoord(&m_cursorPos, true, &m_nCaretPosX);
	m_nAnchorPosX = m_nCaretPosX;
	RepositionCaret();
	NotifyCursorChange();
	return true;
}

//
//	Add what has been appended to a followed file since it was opened, or last
//	updated - filtered, the lines of it that match. A caret at the end stays
//	there, keeping the end in view. false if the file has got shorter: it's
//	been rewritten, and needs opening again.
//
bool TextView::UpdateTail()
{
	size_w	   length = m_pTextDoc->byte_length();
	bool	   atEnd  = m_cursorPos.byte_offset == length && m_selAnchor.byte_offset == length;
	TextChange change;
	size_w	   added;

	if(!FileDoc()->grow(&added))
		return false;

    if (m_pSourceDoc != 0)
    {
        if (FilterSlice())
            m_pHost->StartTimer(FILTER_TIMER, 0);

        // A completed index may need extending for newly appended source lines.
        if (added)
            IndexFile(true);
    }

    if ((change.inserted = m_pTextDoc->byte_length() - length) == 0)
        return true;

    // it only grew at the end, so positions stay put, but the last line can be longer
	change.at = TextPos(length);
	AdjustCoordsForChange(&change);
	m_pTextDoc->coord_from_pos(TextPos(m_cursorPos.byte_offset), &m_cursorPos);
	m_pTextDoc->coord_from_pos(TextPos(m_selAnchor.byte_offset), &m_selAnchor);

	ResetLineCache();
	m_nLineCount = m_pTextDoc->linecount();

	if(atEnd)
	{
		MoveFileEnd();
		m_selAnchor = m_cursorPos;
	}

	UpdateMarginWidth();
	SetupScrollbars();
	UpdateCaretCoord(&m_cursorPos, true, &m_nCaretPosX);
	RepositionCaret();
	RefreshWindow();

	NotifyCursorChange();
	NotifyParent(TVNOTIFY_CHANGED);
	return true;
}

//
//
//
int32_t TextView::ClearFile()
{
	EndFilter(false);

	// whatever was being indexed has gone
	if(m_fIndexing)
	{
		m_pHost->StopTimer(INDEX_TIMER);
		m_fIndexing = false;
	}

	if(m_pTextDoc)
	{
		m_pTextDoc->clear();
	}

	ResetLineCache();

	m_nLineCount		= m_pTextDoc->linecount();
	m_nLongestLine		= 0;	// grows as lines are laid out
	m_nCRLFMode			= DEFAULT_LINE_FORMAT;

	m_nHScrollPos		= 0;

	m_cursorPos			= TextCoord();
	m_scrollVPos		= TextCoord();
	m_selAnchor			= TextCoord();
	m_nCaretPosX		= 0;

	UpdateMetrics();
	NotifyCursorChange();
	NotifyIndexChange();

	return true;
}

// Indexing in the background: a megabyte at a time, for this long each tick
static const size_w INDEX_CHUNK = 0x100000;
static const int    INDEX_SLICE_MS = 15;

//
//	Start or stop indexing the rest of the document. It's done a slice at a
//	time on a timer, so the window stays responsive meanwhile.
//
int32_t TextView::IndexFile(bool fIndex)
{
	TextViewIndexInfo info;

	FillIndexInfo(&info);

	if(fIndex && !m_fIndexing && info.nPercent < 100)
	{
		m_fIndexing = m_pHost->StartTimer(INDEX_TIMER, 0);
	}
	else if(!fIndex && m_fIndexing)
	{
		m_pHost->StopTimer(INDEX_TIMER);
		m_fIndexing = false;
	}

	NotifyIndexChange();
	return m_fIndexing == (fIndex != false);
}

//
//	Index for a few milliseconds, then show how far it's got
//
int32_t TextView::OnIndexTimer()
{
	bool          more;

	auto start = std::chrono::steady_clock::now();

	do
	{
		more = FileDoc()->index_next(INDEX_CHUNK);
	}
	while(more && std::chrono::steady_clock::now() - start < std::chrono::milliseconds(INDEX_SLICE_MS));

	// done, or no more can be read
	if(!more)
	{
		m_pHost->StopTimer(INDEX_TIMER);
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
void TextView::IndexChanged()
{
	TextViewIndexInfo info;

	FillIndexInfo(&info);

	if(info.nPercent != m_nIndexPercent || !m_fIndexing)
	{
		m_nLineCount = m_pTextDoc->linecount();

        // Filtered source numbers also become exact as indexing advances.
        // Refresh their margin while indexing, and the normal view once done.
        if (m_fIndexing && !m_pSourceDoc)
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
void TextView::FillIndexInfo(TextViewIndexInfo *info)
{
	size_w length  = FileDoc()->byte_length();
	size_w indexed = FileDoc()->indexed_length();

	// under 100 until every byte is
	info->nPercent   = indexed >= length ? 100 : (uint32_t)(indexed * 100 / length);
	info->fIndexing  = m_fIndexing;
	info->nLineCount = (uint64_t)FileDoc()->linecount();
}

//
//	Tell the parent how far indexing has got
//
void TextView::NotifyIndexChange()
{
	TextViewIndexInfo info;

	FillIndexInfo(&info);
	m_nIndexPercent = info.nPercent;
	NotifyParent(TVNOTIFY_INDEX_CHANGE, &info);
}

// How much more of the document to index at a time, while a lookup waits
static const size_w EDIT_INDEX_CHUNK = 0x1000000;

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
