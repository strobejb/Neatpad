#ifndef TEXTVIEW_INTERNAL_INCLUDED
#define TEXTVIEW_INTERNAL_INCLUDED

#define TEXTBUFSIZE  32
#define LINENO_FMT  _T(" %d ")
#define LINENO_PAD	 8

#include <commctrl.h>
#include "TextDocument.h"

//
//	ATTR - text character attribute
//	
typedef struct
{
	COLORREF	fg;			// foreground colour
	COLORREF	bg;			// background colour
	ULONG		style;		// possible font-styling information

} ATTR;

//
//	FONT - font attributes
//
typedef struct
{
	// Windows font information
	HFONT		hFont;		
	TEXTMETRIC	tm;

	// dimensions needed for control-character 'bitmaps'
	int			nInternalLeading;
	int			nDescent;

} FONT;

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

enum SELMODE { SELMODE_NONE, SELMODE_NORMAL, SELMODE_MARGIN };

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
	LONG OnSetFont(HFONT hFont);
	LONG OnSize(UINT nFlags, int width, int height);
	LONG OnVScroll(UINT nSBCode, UINT nPos);
	LONG OnHScroll(UINT nSBCode, UINT nPos);
	LONG OnMouseWheel(int nDelta);
	LONG OnTimer(UINT nTimer);

	LONG OnMouseActivate(HWND hwndTop, UINT nHitTest, UINT nMessage);
	LONG OnLButtonDown(UINT nFlags, int x, int y);
	LONG OnLButtonUp(UINT nFlags, int x, int y);
	LONG OnMouseMove(UINT nFlags, int x, int y);

	LONG OnSetFocus(HWND hwndOld);
	LONG OnKillFocus(HWND hwndNew);
	
	//
	//	
	//
	LONG OpenFile(TCHAR *szFileName);
	LONG ClearFile();

	LONG AddFont(HFONT);
	LONG SetFont(HFONT, int idx);
	LONG SetLineSpacing(int nAbove, int nBelow);
	LONG SetLongLine(int nLength);
	COLORREF SetColour(UINT idx, COLORREF rgbColour);

	//
	//	Private Helper functions
	//
	void PaintLine(HDC hdc, ULONG line);
	void PaintText(HDC hdc, ULONG nLineNo, RECT *rect);
	int  PaintMargin(HDC hdc, ULONG line, RECT *margin);

	int   ApplyTextAttributes(ULONG nLineNo, ULONG offset, ULONG &nColumn, TCHAR *szText, int nTextLen, ATTR *attr);
	int   NeatTextOut(HDC hdc, int xpos, int ypos, TCHAR *szText, int nLen, int nTabOrigin, ATTR *attr);
	
	int  PaintCtrlChar(HDC hdc, int xpos, int ypos, ULONG chValue, FONT *fa);
	void InitCtrlCharFontAttr(HDC hdc, FONT *fa);

	void RefreshWindow();
	LONG InvalidateRange(ULONG nStart, ULONG nFinish);
	LONG InvalidateLine(ULONG nLineNo);
	VOID UpdateLine(ULONG nLineNo);

	int  CtrlCharWidth(HDC hdc, ULONG chValue, FONT *fa);
	int  NeatTextYOffset(FONT *font);
	int  NeatTextWidth(HDC hdc, TCHAR *buf, int len, int nTabOrigin);
	int	 TabWidth();
	int  LeftMarginWidth();
	void UpdateMarginWidth();
	int	 SetCaretWidth(int nWidth);
	BOOL SetImageList(HIMAGELIST hImgList);

	BOOL  MouseCoordToFilePos(int x, int y, ULONG *pnLineNo, ULONG *pnCharOffset, ULONG *pnFileOffset, int *px, ULONG *pnLineLen=0);
	ULONG RepositionCaret();
	VOID  MoveCaret(int x, int y);

	ULONG	SetStyle(ULONG uMask, ULONG uStyles);
	
	ULONG	SetVar(ULONG nVar, ULONG nValue);
	ULONG	GetVar(ULONG nVar);

	ULONG	GetStyleMask(ULONG uMask);
	bool	CheckStyle(ULONG uMask);

	COLORREF GetColour(UINT idx);
	COLORREF LineColour(ULONG nLineNo);
	COLORREF LongColour(ULONG nLineNo);

	int		SetLineImage(ULONG nLineNo, ULONG nImageIdx);
	LINEINFO *GetLineInfo(ULONG nLineNo);

	VOID	SetupScrollbars();
	VOID	UpdateMetrics();
	VOID	RecalcLineHeight();
	bool    PinToBottomCorner();

	void	Scroll(int dx, int dy);
	HRGN	ScrollRgn(int dx, int dy, bool fReturnUpdateRgn);

	HWND	m_hWnd;
	ULONG	m_uStyleFlags;

	// Font-related data	
	FONT	m_FontAttr[MAX_FONTS];
	int		m_nNumFonts;
	int		m_nFontWidth;
	int		m_nMaxAscent;
	int		m_nLineHeight;
	int		m_nHeightAbove;
	int		m_nHeightBelow;

	// Scrollbar-related data
	ULONG	m_nVScrollPos;
	ULONG	m_nVScrollMax;
	int		m_nHScrollPos;
	int		m_nHScrollMax;

	int		m_nLongestLine;
	int		m_nWindowLines;
	int		m_nWindowColumns;

	// Display-related data
	int		m_nTabWidthChars;
	ULONG	m_nSelectionStart;
	ULONG	m_nSelectionEnd;
	ULONG	m_nCursorOffset;
	DWORD	m_nCaretWidth;
	ULONG	m_nCurrentLine;
	int		m_nLongLineLimit;
	
	LINEINFO	m_LineInfo[MAX_LINE_INFO];
	int			m_nLineInfoCount;

	int			m_nLinenoWidth;
	HCURSOR		m_hMarginCursor;
	RECT		m_rcBorder;

	COLORREF	m_rgbColourList[TXC_MAX_COLOURS];

	// Runtime data
	SELMODE		m_nSelectionMode;
	ULONG		m_nSelMarginOffset1;
	ULONG		m_nSelMarginOffset2;
	UINT		m_nScrollTimer;
	int			m_nScrollCounter;
	bool		m_fHideCaret;
	bool		m_fTransparent;
	HIMAGELIST	m_hImageList;

	// File-related data
	ULONG		m_nLineCount;

	TextDocument *m_pTextDoc;
};

#endif