//
//	Neatpad
//	OptionsFont.c
//
//	www.catch22.net
//

#define _CRT_SECURE_NO_DEPRECATE
#define _CRT_NON_CONFORMING_SWPRINTFS
#define _WIN32_WINNT 0x501
#define STRICT

#include <windows.h>
#include <tchar.h>
#include <commctrl.h>
#include <shellapi.h>
#include "Neatpad.h"
#include "..\TextView\TextView.h"
#include "resource.h"

#include <uxtheme.h>
#include <tmschema.h>

COLORREF LightenRGB(COLORREF rgbColor, int amt);

#define MSG_UPDATE_PREVIEW (WM_USER+1)

static HFONT g_hBoldFont;
static HFONT g_hNormalFont;
static HICON g_hIcon2;			// TrueType icon
static HICON g_hIcon3;			// OpenType icon
static int	 g_nFontHeight;

static COLORREF g_crPreviewFG;
static COLORREF g_crPreviewBG;
static COLORREF g_rgbTempColourList[TXC_MAX_COLOURS];
static LONG		g_tempFontSmoothing;

COLORREF g_rgbAutoColourList[TXC_MAX_COLOURS] = 
{
	SYSCOL(COLOR_WINDOWTEXT),							// foreground
	SYSCOL(COLOR_WINDOW),								// background
	SYSCOL(COLOR_HIGHLIGHTTEXT),						// selected text
	SYSCOL(COLOR_HIGHLIGHT),							// selection
	SYSCOL(COLOR_WINDOWTEXT),							// inactive selected text
	SYSCOL(COLOR_3DFACE),								// inactive selection
	SYSCOL(COLOR_3DFACE),								// selection margin#1
	SYSCOL(COLOR_3DHIGHLIGHT),							// selection margin#2
	MIXED_SYSCOL(COLOR_3DSHADOW, COLOR_3DDKSHADOW),		// line number text
	MIXED_SYSCOL2(COLOR_3DFACE, COLOR_WINDOW),			// line number bg
	SYSCOL(COLOR_WINDOWTEXT),							// long line text
	MIXED_SYSCOL(COLOR_3DFACE, COLOR_WINDOW),			// long-line background
	SYSCOL(COLOR_WINDOWTEXT),							// current line text
	RGB(230,240,255),									// current line background
};


#ifndef ODS_NOFOCUSRECT
#define ODS_NOFOCUSRECT     0x0200
#endif

#define COLORECT_WIDTH  32
#define COLORECT_OFFSET 4

#define NUM_DEFAULT_COLOURS 17

struct _CUSTCOL
{
	COLORREF cr;
	TCHAR *szName;

} CUSTCOL[NUM_DEFAULT_COLOURS] = 
{
	{ RGB(255,255,255),	_T("Automatic") },
	{ RGB(0,0,0),		_T("Black") },
	{ RGB(255,255,255),	_T("White") },
	{ RGB(128, 0, 0),	_T("Maroon") },
	{ RGB(0, 128,0),	_T("Dark Green") },
	{ RGB(128,128,0),	_T("Olive") },
	{ RGB(0,0,128),		_T("Dark Blue") },
	{ RGB(128,0,128),	_T("Purple") },
	{ RGB(0,128,128),	_T("Aquamarine") },
	{ RGB(196,196,196),	_T("Light Grey") },
	{ RGB(128,128,128),	_T("Dark Grey") },
	{ RGB(255,0,0),		_T("Red") },
	{ RGB(0,255,0),		_T("Green") },
	{ RGB(255,255,0),	_T("Yellow") },
	{ RGB(0,0,255),		_T("Blue") },
	{ RGB(255,0,255),	_T("Magenta") },
	{ RGB(0,255,255),	_T("Cyan") },
//	{ RGB(255,255,255),	"Custom..." },
};

//
//	Convert from logical-units to point-sizes
//
int LogicalToPoints(int nLogicalSize)
{
	HDC hdc      = GetDC(0);
	int size     = MulDiv(nLogicalSize, 72, GetDeviceCaps(hdc, LOGPIXELSY));

	ReleaseDC(0, hdc);

	return size;
}

DWORD GetCurrentComboData(HWND hwnd, UINT uCtrl)
{
	int idx = SendDlgItemMessage(hwnd, uCtrl, CB_GETCURSEL, 0, 0);
	return    SendDlgItemMessage(hwnd, uCtrl, CB_GETITEMDATA, idx == -1 ? 0 : idx, 0);
}

DWORD GetCurrentListData(HWND hwnd, UINT uCtrl)
{
	int idx = SendDlgItemMessage(hwnd, uCtrl, LB_GETCURSEL, 0, 0);
	return    SendDlgItemMessage(hwnd, uCtrl, LB_GETITEMDATA, idx == -1 ? 0 : idx, 0);
}

DWORD AddComboStringWithData(HWND hwnd, UINT uCtrl, TCHAR *szText, DWORD data)
{
	int idx = SendDlgItemMessage(hwnd, uCtrl, CB_ADDSTRING, 0, (LONG)szText);
	return    SendDlgItemMessage(hwnd, uCtrl, CB_SETITEMDATA, idx, data);
}

