#pragma once

#ifndef NEATPAD_TEXTVIEW_INTERNAL_INCLUDED
#define NEATPAD_TEXTVIEW_INTERNAL_INCLUDED

#define TEXTBUFSIZE  128
#define LINENO_FMT  _T(" %2d ")
#define LINENO_PAD	 8

#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>

/*HTHEME  (WINAPI * OpenThemeData_Proc)(HWND hwnd, LPCWSTR pszClassList);
BOOL    (WINAPI * CloseThemeData_Proc)(HTHEME hTheme);
HRESULT (WINAPI * DrawThemeBackground_Proc)(HTHEME hTheme, HDC hdc, int, int, const RECT*, const RECT*);
*/


#include "TextDocument.h"
#include "TextView.h"

#include "..\UspLib\usplib.h"

typedef struct
{
	USPDATA *uspData;
	ULONG	 line_begin;	// byte offset of the line's start (cache key; flushed on every edit)
	ULONG	 usage;			// cache-count

	int		 length;		// length in chars INCLUDING CR/LF
	int		 length_CRLF;	// length in chars EXCLUDING CR/LF

} USPCACHE;

typedef const SCRIPT_LOGATTR CSCRIPT_LOGATTR;

#define USP_CACHE_SIZE 200

//
//	LINEINFO - information about a specific line
//
typedef struct
{
	ULONG	nLineNo;
	int		nImageIdx;

	// more here in the future?

} LINEINFO;

typedef int (__cdecl * COMPAREPROC) (const void *, const void *);

// maximum number of lines that we can hold info for at one time
#define MAX_LINE_INFO 128	

// maximum fonts that a TextView can hold
#define MAX_FONTS 32

enum SELMODE { SEL_NONE, SEL_NORMAL, SEL_MARGIN, SEL_BLOCK };

typedef struct
{
	ULONG	line_begin;		// byte offset of the line's start
	ULONG	xpos;

} CURPOS;

//
//	TextView - internal window implementation
//
class TextView
{
public:

	TextView(HWND hwnd);
	~TextView();

	LONG WINAPI WndProc(UINT msg, WPARAM wParam, LPARAM lParam);

private:

	//
	//	Message handlers
	//
	LONG OnPaint();
	LONG OnNcPaint(HRGN hrgnUpdate);
	LONG OnSetFont(HFONT hFont);
	LONG OnSize(UINT nFlags, int width, int height);
	LONG OnVScroll(UINT nSBCode, UINT nPos);
	LONG OnHScroll(UINT nSBCode, UINT nPos);
	LONG OnMouseWheel(int nDelta);
	LONG OnTimer(UINT nTimer);

	LONG OnMouseActivate(HWND hwndTop, UINT nHitTest, UINT nMessage);
	LONG OnContextMenu(HWND wParam, int x, int y);

	LONG OnLButtonDown(UINT nFlags, int x, int y);
	LONG OnLButtonUp(UINT nFlags, int x, int y);
	LONG OnLButtonDblClick(UINT nFlags, int x, int y);
	LONG OnMouseMove(UINT nFlags, int x, int y);

	LONG OnKeyDown(UINT nKeyCode, UINT nFlags);
	LONG OnChar(UINT nChar, UINT nFlags);

	LONG OnSetFocus(HWND hwndOld);
	LONG OnKillFocus(HWND hwndNew);

	BOOL OnCut();
	BOOL OnCopy();
	BOOL OnPaste();
	BOOL OnClear();
	
private:

	//
	//	Internal private functions
	//
	LONG		OpenFile(TCHAR *szFileName);
	LONG		ClearFile();
	void		ResetLineCache();

	//
	//	Cursor/Selection
	//
	ULONG		SelectionSize();
	ULONG		SelectAll();
	bool		GetSelection(TextCoord *start, TextCoord *end);
	void		SelectionColumns(TextCoord *line, ULONG lineLen, ULONG *start, ULONG *end);

	//void		Toggle


	//
	//	Painting support
	//
	void		RefreshWindow();
	void		PaintLine(HDC hdc, TextCoord *coord, ULONG row, int x, int y, HRGN hrgnUpdate);
	void		PaintText(HDC hdc, TextCoord *coord, int x, int y, RECT *bounds);
	int			PaintMargin(HDC hdc, TextCoord *line, int x, int y);

	LONG		InvalidateRange(TextCoord *start, TextCoord *finish);
	LONG		InvalidateLine(TextCoord *line, bool forceAnalysis);
	VOID		UpdateLine(TextCoord *line);


	int			ApplyTextAttributes(TextCoord *line, ULONG &nColumn, TCHAR *szText, int nTextLen, ATTR *attr);
	int			ApplySelection(USPDATA *uspData, TextCoord *line, ULONG nTextLen);
	int			SyntaxColour(TCHAR *szText, ULONG nTextLen, ATTR *attr);
	int			StripCRLF(TCHAR *szText, ATTR *attrList, int nLength, bool fAllow);
	int			CRLF_size(TCHAR *szText, int nLength);

	//
	//	Font support
	//
	LONG		AddFont(HFONT);
	LONG		AddFontFallback(int slot, HFONT hFont);
	LONG		ClearFontFallbacks(int slot);
	LONG		SetFont(HFONT, int idx);
	LONG		SetLineSpacing(int nAbove, int nBelow);
	LONG		SetLongLine(int nLength);
	
