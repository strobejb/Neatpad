#pragma once

#ifndef NEATPAD_TEXTVIEW_INTERNAL_INCLUDED
#define NEATPAD_TEXTVIEW_INTERNAL_INCLUDED

// Characters of a line that are laid out and drawn. A longer line stops being
// drawn at this point (like VS Code's stopRenderingLineAfter).
#define LINE_LAYOUT_LIMIT	10000

// Timers: the mouse-selection scroll, and indexing in the background
#define SCROLL_TIMER	1
#define INDEX_TIMER		2
#define FILTER_TIMER	3
#define LINENO_PAD	 8

#include <functional>
#include <stdint.h>
#include <string>
#include <vector>

#ifdef _WIN32
// EditMessage's types: the edit-control messages are Windows' alone
// (win32/TextViewEdit.cpp). Nothing else in the engine uses the platform.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

/*HTHEME  (WINAPI * OpenThemeData_Proc)(HWND hwnd, LPCWSTR pszClassList);
bool    (WINAPI * CloseThemeData_Proc)(HTHEME hTheme);
HRESULT (WINAPI * DrawThemeBackground_Proc)(HTHEME hTheme, HDC hdc, int, int, const TextRect*, const TextRect*);
*/


#include "../TextDocument/TextDocument.h"
#include "../TextDocument/SaveFile.h"
#include "TextViewDefs.h"
#include "TextViewHost.h"
#include "TextLayout.h"
#include "TextCanvas.h"

//
//	LINECACHE - a line laid out, kept while it's in use
//
//
//	A rectangle in the view, in pixels: right and bottom are just outside it
//
struct TextRect
{
	int left;
	int top;
	int right;
	int bottom;
};

typedef struct
{
	TextLayout *layout;
	TextPos	 line_begin;	// the line's start (cache key; flushed on every edit)
	uint32_t	 usage;			// cache-count

	int		 length;		// length in chars INCLUDING CR/LF
	int		 length_CRLF;	// length in chars EXCLUDING CR/LF

} LINECACHE;

#define LINE_CACHE_SIZE 200

//
//	LINEINFO - information about a specific line
//
typedef struct
{
	uint32_t	nLineNo;
	int		nImageIdx;

	// more here in the future?

} LINEINFO;

//
//	The keys and scrollbar actions the engine handles: Win32's VK_* and SB_*
//	values, so the Win32 host passes its own straight in. Other hosts map theirs.
//
enum
{
	TVK_BACK	= 0x08,
	TVK_SHIFT	= 0x10,
	TVK_CONTROL	= 0x11,
	TVK_PRIOR	= 0x21,
	TVK_NEXT	= 0x22,
	TVK_END		= 0x23,
	TVK_HOME	= 0x24,
	TVK_LEFT	= 0x25,
	TVK_UP		= 0x26,
	TVK_RIGHT	= 0x27,
	TVK_DOWN	= 0x28,
	TVK_INSERT	= 0x2D,
	TVK_DELETE	= 0x2E
};

enum
{
	TVSB_LINEUP			= 0,
	TVSB_LINEDOWN		= 1,
	TVSB_PAGEUP			= 2,
	TVSB_PAGEDOWN		= 3,
	TVSB_THUMBPOSITION	= 4,
	TVSB_THUMBTRACK		= 5,
	TVSB_TOP			= 6,
	TVSB_BOTTOM			= 7,

	TVSB_LINELEFT		= TVSB_LINEUP,
	TVSB_LINERIGHT		= TVSB_LINEDOWN,
	TVSB_PAGELEFT		= TVSB_PAGEUP,
	TVSB_PAGERIGHT		= TVSB_PAGEDOWN,
	TVSB_LEFT			= TVSB_TOP,
	TVSB_RIGHT			= TVSB_BOTTOM
};

// The smaller and larger of two values of one type (windows.h has min and
// max macros, which std::min and std::max trip over)
template <class T> inline T min_of(T a, T b)
{
    return a < b ? a : b;
}