//
//	Font-enumeration callback
//
int CALLBACK EnumFontNames(ENUMLOGFONTEX *lpelfe, NEWTEXTMETRICEX *lpntme, DWORD FontType, LPARAM lParam)
{
	HWND hwndCombo = (HWND)lParam;
	TCHAR *pszName  = lpelfe->elfLogFont.lfFaceName;

		if(pszName[0] == '@')
			return 1;
	
	// make sure font doesn't already exist in our list
	if(SendMessage(hwndCombo, CB_FINDSTRING, 0, (LPARAM)pszName) == CB_ERR)
	{
		int		idx;
		BOOL	fFixed;
		int		fTrueType;		// 0 = normal, 1 = TrueType, 2 = OpenType

		// add the font
		idx = SendMessage(hwndCombo, CB_ADDSTRING, 0, (LPARAM)pszName);

		// record the font's attributes (Fixedwidth and Truetype)
		fFixed		= (lpelfe->elfLogFont.lfPitchAndFamily & FIXED_PITCH) ? TRUE : FALSE;
		fTrueType	= (lpelfe->elfLogFont.lfOutPrecision == OUT_STROKE_PRECIS) ? TRUE : FALSE;
		fTrueType	= (lpntme->ntmTm.ntmFlags & NTM_TT_OPENTYPE) ? 2 : fTrueType;
			
		// store this information in the list-item's userdata area
		SendMessage(hwndCombo, CB_SETITEMDATA, idx, MAKEWPARAM(fFixed, fTrueType));
	}

	return 1;
}

//
//	Initialize the font-list by enumeration all system fonts
//
void FillFontComboList(HWND hwndCombo)
{
	HDC		hdc = GetDC(hwndCombo);
	LOGFONT lf;
	int		idx;

	SendMessage(hwndCombo, WM_SETFONT, (WPARAM)g_hNormalFont, 0);

	lf.lfCharSet			= ANSI_CHARSET;	// DEFAULT_CHARSET;
	lf.lfFaceName[0]		= '\0';			// all fonts
	lf.lfPitchAndFamily		= 0;

	// store the list of fonts in the combo
	EnumFontFamiliesEx(hdc, &lf, (FONTENUMPROC)EnumFontNames, (LPARAM)hwndCombo, 0);

	ReleaseDC(hwndCombo, hdc);

	// select current font in list
	idx = SendMessage(hwndCombo, CB_FINDSTRING, 0, (LPARAM)g_szFontName);
	SendMessage(hwndCombo, CB_SETCURSEL, idx, 0);
}

/*
//
//	Could be used to fill a ListView with a list
//	of file-types and their associated icons
//
void FillFileTypeList(HWND hwnd)
{
	LVITEM lvi = { LVIF_TEXT|LVIF_IMAGE };
	SHFILEINFO shfi = { 0 };
	HIMAGELIST hImgList;
	int i;

	char *lookup[] = 
	{
		".asm", ".c", ".cpp", ".cs", ".js", 0
	};

	for(i = 0; lookup[i]; i++)
	{
		ZeroMemory(&shfi, sizeof(shfi));
		hImgList = (HIMAGELIST)SHGetFileInfo(lookup[i], FILE_ATTRIBUTE_NORMAL, &shfi, sizeof(shfi),  
			SHGFI_TYPENAME|SHGFI_ICON |SHGFI_USEFILEATTRIBUTES|SHGFI_SMALLICON|SHGFI_SYSICONINDEX);

		ListView_SetImageList(hwnd, hImgList, LVSIL_SMALL);

		lvi.iItem		= 0;
		lvi.iSubItem	= 0;
		lvi.pszText		= shfi.szTypeName;//lookup[i];
		lvi.iImage		= shfi.iIcon;
	
		ListView_InsertItem(hwnd, &lvi);
	}
}*/

void SetComboItemHeight(HWND hwndCombo, int nMinHeight)
{
	TEXTMETRIC	tm;
	HDC			hdc	 = GetDC(hwndCombo);
	HANDLE		hold = SelectObject(hdc, g_hNormalFont);
	
	// item height must fit the font+smallicon height
	GetTextMetrics(hdc, &tm);
	nMinHeight = max(tm.tmHeight, nMinHeight);

	SelectObject(hdc, hold);
	ReleaseDC(hwndCombo, hdc);

	SendMessage(hwndCombo, CB_SETITEMHEIGHT, -1, nMinHeight);
	SendMessage(hwndCombo, CB_SETITEMHEIGHT, 0, nMinHeight);
}

//
//	Draw a colour rectangle with border
//
void PaintFrameRect(HDC hdc, RECT *rect, COLORREF border, COLORREF fill)
{
	HBRUSH   hbrFill	= CreateSolidBrush(fill);
	HBRUSH   hbrBorder	= CreateSolidBrush(border);

	FrameRect(hdc, rect, hbrBorder);
	InflateRect(rect, -1, -1);
	FillRect(hdc, rect,  hbrFill);
	InflateRect(rect, 1, 1);

	DeleteObject(hbrFill);
	DeleteObject(hbrBorder);
}

