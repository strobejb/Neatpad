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

	LONG OpenFile(TCHAR *szFileName);
	LONG ClearFile();

private:

	void PaintLine(HDC hdc, ULONG line);
	void TabbedExtTextOut(HDC hdc, RECT *rect, TCHAR *buf, int len);

	COLORREF GetTextViewColor(UINT idx);

	HWND	m_hWnd;

	// Font-related data	
	HFONT	m_hFont;
	int		m_nFontWidth;
	int		m_nFontHeight;

	// File-related data
	ULONG	m_nLineCount;

	TextDocument *m_pTextDoc;
};

#endif