	//
	//	
	//
	int			NeatTextYOffset(USPFONT *font);
	int			TextWidth(HDC hdc, TCHAR *buf, int len);
	//int		TabWidth();
	int			LeftMarginWidth();
	void		UpdateMarginWidth();
	int			SetCaretWidth(int nWidth);
	BOOL		SetImageList(HIMAGELIST hImgList);
	int			SetLineImage(ULONG nLineNo, ULONG nImageIdx);
	LINEINFO *	GetLineInfo(ULONG nLineNo);

	//
	//	Caret/Cursor positioning
	//
	BOOL		MouseCoordToTextCoord(int x, int y, TextCoord *coord, int *px);
	VOID		RepositionCaret();
	//VOID		MoveCaret(int x, int y);
	VOID		UpdateCaretXY(int x, TextCoord *coord);
	VOID		UpdateCaretCoord(TextCoord *coord, BOOL fTrailing, int *outx=0);
	VOID		UpdateViewState(BOOL fAdvancing);
	bool		SetCursorCoord(TextCoord *coord);

	VOID		MoveWordPrev();
	VOID		MoveWordNext();
	VOID		MoveWordStart();
	VOID		MoveWordEnd();
	VOID		MoveCharPrev();
	VOID		MoveCharNext();
	VOID		MoveCurrentLineStart();
	VOID		MoveCurrentLineEnd();
	VOID		MoveLineUp(int numLines);
	VOID		MoveLineDown(int numLines);
	VOID		MovePageUp();
	VOID		MovePageDown();
	VOID		MoveFileStart();
	VOID		MoveFileEnd();

	bool		GetLineLayout(TextCoord *line, USPCACHE **uspCache, CSCRIPT_LOGATTR **logAttr);
	bool		CoordAtPreviousLineEnd(TextCoord *coord, TextCoord *result);
	bool		CoordAtNextLineStart(TextCoord *coord, TextCoord *result);
	VOID		SetCursorLinePos(int charPos);

	//
	//	Editing
	//	
	BOOL		Undo();
	BOOL		Redo();
	BOOL		CanUndo();
	BOOL		CanRedo();
	BOOL		ForwardDelete();
	BOOL		BackDelete();
	ULONG		EnterText(TCHAR *szText, ULONG nLength);

	//
	//	Scrolling
	//
	HRGN		ScrollRgn(int dx, int dy, bool fReturnUpdateRgn);
	void		Scroll(int dx, int dy);
	bool		SetScrollCoord(TextCoord *coord);
	bool		LastScrollCoord(TextCoord *top);
	VOID		AdjustCoordsForChange(TextChange *change);
	bool		ViewportLineFromRow(ULONG row, TextCoord *coord);
	bool		ViewportRowFromLine(TextCoord *line, ULONG *row);
	int			ScrollVByLines(int dy);
	void		ScrollToCaret();
	void		ScrollToCoord(int xpos, TextCoord *coord);
	void		ScrollToDocumentEnd();
	VOID		SetupScrollbars();
	VOID		UpdateMetrics();
	VOID		RecalcLineHeight();
	bool		PinToBottomCorner();

	//
	//	TextView configuration
	//
	ULONG		SetStyle(ULONG uMask, ULONG uStyles);
	ULONG		SetVar(ULONG nVar, ULONG nValue);
	ULONG		GetVar(ULONG nVar);
	ULONG		GetStyleMask(ULONG uMask);
	bool		CheckStyle(ULONG uMask);

	COLORREF	SetColour(UINT idx, COLORREF rgbColour);
	COLORREF	GetColour(UINT idx);
	COLORREF	LineColour(TextCoord *line);
	COLORREF	LongColour(TextCoord *line);

	//
	//	Miscallaneous
	//
	HMENU		CreateContextMenu();
	ULONG		NotifyParent(UINT nNotifyCode, NMHDR *optional = 0);



	//
	// ------ Internal TextView State ------
	//

	HWND		m_hWnd;
	HTHEME		m_hTheme;
	ULONG		m_uStyleFlags;

	// File-related data
	// Display/scroll line count; may be estimated or EOF-clamped while lazy indexing is incomplete.
	ULONG		m_nLineCount;

	// Font-related data	
	USPFONT		m_uspFontList[MAX_FONTS];
	int			m_nNumFonts;
	int			m_nFontWidth;
	int			m_nMaxAscent;
	int			m_nLineHeight;
	int			m_nHeightAbove;
	int			m_nHeightBelow;

	// Scrollbar-related data
	TextCoord	m_scrollVPos;		// top line of the view
	int			m_nHScrollPos;
	int			m_nHScrollMax;

	int			m_nLongestLine;
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
	UINT		m_nEditMode;

	// Display-related data
	int			m_nTabWidthChars;
	DWORD		m_nCaretWidth;
	int			m_nLongLineLimit;
	int			m_nCRLFMode;
	
	LINEINFO	m_LineInfo[MAX_LINE_INFO];
	int			m_nLineInfoCount;

	// Margin information
	int			m_nLinenoWidth;
	HCURSOR		m_hMarginCursor;
	//RECT		m_rcBorder;

	COLORREF	m_rgbColourList[TXC_MAX_COLOURS];

	// Runtime data
	UINT		m_nScrollTimer;
	int			m_nScrollCounter;
	bool		m_fHideCaret;
	//bool		m_fTransparent;
	HIMAGELIST	m_hImageList;
	HMENU		m_hUserMenu;

	// Cache for USPDATA objects
	USPCACHE    *m_uspCache;
	USPDATA		*GetUspData(HDC hdc, TextCoord *coord);
	USPCACHE    *GetUspCache(HDC hdc, TextCoord *coord);

	TextDocument *m_pTextDoc;
};

#endif