void DrawItem_DefaultColours(DRAWITEMSTRUCT *dis)
{
	if(dis->itemState & ODS_DISABLED)
	{
		SetTextColor(dis->hDC, GetSysColor(COLOR_3DSHADOW));
		SetBkColor(dis->hDC,   GetSysColor(COLOR_3DFACE));
	}
	else
	{
		if((dis->itemState & ODS_SELECTED))
		{
			SetTextColor(dis->hDC,  GetSysColor(COLOR_HIGHLIGHTTEXT));
			SetBkColor(dis->hDC,	GetSysColor(COLOR_HIGHLIGHT));
		}
		else
		{
			SetTextColor(dis->hDC, GetSysColor(COLOR_WINDOWTEXT));
			SetBkColor(dis->hDC,   GetSysColor(COLOR_WINDOW));
		}
	}
}

//
//	Fontlist owner-draw 
//
BOOL FontCombo_DrawItem(HWND hwnd, DRAWITEMSTRUCT *dis)
{
	TCHAR		szText[100];
	
	BOOL		fFixed		= LOWORD(dis->itemData);
	BOOL		fTrueType	= HIWORD(dis->itemData);

	TEXTMETRIC	tm;
	int			xpos, ypos;
	HANDLE		hOldFont;

	if(dis->itemAction & ODA_FOCUS && !(dis->itemState & ODS_NOFOCUSRECT))
	{
		DrawFocusRect(dis->hDC, &dis->rcItem);
		return TRUE;
	}

	/*{
		HTHEME hTheme = 	OpenThemeData(hwnd, L"combobox");
		RECT rc;
		HDC hdc=GetDC(GetParent(hwnd));
		CopyRect(&rc, &dis->rcItem);
		InflateRect(&rc, 3, 3);
		//GetClientRect(hwnd, &rc);
		//rc.bottom = rc.top + 22;

		//DrawThemeBackground(
		//	hTheme, 
		//	dis->hDC, 
		//	4,//CP_DROPDOWNBUTTON, 
		//	CBXS_HOT,//CBXS_NORMAL, 
		//	&rc, 
		//	&rc);

		CloseThemeData(hTheme);
		ReleaseDC(GetParent(hwnd),hdc);
		return TRUE;
	}*/

	//
	//	Get the item text
	//
	if(dis->itemID == -1)
		SendMessage(dis->hwndItem, WM_GETTEXT, 0, (LONG)szText);
	else
		SendMessage(dis->hwndItem, CB_GETLBTEXT, dis->itemID, (LONG)szText);
	
	//
	//	Set text colour and background based on current state
	//
	DrawItem_DefaultColours(dis);

	// set the font: BOLD for fixed-width, NORMAL for 'normal'
	hOldFont = SelectObject(dis->hDC, fFixed ? g_hBoldFont : g_hNormalFont);
	GetTextMetrics(dis->hDC, &tm);

	ypos = dis->rcItem.top  + (dis->rcItem.bottom-dis->rcItem.top-tm.tmHeight)/2;
	xpos = dis->rcItem.left + 20;
	
	// draw the text
	ExtTextOut(dis->hDC, xpos, ypos,
		ETO_CLIPPED|ETO_OPAQUE, &dis->rcItem, szText, _tcslen(szText), 0);

	// draw a 'TT' icon if the font is TRUETYPE
	if(fTrueType)
		DrawIconEx(dis->hDC, dis->rcItem.left+2, dis->rcItem.top, g_hIcon2,16, 16, 0, 0, DI_NORMAL);
	//else if(fTrueType == 2)
	//	DrawIconEx(dis->hDC, dis->rcItem.left+2, dis->rcItem.top, g_hIcon3,16, 16, 0, 0, DI_NORMAL);

	SelectObject(dis->hDC, hOldFont);

	// draw the focus rectangle
	if((dis->itemState & ODS_FOCUS) && !(dis->itemState & ODS_NOFOCUSRECT))
	{
		DrawFocusRect(dis->hDC, &dis->rcItem);
	}

	return TRUE;
}

//
//	Combobox must have the CBS_HASSTRINGS style set!!
//	
BOOL ColourCombo_DrawItem(HWND hwnd, UINT uCtrlId, DRAWITEMSTRUCT *dis, BOOL fSelectImage)
{
	RECT		rect	= dis->rcItem;
	int			boxsize = (dis->rcItem.bottom - dis->rcItem.top) - 4;			
	int			xpos;
	int			ypos;
	TEXTMETRIC	tm;
	TCHAR		szText[80];
	HANDLE		hOldFont;
	
	if(!fSelectImage)
		rect.left += boxsize + 4;

	if(dis->itemAction & ODA_FOCUS && !(dis->itemState & ODS_NOFOCUSRECT))
	{
		DrawFocusRect(dis->hDC, &rect);
		return TRUE;
	}
	
	//
	//	Get the item text
	//
	if(dis->itemID == -1)
		SendMessage(dis->hwndItem, WM_GETTEXT, 0, (LONG)szText);
	else
		SendMessage(dis->hwndItem, CB_GETLBTEXT, dis->itemID, (LONG)szText);
	
	//
	//	Set text colour and background based on current state
	//
	DrawItem_DefaultColours(dis);
	
	//
	//	Draw the text (centered vertically)
	//	
	hOldFont = SelectObject(dis->hDC, g_hNormalFont);

	GetTextMetrics(dis->hDC, &tm);
	ypos = dis->rcItem.top  + (dis->rcItem.bottom - dis->rcItem.top - tm.tmHeight) / 2;
	xpos = dis->rcItem.left + boxsize + 4 + 4;
	
	ExtTextOut(dis->hDC, xpos, ypos, 
		ETO_CLIPPED|ETO_OPAQUE, &rect, szText, lstrlen(szText), 0);
	
	if((dis->itemState & ODS_FOCUS) && !(dis->itemState & ODS_NOFOCUSRECT))
	{
		DrawFocusRect(dis->hDC, &rect);
	}
	
	// 
	//	Paint the colour rectangle
	//	
	rect = dis->rcItem;
	InflateRect(&rect, -2, -2);
	rect.right = rect.left + boxsize;
	
	if(dis->itemState & ODS_DISABLED)
		PaintFrameRect(dis->hDC, &rect, GetSysColor(COLOR_3DSHADOW), GetSysColor(COLOR_3DFACE));
	else
		PaintFrameRect(dis->hDC, &rect, RGB(0,0,0), REALIZE_SYSCOL(dis->itemData));
	
	
	return TRUE;
}

