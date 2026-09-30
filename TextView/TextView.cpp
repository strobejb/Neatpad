//
//	MODULE:		TextView.cpp
//
//	PURPOSE:	Implementation of the TextView control
//
//	NOTES:		www.catch22.net
//
#define _WIN32_WINNT 0x400
#include <windows.h>
#include <tchar.h>
#include "TextView.h"
#include "TextViewInternal.h"

//
//	Constructor for TextView class
//
TextView::TextView(HWND hwnd)
{
	m_hWnd = hwnd;

	// Set the default font
	OnSetFont((HFONT)GetStockObject(ANSI_FIXED_FONT));

	// Scrollbar related data
	m_nVScrollPos = 0;
	m_nHScrollPos = 0;
	m_nVScrollMax = 0;
	m_nHScrollMax = 0;

	// File-related data
	m_nLineCount   = 0;
	m_nLongestLine = 0;

	m_pTextDoc = new TextDocument();

	SetupScrollbars();
}

//
//	Destructor for TextView class
//
TextView::~TextView()
{
	if(m_pTextDoc)
		delete m_pTextDoc;
}

VOID TextView::UpdateMetrics()
{
	RECT rect;
	GetClientRect(m_hWnd, &rect);

	OnSize(0, rect.right, rect.bottom);
	RefreshWindow();
}
//
//	Set a new font
//
LONG TextView::OnSetFont(HFONT hFont)
{
	HDC hdc;
	TEXTMETRIC tm;
	HANDLE hOld;

	m_hFont = hFont;

	hdc  = GetDC(m_hWnd);
	hOld = SelectObject(hdc, hFont);

	GetTextMetrics(hdc, &tm);

	m_nFontHeight = tm.tmHeight;
	m_nFontWidth  = tm.tmAveCharWidth;

	SelectObject(hdc, hOld);
	ReleaseDC(m_hWnd, hdc);

	UpdateMetrics();

	return 0;
}

//
//	Win32 TextView window procedure.
//
LRESULT WINAPI TextViewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	TextView *ptv = (TextView *)GetWindowLong(hwnd, 0);

	switch(msg)
	{
	// First message received by any window - make a new TextView object
	// and store pointer to it in our extra-window-bytes
	case WM_NCCREATE:

		if((ptv = new TextView(hwnd)) == 0)
			return FALSE;

		SetWindowLong(hwnd, 0, (LONG)ptv);
		return TRUE;

	// Last message received by any window - delete the TextView object
	case WM_NCDESTROY:

		delete ptv;
		return 0;

	// Draw contents of TextView whenever window needs updating
	case WM_PAINT:
		return ptv->OnPaint();

	// Set a new font 
	case WM_SETFONT:
		return ptv->OnSetFont((HFONT)wParam);

	case WM_SIZE:
		return ptv->OnSize(wParam, LOWORD(lParam), HIWORD(lParam));

	case WM_VSCROLL:
		return ptv->OnVScroll(LOWORD(wParam), HIWORD(wParam));

	case WM_HSCROLL:
		return ptv->OnHScroll(LOWORD(wParam), HIWORD(wParam));

	case WM_MOUSEWHEEL:
		return ptv->OnMouseWheel((short)HIWORD(wParam));

	//
	case TXM_OPENFILE:
		return ptv->OpenFile((TCHAR *)lParam);

	case TXM_CLEAR:
		return ptv->ClearFile();

	default:
		break;
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}

//
//	Register the TextView window class
//
BOOL InitTextView()
{
	WNDCLASSEX	wcx;

	//Window class for the main application parent window
	wcx.cbSize			= sizeof(wcx);
	wcx.style			= 0;
	wcx.lpfnWndProc		= TextViewWndProc;
	wcx.cbClsExtra		= 0;
	wcx.cbWndExtra		= sizeof(TextView *);
	wcx.hInstance		= GetModuleHandle(0);
	wcx.hIcon			= 0;
	wcx.hCursor			= LoadCursor (NULL, IDC_IBEAM);
	wcx.hbrBackground	= (HBRUSH)0;		//NO FLICKERING FOR US!!
	wcx.lpszMenuName	= 0;
	wcx.lpszClassName	= TEXTVIEW_CLASS;	
	wcx.hIconSm			= 0;

	return RegisterClassEx(&wcx) ? TRUE : FALSE;
}

//
//	Create a TextView control!
//
HWND CreateTextView(HWND hwndParent)
{
	return CreateWindowEx(WS_EX_CLIENTEDGE, 
		TEXTVIEW_CLASS, _T(""), 
		WS_VSCROLL |WS_HSCROLL | WS_CHILD | WS_VISIBLE,
		0, 0, 0, 0, 
		hwndParent, 
		0, 
		GetModuleHandle(0), 
		0);
}