template <class T> inline T max_of(T a, T b)
{
    return a < b ? b : a;
}

// A line number as the margin shows it, " 123 ": returns its length
int FormatLineNumber(tchar_t *buf, uint64_t lineno);

// maximum number of lines that we can hold info for at one time
#define MAX_LINE_INFO 128	

enum SELMODE { SEL_NONE, SEL_NORMAL, SEL_MARGIN, SEL_BLOCK };

//
//	The view's API in the engine's own terms, without the platform's types.
//	The Win32 host turns its messages' structures into these, and back.
//

// What to find, and what to replace it with
struct TextViewFind
{
	const tchar_t *text;
	uint32_t       length;
	bool           backwards;
	bool           wrap;
	bool           matchcase;
	const tchar_t *replace;
	uint32_t       replacelength;
};

// The document's size and the selection's, in bytes, and in UTF-16 units where
// those are counted
struct TextViewDocStats
{
	uint64_t byte_count;
	uint64_t char_count;
	bool     char_count_known;		// also covers sel_char_count
	uint64_t sel_byte_count;
	uint64_t sel_char_count;
	uint32_t linebreaks_seen;		// TXL_* kinds of line break found so far
};

// Recovery state: where it's kept, and the file it's for
struct TextViewRecovery
{
	const seqfilechar             *directory;
	std::basic_string<seqfilechar> filename;			// input on save, output on successful restore
	bool                           originalUnavailable;	// failed restore: original changed or cannot be opened
	bool                           preserveSources;		// save input: retain bases before replacing originals
};

// The longest file name recovery keeps, in units
const size_t TV_RECOVERY_NAME_MAX = 259;

typedef struct
{
	TextPos	line_begin;		// the line's start
	uint32_t	xpos;

} CURPOS;

//
//	TextView - the editor engine. Its platform's host owns it.
//
// A new document's line endings, and a file's without any: the platform's own
#ifdef _WIN32
#define DEFAULT_LINE_FORMAT TXL_CRLF
#else
#define DEFAULT_LINE_FORMAT TXL_LF
#endif

class TextView
{
    friend class TextViewMarginProbe; // Hidden-window gutter tests.

public:

	TextView(TextViewHost *host);
	~TextView();

	//
	//	Input, painting and timers, which the platform passes on
	//
	void Paint(TextCanvas &canvas, TextRect *rcPaint);
	void FontsChanged();
	int32_t OnSize(uint32_t nFlags, int width, int height);
	int32_t OnVScroll(uint32_t nSBCode, uint32_t nPos);
	int32_t OnHScroll(uint32_t nSBCode, uint32_t nPos);
	int32_t OnMouseWheel(int nDelta);
	int32_t OnTimer(uintptr_t nTimer);

	int32_t OnLButtonDown(uint32_t nFlags, int x, int y);
	int32_t OnLButtonUp(uint32_t nFlags, int x, int y);
	int32_t OnLButtonDblClick(uint32_t nFlags, int x, int y);
	int32_t OnMouseMove(uint32_t nFlags, int x, int y);

	int32_t OnKeyDown(uint32_t nKeyCode, uint32_t nFlags);
	int32_t OnChar(uint32_t nChar, uint32_t nFlags);
	int32_t OnText(const tchar_t *text, uint32_t length);

	int32_t OnSetFocus();
	int32_t OnKillFocus();

	bool OnCut();
	bool OnCopy();
	bool OnPaste();
	bool OnClear();

	//
	//	The view's API, which the platform's messages or functions call
	//
	bool		Undo();
	bool		Redo();
	bool		CanUndo();
	bool		CanRedo();
	TextDocument *FileDoc() const;
#ifdef _WIN32
	LRESULT		EditMessage(uint32_t msg, WPARAM wParam, LPARAM lParam);	// win32/TextViewEdit.cpp
#endif
	int32_t		OpenFile(const seqfilechar *szFileName, int nEncoding, bool fReadOnly);	// -1 for its byte-order mark to say
    bool        SaveState(TextViewRecovery *state);
    bool        LoadState(TextViewRecovery *state);