int CALLBACK EnumFontSizes(ENUMLOGFONTEX *lpelfe, NEWTEXTMETRICEX *lpntme, DWORD FontType, LPARAM lParam)
{
	static int ttsizes[] = { 8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 36, 48, 72 };
	TCHAR ach[100];

	BOOL fTrueType = (lpelfe->elfLogFont.lfOutPrecision == OUT_STROKE_PRECIS) ? TRUE : FALSE;

	HWND hwndCombo = (HWND)lParam;
	int  i, count, idx;

	if(fTrueType)
	{
		for(i = 0; i < (sizeof(ttsizes) / sizeof(ttsizes[0])); i++)
		{
			_stprintf(ach, _T("%d"), ttsizes[i]);
			idx = SendMessage(hwndCombo, CB_ADDSTRING, 0, (LPARAM)ach);
			SendMessage(hwndCombo, CB_SETITEMDATA, idx, ttsizes[i]);
			//nFontSizes[i] = ttsizes[i];
		}
		//nNumFontSizes = i;
		return 0;
	}
	else
	{
		int size = LogicalToPoints(lpntme->ntmTm.tmHeight);
		_stprintf(ach, _T("%d"), size);

		count = SendMessage(hwndCombo, CB_GETCOUNT, 0, 0);

		for(i = 0; i < count; i++)
		{
			if(SendMessage(hwndCombo, CB_GETITEMDATA, 0, 0) == size)
				break;
		}
		
		if(i >= count)
		{
			idx = SendMessage(hwndCombo, CB_ADDSTRING, 0, (LPARAM)ach);
			SendMessage(hwndCombo, CB_SETITEMDATA, idx, size);
		}

		return 1;	
	}

	return 1;
}


void InitSizeList(HWND hwnd)
{
	LOGFONT lf = { 0 };
	HDC hdc = GetDC(hwnd);
	
	// get current font size
	int cursize = GetDlgItemInt(hwnd, IDC_SIZELIST, 0, 0);
		
	int item   = SendDlgItemMessage(hwnd, IDC_FONTLIST, CB_GETCURSEL, 0, 0);
	int i, count, nearest = 0;

	lf.lfCharSet		= DEFAULT_CHARSET;
	lf.lfPitchAndFamily = 0;
	SendDlgItemMessage(hwnd, IDC_FONTLIST, CB_GETLBTEXT, item, (LPARAM)lf.lfFaceName);

	// empty list
	SendDlgItemMessage(hwnd, IDC_SIZELIST, CB_RESETCONTENT, 0, 0);

	// enumerate font sizes
	EnumFontFamiliesEx(hdc, &lf, (FONTENUMPROC)EnumFontSizes, (LONG)GetDlgItem(hwnd, IDC_SIZELIST), 0);

	// set selection to first item
	count = SendDlgItemMessage(hwnd, IDC_SIZELIST, CB_GETCOUNT, 0, 0);
	
	for(i = 0; i < count; i++)
	{
		int n = SendDlgItemMessage(hwnd, IDC_SIZELIST, CB_GETITEMDATA, i, 0);

		if(n <= cursize) 
			nearest = i;
	}

	SendDlgItemMessage(hwnd, IDC_SIZELIST, CB_SETCURSEL, nearest, 0);

	ReleaseDC(hwnd, hdc);
}

static WNDPROC  oldPreviewProc;
static HFONT	g_hPreviewFont;

