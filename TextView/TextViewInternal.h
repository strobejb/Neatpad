#ifndef TEXTVIEW_INTERNAL_INCLUDED
#define TEXTVIEW_INTERNAL_INCLUDED

class TextView
{
public:

	TextView(HWND hwnd) : m_hWnd(hwnd) {}
	HWND	m_hWnd;
	int x;

	LRESULT WINAPI OnPaint();
};

#endif