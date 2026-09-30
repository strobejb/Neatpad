#ifndef TEXTVIEW_INTERNAL_INCLUDED
#define TEXTVIEW_INTERNAL_INCLUDED

#define LONGEST_LINE 0x100

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

	LONG OnPaint();
	LONG OnSetFont(HFONT hFont);
	LONG OnSize(UINT nFlags, int width, int height);
	LONG OnVScroll(UINT nSBCode, UINT nPos);
	LONG OnHScroll(UINT nSBCode, UINT nPos);
	LONG OnMouseWheel(int nDelta);

	LONG OpenFile(TCHAR *szFileName);
	LONG ClearFile();

	LONG AddFont(HFONT);
	LONG SetFont(HFONT, int idx);
	LONG SetLineSpacing(int nAbove, int nBelow);

private:

	void PaintLine(HDC hdc, ULONG line);
	void PaintText(HDC hdc, ULONG nLineNo, RECT *rect);

	int  ApplyTextAttributes(ULONG nLineNo, ULONG offset, TCHAR *szText, int nTextLen, ATTR *attr);
	int  NeatTextOut(HDC hdc, int xpos, int ypos, TCHAR *szText, int nLen, int nTabOrigin, ATTR *attr);
	
	int  PaintCtrlChar(HDC hdc, int xpos, int ypos, ULONG chValue, FONT *font);
	void InitCtrlCharFontAttr(HDC hdc, FONT *font);

	void RefreshWindow();

	COLORREF GetColour(UINT idx);

	VOID	RecalcLineHeight();
	VOID	SetupScrollbars();
	VOID	UpdateMetrics();
	bool    PinToBottomCorner();
	void	Scroll(int dx, int dy);

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

	// File-related data
	ULONG	m_nLineCount;

	TextDocument *m_pTextDoc;
};

#endif