    // The file a saved state is for, without loading it (empty for none)
    static bool StateFilename(const seqfilechar *directory, std::basic_string<seqfilechar> &filename);
	int32_t		TailFile(const seqfilechar *szFileName);
	bool		UpdateTail();
	int32_t		SaveFile(const seqfilechar *szFileName, uint32_t nEncoding, bool fBom, uint32_t nLineBreak);
	int32_t		ClearFile();
	int32_t		SetLineSpacing(int nAbove, int nBelow);
	uint32_t	SetColour(uint32_t idx, uint32_t rgbColour);
	uint32_t		SetStyle(uint32_t uMask, uint32_t uStyles);
	int			SetCaretWidth(int nWidth);
	int32_t		SetLongLine(int nLength);
	int			SetLineImage(uint32_t nLineNo, uint32_t nImageIdx);
    uint32_t        SetTabWidth(uint32_t width);
    uint32_t          SetFilterContext(uint32_t lines);
    bool		ConvertLineBreaks(uint32_t nLineBreak);
	size_w		SelectionSize();
	uint32_t		SelectAll();
    int32_t		IndexFile(bool fIndex);
	TEXT_RESULT	GotoLine(size_w lineno, bool fMove);
	void		FillIndexInfo(TextViewIndexInfo *info);
	void		FillCursorInfo(TextViewCursorInfo *info);
	uint32_t		FindText(const TextViewFind *find);
	int32_t		ShowMatches(const TextViewFind *find, bool fFilter);
	size_w		ReplaceText(const TextViewFind *find, bool fAll);
	TEXT_RESULT	CursorOffset(size_w *chars);
	TEXT_RESULT	CursorLine(size_w *lineno);
	size_w		CursorColumn();
	size_w		CursorCharColumn();
	int			LineFormat();
	uint32_t		GetEditMode();
	uint32_t		SetEditMode(uint32_t mode);
	bool		GetDocStats(TextViewDocStats *stats);
	
private:

	//
	//	Internal private functions
	//

	int32_t		OnIndexTimer();
	void		IndexChanged();
	void		NotifyIndexChange();

	//
	//	Lookups for the EDIT control messages (TextViewFile.cpp). They must answer, so
	//	their lookups index as far as they need.
	//
	TEXT_RESULT	IndexUntilKnown(const std::function<TEXT_RESULT()> &lookup);
	bool		CharFromPos(const TextPos &pos, size_w *chars);
	bool		PosFromChar(size_w chars, TextCoord *coord);
	bool		LineFromPos(const TextPos &pos, size_w *lineno);
	bool		LineStart(size_w lineno, TextCoord *coord);
	void		ResetLineCache();

	//
	//	Cursor/Selection
	//
	void		HighlightMatches(tchar_t *szText, int nTextLen, TextRun *attr);
	void		HighlightMatchesUtf8NoCase(tchar_t *szText, int nTextLen, TextRun *attr);

	// Filtering (TextViewFilter.cpp)
    void          WriteFilterContext(TextPos match);
    uint32_t          WriteFilterLines(LineIterator line, uint32_t count);
    bool		StartFilter();
	void		EndFilter(bool fKeepPlace);
	bool		FilterSlice();
	void		WriteFilterLine(TextPos begin, TextPos next);
	void		ShowFilteredLines();
	int32_t		OnFilterTimer();
	bool		GetSelection(TextCoord *start, TextCoord *end);
	void		SelectionColumns(TextCoord *line, uint32_t lineLen, uint32_t *start, uint32_t *end);

	//void		Toggle


	//
	//	Painting support
	//
	void		RefreshWindow();
	void		PaintLine(TextCanvas &canvas, TextCoord *coord, int x);
	void		PaintText(TextCanvas &canvas, TextCoord *coord, int x, int y, TextRect *bounds);
	int			PaintMargin(TextCanvas &canvas, TextCoord *line, int x, int y);
    uint32_t    LineDiff(TextCoord *line, const std::function<void(size_w, size_w)> &inserted = {},
                         size_w columns = (size_w)-1);
    bool        DiffJoins(TextCoord *line, bool previous);

