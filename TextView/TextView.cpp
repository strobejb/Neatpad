//
//	MODULE:		TextView.cpp
//
//	PURPOSE:	Implementation of the TextView control
//
//	NOTES:		www.catch22.net
//
#define _WIN32_WINNT 0x501
#define STRICT
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <tchar.h>
#include <stdio.h>

// for the EM_xxx message constants
#include <richedit.h>

#include "TextView.h"
#include "TextViewInternal.h"
#include "racursor.h"

#if !defined(UNICODE)
#error "Please build as Unicode only!"
#endif

#if !defined(GetWindowLongPtr)
#error "Latest Platform SDK is required to build Neatpad - try PSDK-Feb-2003
#endif

#pragma comment(lib, "comctl32.lib")

//
//	Constructor for TextView class
//
TextView::TextView(HWND hwnd)
{
	m_hWnd = hwnd;

	m_hTheme = OpenThemeData(hwnd, L"edit");

	// Font-related data
	m_nNumFonts		= 1;
	m_nHeightAbove	= 0;
	m_nHeightBelow	= 0;
	
	// File-related data
	m_nLineCount   = 0;
	m_nLongestLine = 0;	
	

	// Scrollbar related data
	m_nVScrollPos = 0;
	m_nHScrollPos = 0;
	m_nVScrollMax = 0;
	m_nHScrollMax = 0;

	// Display-related data
	m_nTabWidthChars = 4;
	m_uStyleFlags	 = 0;
	m_nCaretWidth	 = 0;
	m_nLongLineLimit = 80;
	m_nLineInfoCount = 0;
	m_nCRLFMode		 = TXL_CRLF;//ALL;

	// allocate the USPDATA cache
	m_uspCache		= new USPCACHE[USP_CACHE_SIZE];
	
	for(int i = 0; i < USP_CACHE_SIZE; i++)
	{
		m_uspCache[i].usage   = 0;
		m_uspCache[i].lineno  = 0;
		m_uspCache[i].uspData = UspAllocate();
	}

	SystemParametersInfo(SPI_GETCARETWIDTH, 0, &m_nCaretWidth, 0);

	if(m_nCaretWidth == 0)
		m_nCaretWidth = 2;

	// Default display colours
	m_rgbColourList[TXC_FOREGROUND]		= SYSCOL(COLOR_WINDOWTEXT);
	m_rgbColourList[TXC_BACKGROUND]		= SYSCOL(COLOR_WINDOW);			// RGB(34,54,106)
	m_rgbColourList[TXC_HIGHLIGHTTEXT]	= SYSCOL(COLOR_HIGHLIGHTTEXT);
	m_rgbColourList[TXC_HIGHLIGHT]		= SYSCOL(COLOR_HIGHLIGHT);
	m_rgbColourList[TXC_HIGHLIGHTTEXT2]	= SYSCOL(COLOR_WINDOWTEXT);//INACTIVECAPTIONTEXT);
	m_rgbColourList[TXC_HIGHLIGHT2]		= SYSCOL(COLOR_3DFACE);//INACTIVECAPTION);
	m_rgbColourList[TXC_SELMARGIN1]		= SYSCOL(COLOR_3DFACE);
	m_rgbColourList[TXC_SELMARGIN2]		= SYSCOL(COLOR_3DHIGHLIGHT);
	m_rgbColourList[TXC_LINENUMBERTEXT]	= SYSCOL(COLOR_3DSHADOW);
	m_rgbColourList[TXC_LINENUMBER]		= SYSCOL(COLOR_3DFACE);
	m_rgbColourList[TXC_LONGLINETEXT]	= SYSCOL(COLOR_3DSHADOW);
	m_rgbColourList[TXC_LONGLINE]		= SYSCOL(COLOR_3DFACE);
	m_rgbColourList[TXC_CURRENTLINETEXT] = SYSCOL(COLOR_WINDOWTEXT);
	m_rgbColourList[TXC_CURRENTLINE]	 = RGB(230,240,255);


	// Runtime data
	m_nSelectionMode	= SEL_NONE;
	m_nEditMode			= MODE_INSERT;
	m_nScrollTimer		= 0;
	m_fHideCaret		= false;
	m_hUserMenu			= 0;
	m_hImageList		= 0;
	
	m_nSelectionStart	= 0;
	m_nSelectionEnd		= 0;
	m_nSelectionType	= SEL_NONE;
	m_nCursorOffset		= 0;
	m_nCurrentLine		= 0;

	m_nLinenoWidth		= 0;
	m_nCaretPosX		= 0;
	m_nAnchorPosX		= 0;

	//SetRect(&m_rcBorder, 2, 2, 2, 2);

	m_pTextDoc = new TextDocument();

	m_hMarginCursor = CreateCursor(GetModuleHandle(0), 21, 5, 32, 32, XORMask, ANDMask);
	
	//
	//	The TextView state must be fully initialized before we
	//	start calling member-functions
	//

	memset(m_uspFontList, 0, sizeof(m_uspFontList));

	// Set the default font
	OnSetFont((HFONT)GetStockObject(ANSI_FIXED_FONT));

	UpdateMetrics();
	UpdateMarginWidth();
}