LONG CALLBACK PreviewWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	RECT		rect;
	PAINTSTRUCT ps;
	HANDLE		hold;

	switch(msg)
	{
	case WM_ERASEBKGND:
		return 1;

	case WM_PAINT:
		BeginPaint(hwnd, &ps);
		
		GetClientRect(hwnd, &rect);

		FrameRect(ps.hdc, &rect, GetSysColorBrush(COLOR_3DSHADOW));
		InflateRect(&rect, -1, -1);

		SetTextColor(ps.hdc, g_crPreviewFG);
		SetBkColor(ps.hdc, g_crPreviewBG);

		ExtTextOut(ps.hdc, 0, 0, ETO_OPAQUE, &rect, 0, 0, 0);
		hold = SelectObject(ps.hdc, g_hPreviewFont);

		DrawText(ps.hdc, _T("Sample Text"), -1, &rect, DT_SINGLELINE|DT_CENTER|DT_VCENTER);
		
		SelectObject(ps.hdc, hold);
		EndPaint(hwnd, &ps);
		return 0;
	}

	return CallWindowProc(oldPreviewProc, hwnd, msg, wParam, lParam);
}

void AddColourListItem(HWND hwnd, UINT uItem, int fgIdx, int bgIdx, TCHAR *szName)
{
	HWND hwndCtrl = GetDlgItem(hwnd, uItem);
	int idx = SendMessage(hwndCtrl, LB_ADDSTRING, 0, (LONG)szName);
	SendMessage(hwndCtrl, LB_SETITEMDATA, idx, MAKELONG(fgIdx, bgIdx));
}

void AddColourComboItem(HWND hwnd, UINT uItem, COLORREF col, TCHAR *szName)
{
	HWND hwndCtrl = GetDlgItem(hwnd, uItem);
	int idx = SendMessage(hwndCtrl, CB_ADDSTRING, 0, (LONG)szName);
	SendMessage(hwndCtrl, CB_SETITEMDATA, idx, col);
}

void UpdatePreviewPane(HWND hwnd)
{
	TCHAR szFaceName[200];
	int idx = SendDlgItemMessage(hwnd, IDC_FONTLIST, CB_GETCURSEL, 0, 0);
	int size;
	DWORD data;

	SendDlgItemMessage(hwnd, IDC_FONTLIST, CB_GETLBTEXT, idx, (LONG)szFaceName);

	size = GetDlgItemInt(hwnd, IDC_SIZELIST, 0, FALSE);

	if(g_hPreviewFont != 0)
		DeleteObject(g_hPreviewFont);

	g_hPreviewFont = EasyCreateFont(size, 
								IsDlgButtonChecked(hwnd, IDC_BOLD),
								g_tempFontSmoothing,
								szFaceName);

	idx  = SendDlgItemMessage(hwnd, IDC_ITEMLIST, LB_GETCURSEL, 0, 0);
	data = SendDlgItemMessage(hwnd, IDC_ITEMLIST, LB_GETITEMDATA, idx, 0);


	if((short)LOWORD(data) >= 0)
		g_crPreviewFG = REALIZE_SYSCOL(g_rgbTempColourList[LOWORD(data)]);
	else
		g_crPreviewFG = GetSysColor(COLOR_WINDOWTEXT);

	if((short)HIWORD(data) >= 0)
		g_crPreviewBG = REALIZE_SYSCOL(g_rgbTempColourList[HIWORD(data)]);
	else
		g_crPreviewBG = GetSysColor(COLOR_WINDOW);

	InvalidateRect(GetDlgItem(hwnd, IDC_PREVIEW), 0, TRUE); 
}

BOOL PickColour(HWND hwndParent, COLORREF *col, COLORREF *custCol)
{
	CHOOSECOLOR cc = { sizeof(cc) };
	COLORREF    custTmp[16];

	memcpy(custTmp, custCol, sizeof(custTmp));

	cc.Flags			= CC_ANYCOLOR|CC_FULLOPEN|CC_SOLIDCOLOR|CC_RGBINIT;
	cc.hwndOwner		= hwndParent;
	cc.lpCustColors		= custTmp;
	cc.rgbResult		= *col;

	if(ChooseColor(&cc))
	{
		*col = cc.rgbResult;
		memcpy(custCol, custTmp, sizeof(custTmp));
		return TRUE;
	}
	else
	{
		return FALSE;
	}
}

//
//
//
void SelectColorInList(HWND hwnd, UINT uComboIdx, short itemIdx)
{
	HWND hwndCombo = GetDlgItem(hwnd, uComboIdx);
	int  i;

	if(itemIdx == (short)-1)
	{
		SendMessage(hwndCombo, CB_SETCURSEL, 0, 0);
		EnableWindow(hwndCombo, FALSE);
		EnableWindow(GetWindow(hwndCombo, GW_HWNDNEXT), FALSE);
		return;
	}
	else
	{
		EnableWindow(hwndCombo, TRUE);
		EnableWindow(GetWindow(hwndCombo, GW_HWNDNEXT), TRUE);
	}

	if(itemIdx < 0 || itemIdx >= TXC_MAX_COLOURS)
		return;

	// update the Auto item
	SendMessage(hwndCombo, CB_SETITEMDATA, 0, g_rgbAutoColourList[itemIdx]);

	// remove the custom entry (if any)
	SendMessage(hwndCombo, CB_DELETESTRING, NUM_DEFAULT_COLOURS, 0);

	// if an "AUTO" colour
	if((g_rgbTempColourList[itemIdx] & 0x80000000) ||
		g_rgbTempColourList[itemIdx] == g_rgbAutoColourList[itemIdx])
	{
		SendMessage(hwndCombo, CB_SETCURSEL, 0, 0);
	}
	// normal colour
	else
	{
		// try to match current colour with a default colour
		for(i = 1; i < NUM_DEFAULT_COLOURS; i++)
		{
			if(g_rgbTempColourList[itemIdx] == CUSTCOL[i].cr)
			{
				SendMessage(hwndCombo, CB_SETCURSEL, i, 0);
				break;
			}
		}
		
		// if we didn't match the colour, add it as a custom entry
		if(i == NUM_DEFAULT_COLOURS)
		{
			i = SendMessage(hwndCombo, CB_ADDSTRING, 0, (LONG)_T("Custom"));
			SendMessage(hwndCombo, CB_SETITEMDATA, i, g_rgbTempColourList[itemIdx]);
			SendMessage(hwndCombo, CB_SETCURSEL, i, 0);
		}
	}
}

