#ifndef TEXTVIEW_INTERNAL_INCLUDED
#define TEXTVIEW_INTERNAL_INCLUDED

#define TEXTBUFSIZE  32

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

// maximum fonts that a TextView can hold
#define MAX_FONTS 32

//
//	TextView - internal window implementation
//
class TextView
{
public:

	TextView(HWND hwnd);
	~TextView();

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
	//	Public interface
	//
	LONG OpenFile(TCHAR *szFileName);
	LONG ClearFile();

	LONG AddFont(HFONT);
	LONG SetFont(HFONT, int idx);
	LONG SetLineSpacing(int nAbove, int nBelow);
	COLORREF SetColour(UINT idx, COLORREF rgbColour);

private:

	void PaintLine(HDC hdc, ULONG line);
	void PaintText(HDC hdc, ULONG nLineNo, RECT *rect);

	int  ApplyTextAttributes(ULONG nLineNo, ULONG offset, TCHAR *szText, int nTextLen, ATTR *attr);
	int  NeatTextOut(HDC hdc, int xpos, int ypos, TCHAR *szText, int nLen, int nTabOrigin, ATTR *attr);
	
	int  PaintCtrlChar(HDC hdc, int xpos, int ypos, ULONG chValue, FONT *fa);
	void InitCtrlCharFontAttr(HDC hdc, FONT *fa);

	void RefreshWindow();
	LONG InvalidateRange(ULONG nStart, ULONG nFinish);

	int  CtrlCharWidth(HDC hdc, ULONG chValue, FONT *fa);
	int  NeatTextWidth(HDC hdc, TCHAR *buf, int len, int nTabOrigin);
	int	 TabWidth();

	BOOL  MouseCoordToFilePos(int x, int y, ULONG *pnLineNo, ULONG *pnCharOffset, ULONG *pnFileOffset, int *px);
	ULONG RepositionCaret();

	COLORREF GetColour(UINT idx);

	VOID	SetupScrollbars();
	VOID	UpdateMetrics();
	VOID	RecalcLineHeight();
	bool    PinToBottomCorner();

	void	Scroll(int dx, int dy);
	HRGN	ScrollRgn(int dx, int dy, bool fReturnUpdateRgn);

	HWND	m_hWnd;

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

	COLORREF m_rgbColourList[TXC_MAX_COLOURS];

	// Runtime data
	bool	m_fMouseDown;
	UINT	m_nScrollTimer;
	int		m_nScrollCounter;

	// File-related data
	ULONG	m_nLineCount;

	TextDocument *m_pTextDoc;
};

#endif