//
//	Destructor for TextView class
//
TextView::~TextView()
{
	if(m_pTextDoc)
		delete m_pTextDoc;

	DestroyCursor(m_hMarginCursor);

	for(int i = 0; i < USP_CACHE_SIZE; i++)
		UspFree(m_uspCache[i].uspData);

	CloseThemeData(m_hTheme);
}

ULONG TextView::NotifyParent(UINT nNotifyCode, NMHDR *optional)
{
	UINT  nCtrlId = GetWindowLong(m_hWnd, GWL_ID);
	NMHDR nmhdr   = { m_hWnd, nCtrlId, nNotifyCode };
	NMHDR *nmptr  = &nmhdr;  
	
	if(optional)
	{
		nmptr  = optional;
		*nmptr = nmhdr;
	}

	return SendMessage(GetParent(m_hWnd), WM_NOTIFY, (WPARAM)nCtrlId, (LPARAM)nmptr);
}

VOID TextView::UpdateMetrics()
{
	RECT rect;
	GetClientRect(m_hWnd, &rect);

	OnSize(0, rect.right, rect.bottom);
	RefreshWindow();

	RepositionCaret();
}

LONG TextView::OnSetFocus(HWND hwndOld)
{
	CreateCaret(m_hWnd, (HBITMAP)NULL, m_nCaretWidth, m_nLineHeight);
	RepositionCaret();

	ShowCaret(m_hWnd);
	RefreshWindow();
	return 0;
}

LONG TextView::OnKillFocus(HWND hwndNew)
{
	// if we are making a selection when we lost focus then
	// stop the selection logic
	if(m_nSelectionMode != SEL_NONE)
	{
		OnLButtonUp(0, 0, 0);
	}

	HideCaret(m_hWnd);
	DestroyCaret();
	RefreshWindow();
	return 0;
}

ULONG TextView::SetStyle(ULONG uMask, ULONG uStyles)
{
	ULONG oldstyle = m_uStyleFlags;

	m_uStyleFlags  = (m_uStyleFlags & ~uMask) | uStyles;

	ResetLineCache();

	// update display here
	UpdateMetrics();
	RefreshWindow();

	return oldstyle;
}

ULONG TextView::SetVar(ULONG nVar, ULONG nValue)
{
	return 0;//oldval;
}

ULONG TextView::GetVar(ULONG nVar)
{
	return 0;
}

ULONG TextView::GetStyleMask(ULONG uMask)
{
	return m_uStyleFlags & uMask;
}
	
bool TextView::CheckStyle(ULONG uMask)
{
	return (m_uStyleFlags & uMask) ? true : false;
}

int TextView::SetCaretWidth(int nWidth)
{
	int oldwidth = m_nCaretWidth;
	m_nCaretWidth  = nWidth;

	return oldwidth;
}

BOOL TextView::SetImageList(HIMAGELIST hImgList)
{
	m_hImageList = hImgList;
	return TRUE;
}