BOOL InitFontOptionsDlg(HWND hwnd)
{
	HWND	hwndPreview;
	int		i;
	LOGFONT lf;
	HFONT   hDlgFont;
	TCHAR	ach[LF_FACESIZE];

	// make a temporary copy of the current colour settings
	memcpy(g_rgbTempColourList, g_rgbColourList, sizeof(COLORREF) * TXC_MAX_COLOURS);

	//
	//	Load the TrueType icon for the font-list
	//
	g_hIcon2 = LoadImage(GetModuleHandle(0), MAKEINTRESOURCE(IDI_ICON2), IMAGE_ICON, 16, 16, 0);
	g_hIcon3 = LoadImage(GetModuleHandle(0), MAKEINTRESOURCE(IDI_ICON3), IMAGE_ICON, 16, 16, 0);
	
	//
	//	Create two fonts (normal+bold) based on current dialog's font settings
	//
	hDlgFont = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
	GetObject(hDlgFont, sizeof(lf), &lf);
		
	g_hNormalFont = CreateFontIndirect(&lf);
	lf.lfWeight   = FW_BOLD;
	g_hBoldFont   = CreateFontIndirect(&lf);

	//
	//	Manually set the COMBO item-heights because WM_MEASUREITEM has already
	//  been sent and we missed it..
	//
	SetComboItemHeight(GetDlgItem(hwnd, IDC_FGCOLCOMBO), 14);
	SetComboItemHeight(GetDlgItem(hwnd, IDC_BGCOLCOMBO), 14);
	SetComboItemHeight(GetDlgItem(hwnd, IDC_FONTLIST), 16);
	
	g_tempFontSmoothing = g_nFontSmoothing;
	g_hPreviewFont = EasyCreateFont(g_nFontSize, g_fFontBold, g_tempFontSmoothing, g_szFontName);

	// Create the list of fonts
	FillFontComboList(GetDlgItem(hwnd, IDC_FONTLIST));

	// Update the font-size-list
	InitSizeList(hwnd);
	
	//
	//	Subclass the PREVIEW static control so we can custom-draw it
	//
	hwndPreview = GetDlgItem(hwnd, IDC_PREVIEW);
	oldPreviewProc = (WNDPROC)SetWindowLongPtr(hwndPreview, GWLP_WNDPROC, (LONG)PreviewWndProc);
	
//	g_rgbAutoColourList[TXC_LONGLINE]	= MixRGB(GetSysColor(COLOR_3DFACE), GetSysColor(COLOR_WINDOW));
//	g_rgbAutoColourList[TXC_LINENUMBER]	= MixRGB(GetSysColor(COLOR_3DFACE), GetSysColor(COLOR_WINDOW));
//	g_rgbAutoColourList[TXC_LINENUMBER] = MixRGB(g_rgbAutoColourList[TXC_LINENUMBER], GetSysColor(COLOR_WINDOW));

	//g_rgbAutoColourList[TXC_LONGLINE]	= MixRGB(GetSysColor(COLOR_3DFACE), GetSysColor(COLOR_WINDOW));
	//g_rgbAutoColourList[TXC_LINENUMBER]	= MixRGB(GetSysColor(COLOR_3DFACE), GetSysColor(COLOR_WINDOW));
	//g_rgbAutoColourList[TXC_LINENUMBER] = MixRGB(g_rgbAutoColourList[TXC_LINENUMBER], GetSysColor(COLOR_WINDOW));

	AddColourListItem(hwnd, IDC_LIST1, TXC_FOREGROUND,		TXC_BACKGROUND,   _T("Text"));
	AddColourListItem(hwnd, IDC_LIST1, TXC_HIGHLIGHTTEXT,	TXC_HIGHLIGHT,    _T("Selected Text"));
	AddColourListItem(hwnd, IDC_LIST1, TXC_HIGHLIGHTTEXT2,  TXC_HIGHLIGHT2,   _T("Inactive Selection"));
	AddColourListItem(hwnd, IDC_LIST1, TXC_SELMARGIN1,		TXC_SELMARGIN2,   _T("Left Margin"));
	AddColourListItem(hwnd, IDC_LIST1, TXC_LINENUMBERTEXT,  TXC_LINENUMBER,   _T("Line Numbers"));
	AddColourListItem(hwnd, IDC_LIST1, -1,					TXC_LONGLINE,	  _T("Long Lines"));
	AddColourListItem(hwnd, IDC_LIST1, TXC_CURRENTLINETEXT, TXC_CURRENTLINE,  _T("Current Line"));
	
	SendDlgItemMessage(hwnd, IDC_ITEMLIST, LB_SETCURSEL, 0, 0);
	PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(IDC_ITEMLIST, LBN_SELCHANGE), (LPARAM)GetDlgItem(hwnd, IDC_ITEMLIST));

	for(i = 0; i < NUM_DEFAULT_COLOURS; i++)
	{
		AddColourComboItem(hwnd, IDC_FGCOLCOMBO, CUSTCOL[i].cr, CUSTCOL[i].szName);
		AddColourComboItem(hwnd, IDC_BGCOLCOMBO, CUSTCOL[i].cr, CUSTCOL[i].szName);
	}
	
	SendDlgItemMessage(hwnd, IDC_FGCOLCOMBO, CB_SETCURSEL, 1, 0);
	SendDlgItemMessage(hwnd, IDC_BGCOLCOMBO, CB_SETCURSEL, 0, 0);
	
	SendDlgItemMessage(hwnd, IDC_SPIN1, UDM_SETRANGE, 0, MAKELONG(10,0));
	SendDlgItemMessage(hwnd, IDC_SPIN2, UDM_SETRANGE, 0, MAKELONG(10,0));

	//
	//	Select
	//
	_stprintf(ach, _T("%d"), g_nFontSize);

	SendDlgItemMessage(hwnd, IDC_SIZELIST, CB_SELECTSTRING, -1, (LONG)ach);
	SendDlgItemMessage(hwnd, IDC_FONTLIST, CB_SELECTSTRING, -1, (LONG)g_szFontName);

	SetDlgItemInt(hwnd, IDC_PADDINGA, g_nPaddingAbove, 0);
	SetDlgItemInt(hwnd, IDC_PADDINGB, g_nPaddingBelow, 0);

	if((g_fPaddingFlags & COURIERNEW) && lstrcmpi(g_szFontName, _T("Courier New")) == 0)
	{
		SetDlgItemInt(hwnd, IDC_PADDINGA, g_nPaddingAbove, 0);
		SetDlgItemInt(hwnd, IDC_PADDINGB, g_nPaddingBelow, 1);
	}

	if((g_fPaddingFlags & LUCIDACONS) && lstrcmpi(g_szFontName, _T("Lucida Console")) == 0)
	{
		//SetDlgItemInt(hwnd, IDC_PADDINGA, g_nPaddingAbove, 2);
		//SetDlgItemInt(hwnd, IDC_PADDINGB, g_nPaddingBelow, 1);
		//SendDlgItemMessage(hwnd, IDC_
	}

	CheckDlgButton(hwnd, IDC_BOLD,    g_fFontBold);

	UpdatePreviewPane(hwnd);

	return TRUE;
}