    int32_t		InvalidateRange(TextCoord *start, TextCoord *finish);
	int32_t		InvalidateLine(TextCoord *line, bool forceAnalysis);
	void		UpdateLine(TextCoord *line);


	int			ApplyTextAttributes(TextCoord *line, uint32_t &nColumn, tchar_t *szText, int nTextLen, TextRun *attr);
	int			ApplySelection(TextLayout *layout, TextCoord *line, uint32_t nTextLen);
	int			SyntaxColour(tchar_t *szText, uint32_t nTextLen, TextRun *attr);
	int			StripCRLF(tchar_t *szText, TextRun *attrList, int nLength, bool fAllow);
	int			CRLF_size(tchar_t *szText, int nLength);

	//
	//	Font support
	//
	
	//
	//	
	//
	int			NeatTextYOffset(int font);
	//int		TabWidth();
	int			LeftMarginWidth();
	void		UpdateMarginWidth();
	LINEINFO *	GetLineInfo(uint32_t nLineNo);

	//
	//	Caret/Cursor positioning
	//
	bool		MouseCoordToTextCoord(int x, int y, TextCoord *coord, int *px);
	bool		TextCoordToX(TextCoord *coord, bool fTrailing, int *x);
	void		RepositionCaret();
	//void		MoveCaret(int x, int y);
	void		UpdateCaretXY(int x, TextCoord *coord);
	void		UpdateCaretCoord(TextCoord *coord, bool fTrailing, int *outx=0);
	void		UpdateViewState(bool fAdvancing);
	bool		SetCursorCoord(TextCoord *coord);

	void		MoveWordPrev();
	void		MoveWordNext();
	void		MoveWordStart();
	void		MoveWordEnd();
	void		MoveCharPrev();
	void		MoveCharNext();
	void		MoveCurrentLineStart();
	void		MoveCurrentLineEnd();
	void		MoveLineUp(int numLines);
	void		MoveLineDown(int numLines);
	void		MovePageUp();
	void		MovePageDown();
	void		MoveFileStart();
	void		MoveFileEnd();

	bool		GetLineLayout(TextCoord *line, LINECACHE **lineCache);
	bool		CoordAtPreviousLineEnd(TextCoord *coord, TextCoord *result);
	bool		CoordAtNextLineStart(TextCoord *coord, TextCoord *result);
	void		SetCursorLinePos(int charPos);

	//
	//	Editing
	//	
	bool		ForwardDelete();
	bool		BackDelete();
    uint32_t       EnterText(tchar_t *szText, uint32_t nLength, bool forceInsert = false);

	//
	//	Scrolling
	//
	void		ClientRect(TextRect *rect);
	bool		ScrollRgn(int dx, int dy, bool fReturnUpdateRgn, TextRect *scrolled);
	void		InvalidateOutside(const TextRect &inside);
	void		Scroll(int dx, int dy);
	bool		SetScrollCoord(TextCoord *coord);
	bool		LastScrollCoord(TextCoord *top);
	void		AdjustCoordsForChange(TextChange *change);
	bool		ViewportLineFromRow(uint32_t row, TextCoord *coord);
	bool		ViewportRowFromLine(TextCoord *line, uint32_t *row);
	int			ScrollVByLines(int dy);
	void		ScrollToCaret();
	void		ScrollToCoord(int xpos, TextCoord *coord);
	void		ScrollToDocumentEnd();
	void		SetupScrollbars();
	void		UpdateMetrics();
	void		RecalcLineHeight();
	bool		PinToBottomCorner();

	//
	//	TextView configuration
	//
	uint32_t		SetVar(uint32_t nVar, uint32_t nValue);
	uint32_t		GetVar(uint32_t nVar);
	uint32_t		GetStyleMask(uint32_t uMask);
	bool		CheckStyle(uint32_t uMask);