LONG TextView::SetLongLine(int nLength)
{
	int oldlen = m_nLongLineLimit;
	m_nLongLineLimit = nLength;
	return oldlen;
}

int CompareLineInfo(LINEINFO *elem1, LINEINFO *elem2)
{
	if(elem1->nLineNo < elem2->nLineNo)
		return -1;
	if(elem1->nLineNo > elem2->nLineNo)
		return 1;
	else
		return 0;
}

int TextView::SetLineImage(ULONG nLineNo, ULONG nImageIdx)
{
	LINEINFO *linfo = GetLineInfo(nLineNo);

	// if already a line with an image
	if(linfo)
	{
		linfo->nImageIdx = nImageIdx;
	}
	else
	{
		linfo = &m_LineInfo[m_nLineInfoCount++];
		linfo->nLineNo = nLineNo;
		linfo->nImageIdx = nImageIdx;

		// sort the array
		qsort(
			m_LineInfo, 
			m_nLineInfoCount, 
			sizeof(LINEINFO), 
			(COMPAREPROC)CompareLineInfo
			);

	}
	return 0;
}

LINEINFO* TextView::GetLineInfo(ULONG nLineNo)
{
	LINEINFO key = { nLineNo, 0 };

	// perform the binary search
	return (LINEINFO *)	bsearch(
							&key, 
							m_LineInfo,
							m_nLineInfoCount, 
							sizeof(LINEINFO), 
							(COMPAREPROC)CompareLineInfo
							);
}

ULONG TextView::SelectionSize()
{
	ULONG s1 = min(m_nSelectionStart, m_nSelectionEnd); 
	ULONG s2 = max(m_nSelectionStart, m_nSelectionEnd); 
	return s2 - s1;
}

ULONG TextView::SelectAll()
{
	m_nSelectionStart = 0;
	m_nSelectionEnd   = m_pTextDoc->size();
	m_nCursorOffset   = m_nSelectionEnd;

	Smeg(TRUE);
	RefreshWindow();
	return 0;
}