BOOL CALLBACK AdvancedDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	char lookup[] = 
	{ 
		1,		// DEFAULT_QUALITY
		1,		// ??
		1,		// ??
		0,		// NONANTIALIASED_QUALITY
		2,		// ANTIALIASED_QUALITY
		3,		// CLEARTYPE_QUALITY 
	};

	switch(msg)
	{
	case WM_INITDIALOG:
		
		AddComboStringWithData(hwnd, IDC_COMBO1, _T("None"),		NONANTIALIASED_QUALITY);
		AddComboStringWithData(hwnd, IDC_COMBO1, _T("Default"),		DEFAULT_QUALITY);
		AddComboStringWithData(hwnd, IDC_COMBO1, _T("Antialiased"), ANTIALIASED_QUALITY);
		AddComboStringWithData(hwnd, IDC_COMBO1, _T("ClearType"),	CLEARTYPE_QUALITY);

		SendDlgItemMessage(hwnd, IDC_COMBO1, CB_SETCURSEL, lookup[g_tempFontSmoothing], 0);

		return TRUE;

	case WM_CLOSE:
		EndDialog(hwnd, FALSE);
		return TRUE;

	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case IDCANCEL:
			EndDialog(hwnd, FALSE);
			return TRUE;

		case IDOK:
			g_tempFontSmoothing = GetCurrentComboData(hwnd, IDC_COMBO1);
			EndDialog(hwnd, TRUE);
			return TRUE;
		}

		return FALSE;
	}

	return FALSE;
}

