#ifndef TEXTVIEW_INTERNAL_INCLUDED
#define TEXTVIEW_INTERNAL_INCLUDED

#define LONGEST_LINE 0x100

#include "TextDocument.h"

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

private:

	void PaintLine(HDC hdc, ULONG line);
	void TabbedExtTextOut(HDC hdc, RECT *rect, TCHAR *buf, int len);
	void RefreshWindow();

	COLORREF GetColour(UINT idx);

	VOID	SetupScrollbars();
	VOID	UpdateMetrics();
	bool    PinToBottomCorner();
	void	Scroll(int dx, int dy);

	HWND	m_hWnd;

	// Font-related data	
	HFONT	m_hFont;
	int		m_nFontWidth;
	int		m_nFontHeight;

	// Scrollbar related data
	ULONG	m_nVScrollPos;
	ULONG   m_nVScrollMax;
	int		m_nHScrollPos;
	int		m_nHScrollMax;

	int		m_nLongestLine;
	int		m_nWindowLines;
	int		m_nWindowColumns;

	// File-related data
	ULONG	m_nLineCount;

	TextDocument *m_pTextDoc;
};

#endif