//
//	Public memberfunction 
//
LONG WINAPI TextView::WndProc(UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch(msg)
	{
	// Draw contents of TextView whenever window needs updating
	case WM_ERASEBKGND:
		return 1;

	// Need to custom-draw the border for XP/Vista themes
	case WM_NCPAINT:
		return OnNcPaint((HRGN)wParam);

	case WM_PAINT:
		return OnPaint();

	// Set a new font 
	case WM_SETFONT:
		return OnSetFont((HFONT)wParam);

	case WM_SIZE:
		return OnSize(wParam, LOWORD(lParam), HIWORD(lParam));

	case WM_VSCROLL:
		return OnVScroll(LOWORD(wParam), HIWORD(wParam));

	case WM_HSCROLL:
		return OnHScroll(LOWORD(wParam), HIWORD(wParam));

	case WM_MOUSEACTIVATE:
		return OnMouseActivate((HWND)wParam, LOWORD(lParam), HIWORD(lParam));

	case WM_CONTEXTMENU:
		return OnContextMenu((HWND)wParam, (short)LOWORD(lParam), (short)HIWORD(lParam));

	case WM_MOUSEWHEEL:
		return OnMouseWheel((short)HIWORD(wParam));

	case WM_SETFOCUS:
		return OnSetFocus((HWND)wParam);

	case WM_KILLFOCUS:
		return OnKillFocus((HWND)wParam);

	// make sure we get arrow-keys, enter, tab, etc when hosted inside a dialog
	case WM_GETDLGCODE:
		return DLGC_WANTALLKEYS;

	case WM_LBUTTONDOWN:
		return OnLButtonDown(wParam, (short)LOWORD(lParam), (short)HIWORD(lParam));

	case WM_LBUTTONUP:
		return OnLButtonUp(wParam, (short)LOWORD(lParam), (short)HIWORD(lParam));

	case WM_LBUTTONDBLCLK:
		return OnLButtonDblClick(wParam, (short)LOWORD(lParam), (short)HIWORD(lParam));

	case WM_MOUSEMOVE:
		return OnMouseMove(wParam, (short)LOWORD(lParam), (short)HIWORD(lParam));

	case WM_KEYDOWN:
		return OnKeyDown(wParam, lParam);

	case WM_UNDO: case TXM_UNDO: case EM_UNDO:
		return Undo();

	case TXM_REDO: case EM_REDO:
		return Redo();

	case TXM_CANUNDO: case EM_CANUNDO:
		return CanUndo();

	case TXM_CANREDO: case EM_CANREDO:
		return CanRedo();

	case WM_CHAR:
		return OnChar(wParam, lParam);

	case WM_SETCURSOR:
		
		if(LOWORD(lParam) == HTCLIENT)
			return TRUE;

		break;

	case WM_COPY:
		return OnCopy();

	case WM_CUT:
		return OnCut();

	case WM_PASTE:
		return OnPaste();

	case WM_CLEAR:
		return OnClear();

	case WM_GETTEXT:
		return 0;

	case WM_TIMER:
		return OnTimer(wParam);

	//
	case TXM_OPENFILE:
		return OpenFile((TCHAR *)lParam);

	case TXM_CLEAR:
		return ClearFile();

	case TXM_SETLINESPACING:
		return SetLineSpacing(wParam, lParam);

	case TXM_ADDFONT:
		return AddFont((HFONT)wParam);

	case TXM_SETCOLOR:
		return SetColour(wParam, lParam);

	case TXM_SETSTYLE:
		return SetStyle(wParam, lParam);

	case TXM_SETCARETWIDTH:
		return SetCaretWidth(wParam);

	case TXM_SETIMAGELIST:
		return SetImageList((HIMAGELIST)wParam);

	case TXM_SETLONGLINE:
		return SetLongLine(lParam);

	case TXM_SETLINEIMAGE:
		return SetLineImage(wParam, lParam);

	case TXM_GETFORMAT:
		return m_pTextDoc->getformat();

	case TXM_GETSELSIZE:
		return SelectionSize();

	case TXM_SETSELALL:
		return SelectAll();

	case TXM_GETCURPOS:
		return m_nCursorOffset;

	case TXM_GETCURLINE:
		return m_nCurrentLine;

	case TXM_GETCURCOL:
		ULONG nOffset;
		GetUspData(0, m_nCurrentLine, &nOffset);
		return m_nCursorOffset - nOffset;

	case TXM_GETEDITMODE:
		return m_nEditMode;

	case TXM_SETEDITMODE:
		lParam		= m_nEditMode;
		m_nEditMode = wParam;
		return lParam;

	case TXM_SETCONTEXTMENU:
		m_hUserMenu = (HMENU)wParam;
		return 0;

	default:
		break;
	}

	return DefWindowProc(m_hWnd, msg, wParam, lParam);
}

//
//	Win32 TextView window procedure stub
//
LRESULT WINAPI TextViewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	TextView *ptv = (TextView *)GetWindowLongPtr(hwnd, 0);

	switch(msg)
	{
	// First message received by any window - make a new TextView object
	// and store pointer to it in our extra-window-bytes
	case WM_NCCREATE:

		if((ptv = new TextView(hwnd)) == 0)
			return FALSE;

		SetWindowLongPtr(hwnd, 0, (LONG)ptv);
		return TRUE;

	// Last message received by any window - delete the TextView object
	case WM_NCDESTROY:
		delete ptv;
		SetWindowLongPtr(hwnd, 0, 0);
		return 0;

	// Pass everything to the TextView window procedure
	default:
		if(ptv)
			return ptv->WndProc(msg, wParam, lParam);
		else
			return 0;
	}
}

//
//	Register the TextView window class
//
BOOL InitTextView()
{
	WNDCLASSEX	wcx;

	//Window class for the main application parent window
	wcx.cbSize			= sizeof(wcx);
	wcx.style			= CS_DBLCLKS;
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
//		L"EDIT", L"",
		TEXTVIEW_CLASS, _T(""), 
		WS_VSCROLL |WS_HSCROLL | WS_CHILD | WS_VISIBLE,
		0, 0, 0, 0, 
		hwndParent, 
		0, 
		GetModuleHandle(0), 
		0);
}