//BOOL FontOptions(HWND hwnd, WPARAM wParam, 
//
//	Dialogbox procedure for the FONT pane
//
BOOL CALLBACK FontOptionsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	PSHNOTIFY *pshn;

	switch(msg)
	{
	case WM_INITDIALOG:
		CenterWindow(GetParent(hwnd));
		return InitFontOptionsDlg(hwnd);

	case MSG_UPDATE_PREVIEW:
		UpdatePreviewPane(hwnd);
		return TRUE;

	case WM_DESTROY:
		DeleteObject(g_hNormalFont);
		DeleteObject(g_hBoldFont);
		DeleteObject(g_hPreviewFont);
		return TRUE;

	case WM_MEASUREITEM:
		// can't do anything here because we haven't created
		// the fonts yet, so send a manual CB_SETITEMHEIGHT instead
		return FALSE;

	case WM_DRAWITEM:

		if(wParam == IDC_FONTLIST)
			return FontCombo_DrawItem(hwnd, (DRAWITEMSTRUCT *)lParam);

		else if(wParam == IDC_FGCOLCOMBO || wParam == IDC_BGCOLCOMBO)
			return ColourCombo_DrawItem(hwnd, wParam, (DRAWITEMSTRUCT *)lParam, FALSE);

		return FALSE;

	case WM_NOTIFY:

		pshn = (PSHNOTIFY *)lParam;

		if(pshn->hdr.code == NM_CUSTOMDRAW)
		{
			if(pshn->hdr.idFrom == IDC_FONTLIST)
			{
				return FALSE;
			}
			return FALSE;
		}		
		else if(pshn->hdr.code == PSN_APPLY)
		{
			g_nFontSize = GetDlgItemInt(hwnd, IDC_SIZELIST, 0, 0);
			g_fFontBold = IsDlgButtonChecked(hwnd, IDC_BOLD);

			GetDlgItemText(hwnd, IDC_FONTLIST, g_szFontName, LF_FACESIZE);	

			g_nPaddingAbove = GetDlgItemInt(hwnd, IDC_PADDINGA, 0, 0);
			g_nPaddingBelow = GetDlgItemInt(hwnd, IDC_PADDINGB, 0, 0);

			memcpy(g_rgbColourList, g_rgbTempColourList, sizeof(COLORREF) * TXC_MAX_COLOURS);

			g_nFontSmoothing = g_tempFontSmoothing;
			
			return TRUE;
		}

		return FALSE;

	case WM_COMMAND:

		switch(LOWORD(wParam))
		{
		case IDC_ADVANCED:
	
			if(DialogBoxParam(g_hResourceModule, MAKEINTRESOURCE(IDD_FONTEXTRA), hwnd, AdvancedDlgProc, 0))
			{
				UpdatePreviewPane(hwnd);
			}

			return TRUE;

		case IDCANCEL:
			return TRUE;

		case IDC_FONTLIST:
			if(HIWORD(wParam) == CBN_SELCHANGE)
			{
				InitSizeList(hwnd);
			}
				
			PostMessage(hwnd, MSG_UPDATE_PREVIEW, 0, 0);
			return TRUE;

		case IDC_ITEMLIST:

			if(HIWORD(wParam) == CBN_SELCHANGE)
			{
				DWORD itemidx = GetCurrentListData(hwnd, IDC_ITEMLIST);

				SelectColorInList(hwnd, IDC_FGCOLCOMBO, LOWORD(itemidx));
				SelectColorInList(hwnd, IDC_BGCOLCOMBO, HIWORD(itemidx));

				UpdatePreviewPane(hwnd);
			}

			return TRUE;

		case IDC_SIZELIST:
			
			if(HIWORD(wParam) == CBN_SELCHANGE || 
			   HIWORD(wParam) == CBN_EDITCHANGE)
			{
				PostMessage(hwnd, MSG_UPDATE_PREVIEW, 0, 0);
			}

			return TRUE;

		case IDC_FGCOLCOMBO:
		case IDC_BGCOLCOMBO:
			
			if(HIWORD(wParam) == CBN_SELCHANGE)
			{
				short fgidx = LOWORD(GetCurrentListData(hwnd, IDC_ITEMLIST));
				short bgidx = HIWORD(GetCurrentListData(hwnd, IDC_ITEMLIST));

				if(fgidx >= 0)
					g_rgbTempColourList[fgidx] = GetCurrentComboData(hwnd, IDC_FGCOLCOMBO);
				
				if(bgidx >= 0)
					g_rgbTempColourList[bgidx] = GetCurrentComboData(hwnd, IDC_BGCOLCOMBO);

				PostMessage(hwnd, MSG_UPDATE_PREVIEW, 0, 0);
			}

			return TRUE;
			
		case IDC_BOLD:
			PostMessage(hwnd, MSG_UPDATE_PREVIEW, 0, 0);
			return TRUE;

		case IDC_CUSTBUT1:
			{
				COLORREF col = 0;
				int idx = LOWORD(GetCurrentListData(hwnd, IDC_ITEMLIST));

				if(idx >= 0)
					col = REALIZE_SYSCOL(g_rgbTempColourList[idx]);
				
				if(PickColour(hwnd, &col, g_rgbCustColours))
				{
					g_rgbTempColourList[idx] = col;
					SelectColorInList(hwnd, IDC_FGCOLCOMBO, (short)idx);
					UpdatePreviewPane(hwnd);
				}
			}
			return TRUE;

		case IDC_CUSTBUT2:
			{
				COLORREF col = 0;
				int idx = HIWORD(GetCurrentListData(hwnd, IDC_ITEMLIST));
				
				if(idx >= 0)
					col = REALIZE_SYSCOL(g_rgbTempColourList[idx]);
				
				if(PickColour(hwnd, &col, g_rgbCustColours))
				{
					g_rgbTempColourList[idx] = col;
					SelectColorInList(hwnd, IDC_BGCOLCOMBO, (short)idx);
					UpdatePreviewPane(hwnd);
				}
			}
			return TRUE;
		}

		return FALSE;
	}

	return FALSE;
}