	uint32_t	GetColour(uint32_t idx);
	uint32_t	ResolveColour(uint32_t col);
	uint32_t	LineColour(TextCoord *line);
	uint32_t	LongColour(TextCoord *line);

	//
	//	Miscallaneous
	//
	intptr_t	NotifyParent(uint32_t nNotifyCode, void *info = 0);
	void		NotifyCursorChange();



	//
	// ------ Internal TextView State ------
	//

	TextViewHost *m_pHost;		// redraws, scrolling, the caret, timers, clipboard, notifications
	uint32_t		m_uStyleFlags;

	// File-related data
	// Display/scroll line count; may be estimated or EOF-clamped while lazy indexing is incomplete.
	size_w		m_nLineCount;

	// Font-related data	
	int			m_nFontWidth;
	int			m_nMaxAscent;
	int			m_nLineHeight;
	int			m_nHeightAbove;
	int			m_nHeightBelow;

	// Scrollbar-related data
	TextCoord	m_scrollVPos;		// top line of the view
	int			m_nHScrollPos;
	int			m_nHScrollMax;

	int			m_nLongestLine;		// widest line laid out so far, in columns
	int			m_nWindowLines;
	int			m_nWindowColumns;

	// Cursor/Caret position. The caret is the moving end of the selection;
	// m_selAnchor is the fixed end, equal to the caret when nothing is selected.
	TextCoord	m_cursorPos;
	TextCoord	m_selAnchor;
	TextCoord	m_selMarginLine;	// line clicked in the margin during a margin selection
	int			m_nCaretPosX;
	int			m_nAnchorPosX;
	
	SELMODE		m_nSelectionMode;
	SELMODE		m_nSelectionType;
	CURPOS		m_cpBlockStart;
	CURPOS		m_cpBlockEnd;
	uint32_t		m_nEditMode;

	// Display-related data
	int			m_nTabWidthChars;
	uint32_t		m_nCaretWidth;
	int			m_nLongLineLimit;
	int			m_nCRLFMode;	// a new document's is the platform's (DEFAULT_LINE_FORMAT)
	
	LINEINFO	m_LineInfo[MAX_LINE_INFO];
	int			m_nLineInfoCount;

	// Margin information
	int			m_nLinenoWidth;
	//TextRect		m_rcBorder;

	uint32_t	m_rgbColourList[TXC_MAX_COLOURS];

	// The text ShowMatches highlights (none while empty), and whether its case matters
	std::basic_string<tchar_t> m_sMatchText;
	bool		m_fMatchCase = false;

	// Showing only the lines holding a match: they're written to a temp file
	// as they're found, which m_pTextDoc shows, read-only, while m_pSourceDoc
	// holds the whole document. Each line's start in it is kept, to go back to.
	bool		m_fFilter = false;
	TextDocument *m_pSourceDoc = 0;
	TempAppendFile	m_filterFile;
	TextPos		m_filterFrom;					// how far through m_pSourceDoc filtering has got
	std::vector<TextPos> m_filterOrigins;
    TextPos              m_filterWrittenTo;     // end of the last source line included
    uint32_t                 m_filterAfter     = 0; // context still wanted if the source grows
    uint32_t                 m_nFilterContext  = 2;
    uint32_t		m_nFilterEditMode = 0;			// the whole document's edit mode, meanwhile

	// Runtime data
	uintptr_t	m_nScrollTimer;
	int			m_nScrollCounter;
	bool		m_fIndexing;		// the index timer is running
	uint32_t		m_nIndexPercent;	// as the parent was last told
	bool		m_fHideCaret;
	//bool		m_fTransparent;

	// The lines laid out most recently
	LINECACHE   *m_lineCache;
	TextLayout	*GetLayout(TextCoord *coord);
	LINECACHE   *GetLineCache(TextCoord *coord);

	TextDocument *m_pTextDoc;
};

#endif
