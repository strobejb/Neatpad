//
//	Neatpad - Simple Text Editor application 
//
//	www.catch22.net
//	Written by J Brown 2004
//
//	Freeware
//
#define _CRT_SECURE_NO_DEPRECATE
#define _WIN32_WINNT 0x501
#define STRICT


#include <windows.h>
#include <tchar.h>
#include <commctrl.h>
#include <uxtheme.h>
#include "Neatpad.h"
#include "..\TextView\TextView.h"
#include "resource.h"

#if !defined(UNICODE)
#error "Please build as Unicode only!"
#endif

#pragma comment(lib, "uxtheme.lib")

TCHAR		g_szAppName[] = APP_TITLE;
HWND		g_hwndMain;
HWND		g_hwndTextView;
HWND		g_hwndStatusbar;
HWND		g_hwndSearchDlg;
HFONT		g_hFont;

TCHAR		g_szFileName[MAX_PATH];
TCHAR		g_szFileTitle[MAX_PATH];
BOOL		g_fFileChanged = FALSE;

TCHAR		*g_szEditMode[] = { _T("READ"), _T("INS"), _T("OVR") };

// support 'satellite' resource modules
HINSTANCE	g_hResourceModule;

#pragma comment(linker, "/OPT:NOWIN98")

//
//	Set the main window filename
//
void SetWindowFileName(HWND hwnd, TCHAR *szFileName, BOOL fModified)
{
	TCHAR ach[MAX_PATH + sizeof(g_szAppName) + 4];
	TCHAR mod[4] = _T("");

	if(fModified)
		lstrcpy(mod, _T(" *"));

	wsprintf(ach, _T("%s - %s%s"), szFileName, g_szAppName, mod);
	SetWindowText(hwnd, ach);
}

//
//	About dialog-proc
//
LRESULT CALLBACK AboutDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	HICON	hIcon;
	HFONT	hFont;
	RECT	rect;
	HWND	hwndUrl;
	HWND	hwndStatic;
	
	switch(msg)
	{
	case WM_INITDIALOG:

		SendMessage(hwnd, WM_SETFONT, (WPARAM)g_hFont, 0);

		CenterWindow(hwnd);

		//
		//	Set the dialog-icon 
		//
		hIcon = (HICON)LoadImage(GetModuleHandle(0), MAKEINTRESOURCE(IDI_ICON1), IMAGE_ICON, 48, 48, 0);
		SendDlgItemMessage(hwnd, IDC_HEADER2, STM_SETIMAGE, IMAGE_ICON, (WPARAM)hIcon);

		//
		//	Get the current font for the dialog and create a BOLD version,
		//	set this as the AppName static-label's font
		//
		hFont = CreateBoldFontFromHwnd(hwnd);
		SendDlgItemMessage(hwnd, IDC_ABOUT_APPNAME, WM_SETFONT, (WPARAM)hFont, 0);

		//
		//	Locate the existing static-control which displays our homepage
		//	Create a SysLink control right over the top of it (assuming current
		//	version of Windows supports it)
		//
		hwndStatic = GetDlgItem(hwnd, IDC_ABOUT_URL);
		GetClientRect(hwndStatic, &rect);
		MapWindowPoints(hwndStatic, hwnd, (POINT *)&rect, 2);

		hwndUrl = CreateWindow(WC_LINK, SYSLINK_STR, 
			WS_TABSTOP|WS_CHILD|WS_VISIBLE, 
			rect.left, rect.top, rect.right-rect.left, rect.bottom-rect.top,
			hwnd, 0, 0, 0
			);

		if(hwndUrl)
		{
			SendMessage(hwndUrl, WM_SETFONT, (WPARAM)hFont, 0);
			ShowWindow(hwndStatic, SW_HIDE);
		}

		return TRUE;

	case WM_NOTIFY:

		// Spawn the default web-browser when the SysLink control is clicked
		switch(((NMHDR *)lParam)->code)
		{
		case NM_CLICK: case NM_RETURN:
			ShellExecute(hwnd, _T("open"), WEBSITE_URL, 0, 0, SW_SHOWNORMAL);
			return 0;
		}

		break;

	case WM_CLOSE:

		EndDialog(hwnd, TRUE);
		return TRUE;

	case WM_COMMAND:

		if(LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
			EndDialog(hwnd, TRUE);

		break;
	}

	return FALSE;
}

//
//	Display the About dialog-box
//
void ShowAboutDlg(HWND hwndParent)
{
	DialogBoxParam(0, MAKEINTRESOURCE(IDD_ABOUT), hwndParent, AboutDlgProc, 0);
	//DialogBoxWithFont(0, MAKEINTRESOURCE(IDD_ABOUT), hwndParent, AboutDlgProc, 0, _T("MetaCondNormal-Roman"), 9);//_T("Bell MT Bold"));
}



//
//	WM_NOTIFY handler for the TextView notification messages
//
UINT TextViewNotifyHandler(HWND hwnd, NMHDR *nmhdr)
{
	switch(nmhdr->code)
	{
	// document has changed due to text input / undo / redo, update
	// the main window-title to show an asterisk next to the filename
	case TVN_CHANGED:

		if(g_szFileTitle[0])
		{
			BOOL fModified = TextView_CanUndo(g_hwndTextView);

			if(fModified != g_fFileChanged)
			{
				SetWindowFileName(hwnd, g_szFileTitle, fModified);
				g_fFileChanged = fModified;
			}
		}
		break;

	// cursor position has changed, update the statusbar info
	case TVN_CURSOR_CHANGE:

		SetStatusBarText(g_hwndStatusbar, 1, 0, _T(" Ln %d, Col %d"), 
			TextView_GetCurLine(g_hwndTextView) + 1, 
			TextView_GetCurCol(g_hwndTextView) + 1 );

		break;

	// edit/insert mode changed, update statusbar info
	case TVN_EDITMODE_CHANGE:

		SetStatusBarText(g_hwndStatusbar, 2, 0, 
			g_szEditMode[TextView_GetEditMode(g_hwndTextView)] );

		break;

	default:
		break;
	}	

	return 0;
}

//
//	Generic WM_NOTIFY handler for all other messages
//
UINT NotifyHandler(HWND hwnd, NMHDR *nmhdr)
{
	NMMOUSE *nmmouse;
	UINT	 nMode;

	switch(nmhdr->code)
	{
	case NM_DBLCLK:

		// statusbar is the only window at present which sends double-clicks
		nmmouse = (NMMOUSE *)nmhdr;

		// toggle the Readonly/Insert/Overwrite mode
		if(nmmouse->dwItemSpec == 2)
		{
			nMode   = TextView_GetEditMode(g_hwndTextView);
			nMode	= (nMode + 1) % 3;
	
			TextView_SetEditMode(g_hwndTextView, nMode);
		
			SetStatusBarText(g_hwndStatusbar, 2, 0, g_szEditMode[nMode]);
		}

		break;

	default:
		break;
	}	

	return 0;
}

//
//	WM_COMMAND message handler for main window
//
UINT CommandHandler(HWND hwnd, UINT nCtrlId, UINT nCtrlCode, HWND hwndFrom)
{
	RECT rect;

	switch(nCtrlId)
	{
	case IDM_FILE_NEW:
		
		// reset to an empty file
		SetWindowFileName(hwnd, _T("Untitled"), FALSE);
		TextView_Clear(g_hwndTextView);

		g_szFileTitle[0] = '\0';
		g_fFileChanged   = FALSE;
		return 0;
		
	case IDM_FILE_OPEN:
		
		// get a filename to open
		if(ShowOpenFileDlg(hwnd, g_szFileName, g_szFileTitle))
		{
			DoOpenFile(hwnd, g_szFileName, g_szFileTitle);
		}
		
		return 0;

	case IDM_FILE_SAVE:
		MessageBox(hwnd, _T("Not implemented"), APP_TITLE, MB_ICONINFORMATION);
		return 0;

	case IDM_FILE_SAVEAS:

		// does nothing yet
		if(ShowSaveFileDlg(hwnd, g_szFileName, g_szFileTitle))
		{
			MessageBox(hwnd, _T("Not implemented"), APP_TITLE, MB_ICONINFORMATION);
		}

		return 0;
		
	case IDM_FILE_PRINT:
		
		DeleteDC(
			ShowPrintDlg(hwnd)
			);
		
		return 0;

	case IDM_FILE_EXIT:
		PostMessage(hwnd, WM_CLOSE, 0, 0);
		return 0;

	case IDM_EDIT_UNDO:	case WM_UNDO:
		SendMessage(g_hwndTextView, WM_UNDO, 0, 0);
		return 0;
		
	case IDM_EDIT_REDO:
		SendMessage(g_hwndTextView, TXM_REDO, 0, 0);
		return 0;
		
	case IDM_EDIT_COPY: case WM_COPY:	
		SendMessage(g_hwndTextView, WM_COPY, 0, 0);
		return 0;
		
	case IDM_EDIT_CUT: case WM_CUT:
		SendMessage(g_hwndTextView, WM_CUT, 0, 0);
		return 0;
		
	case IDM_EDIT_PASTE: case WM_PASTE:
		SendMessage(g_hwndTextView, WM_PASTE, 0, 0);
		return 0;
			
	case IDM_EDIT_DELETE: case WM_CLEAR:
		SendMessage(g_hwndTextView, WM_CLEAR, 0, 0);
		return 0;

	case IDM_EDIT_FIND:
		ShowFindDlg(hwnd, FIND_PAGE);
		return 0;
		
	case IDM_EDIT_REPLACE:
		ShowFindDlg(hwnd, REPLACE_PAGE);
		return 0;

	case IDM_EDIT_GOTO:
		ShowFindDlg(hwnd, GOTO_PAGE);
		return 0;


	case IDM_EDIT_SELECTALL:
		TextView_SelectAll(g_hwndTextView);
		return 0;
		
	case IDM_VIEW_OPTIONS:
		ShowOptions(hwnd);
		return 0;
		
	case IDM_VIEW_LINENUMBERS:
		g_fLineNumbers = !g_fLineNumbers;
		TextView_SetStyleBool(g_hwndTextView, TXS_LINENUMBERS, g_fLineNumbers);
		return 0;
		
	case IDM_VIEW_LONGLINES:
		g_fLongLines = !g_fLongLines;
		TextView_SetStyleBool(g_hwndTextView, TXS_LONGLINES, g_fLongLines);
		return 0;
		
	case IDM_VIEW_STATUSBAR:
		g_fShowStatusbar = !g_fShowStatusbar;
		ShowWindow(g_hwndStatusbar, SW_HIDE);
		GetClientRect(hwnd, &rect);
		PostMessage(hwnd, WM_SIZE, 0, MAKEWPARAM(rect.right, rect.bottom));
		return 0;
		
	case IDM_VIEW_SAVEEXIT:
		g_fSaveOnExit = !g_fSaveOnExit;
		return 0;
		
	case IDM_VIEW_SAVENOW:
		SaveRegSettings();
		return 0;
		
	case IDM_HELP_ABOUT:
		ShowAboutDlg(hwnd);
		return 0;

	default:
		return 0;
	}
}

//
//	Main window procedure
//
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	int width, height, heightsb;
	HIMAGELIST hImgList;
	RECT rect;
	HDWP hdwp;
	NMHDR *nmhdr;
	TCHAR msgstr[MAX_PATH+200];

	switch(msg)
	{
	case WM_CREATE:
		g_hwndTextView  = CreateTextView(hwnd);
		g_hwndStatusbar = CreateStatusBar(hwnd);

		TextView_SetContextMenu(g_hwndTextView, GetSubMenu(LoadMenu(GetModuleHandle(0),
			MAKEINTRESOURCE(IDR_MENU2)), 0));

		// load the image list
		hImgList = ImageList_LoadImage(
			GetModuleHandle(0), 
			MAKEINTRESOURCE(IDB_BITMAP1), 
			16, 0, 
			RGB(255,0,255),
			IMAGE_BITMAP,
			LR_LOADTRANSPARENT|LR_CREATEDIBSECTION
			);
		
		TextView_SetImageList(g_hwndTextView, hImgList);

		// highlight specific lines with image-index "1"
		//TextView_SetLineImage(g_hwndTextView, 16, 1);
		//TextView_SetLineImage(g_hwndTextView, 5,  1);
		//TextView_SetLineImage(g_hwndTextView, 36, 1);
		//TextView_SetLineImage(g_hwndTextView, 11, 1);

		// tell windows that we can handle drag+drop'd files
		DragAcceptFiles(hwnd, TRUE);
		return 0;

	case WM_DROPFILES:
		HandleDropFiles(hwnd, (HDROP)wParam);
		return 0;

	case WM_DESTROY:
		SaveFileData(g_szFileName, hwnd);
		PostQuitMessage(0);
		DeleteObject(g_hFont);
		return 0;

	//case WM_NCCALCSIZE:
	//	return NcCalcSize(hwnd, wParam, lParam);

	case WM_INITMENU:
		CheckMenuCommand((HMENU)wParam, IDM_VIEW_LINENUMBERS,	g_fLineNumbers);
		CheckMenuCommand((HMENU)wParam, IDM_VIEW_LONGLINES,		g_fLongLines);
		CheckMenuCommand((HMENU)wParam, IDM_VIEW_SAVEEXIT,		g_fSaveOnExit);
		CheckMenuCommand((HMENU)wParam, IDM_VIEW_STATUSBAR,		g_fShowStatusbar);
		//CheckMenuCommand((HMENU)wParam, IDM_VIEW_SEARCHBAR,		g_hwndSearchBar ? TRUE : FALSE);

		EnableMenuCommand((HMENU)wParam, IDM_EDIT_UNDO,		TextView_CanUndo(g_hwndTextView));
		EnableMenuCommand((HMENU)wParam, IDM_EDIT_REDO,		TextView_CanRedo(g_hwndTextView));
		EnableMenuCommand((HMENU)wParam, IDM_EDIT_PASTE,	IsClipboardFormatAvailable(CF_TEXT));
		EnableMenuCommand((HMENU)wParam, IDM_EDIT_COPY,		TextView_GetSelSize(g_hwndTextView));
		EnableMenuCommand((HMENU)wParam, IDM_EDIT_CUT,		TextView_GetSelSize(g_hwndTextView));
		EnableMenuCommand((HMENU)wParam, IDM_EDIT_DELETE,	TextView_GetSelSize(g_hwndTextView));

		return 0;

	//case WM_USER:
	//	wsprintf(msgstr, _T("%s\n\nThis file has been modified outside of Neatpad.")
	//					 _T("Do you wish to reload it?"), g_szFileName);
	//	MessageBox(hwnd, msgstr, _T("Neatpad"), MB_ICONQUESTION|MB_YESNO);
	//
	//	return 0;

	case WM_ENABLE:

		// keep the modeless find/replace dialog in the same enabled state as the main window
		EnableWindow(g_hwndSearchDlg, (BOOL)wParam);
		return 0;

	case WM_MENUSELECT:
		StatusBarMenuSelect(hwnd, g_hwndStatusbar, wParam, lParam);
		return 0;

	case WM_NOTIFY:
		nmhdr = (NMHDR *)lParam;
		
		if(nmhdr->hwndFrom == g_hwndTextView)
			return TextViewNotifyHandler(hwnd, nmhdr);
		else
			return NotifyHandler(hwnd, nmhdr);

	case WM_COMMAND:
		return CommandHandler(hwnd, LOWORD(wParam), HIWORD(wParam), (HWND)lParam);

	case WM_SETFOCUS:
		SetFocus(g_hwndTextView);
		return 0;

	case WM_CLOSE:
		
		// does the file need saving?
		if(TextView_CanUndo(g_hwndTextView))
		{
			UINT r;
			wsprintf(msgstr, _T("Do you want to save changes to\r\n%s?"), g_szFileName);
			r = MessageBox(hwnd, msgstr, APP_TITLE, MB_YESNOCANCEL | MB_ICONQUESTION);

			if(r == IDCANCEL)
				return 0;
		}

		DestroyWindow(hwnd);
		return 0;

	case WM_SIZE:

		// resize the TextView and StatusBar to fit within the main window's client area
		width  = (short)LOWORD(lParam);
		height = (short)HIWORD(lParam);
		
		GetWindowRect(g_hwndStatusbar, &rect);
		heightsb = rect.bottom-rect.top;

		hdwp = BeginDeferWindowPos(3);
		
		if(g_fShowStatusbar)
		{
			DeferWindowPos(hdwp, g_hwndStatusbar, 0, 0, height - heightsb, width, heightsb, SWP_SHOWWINDOW);
		//	MoveWindow(g_hwndStatusbar, 0, height - heightsb, width, heightsb, TRUE);
			height -= heightsb;
		}

		DeferWindowPos(hdwp, g_hwndTextView, 0,  0, 0, width, height, SWP_SHOWWINDOW);
		//MoveWindow(g_hwndTextView, 0, 0, width, height, TRUE);

		EndDeferWindowPos(hdwp);

		SetStatusBarParts(g_hwndStatusbar);

		return 0;

	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

//
//	Register main window class
//
void InitMainWnd()
{
	WNDCLASSEX wcx;
	HANDLE hInst = GetModuleHandle(0);

	// Window class for the main application parent window
	wcx.cbSize			= sizeof(wcx);
	wcx.style			= CS_OWNDC;
	wcx.lpfnWndProc		= WndProc;
	wcx.cbClsExtra		= 0;
	wcx.cbWndExtra		= 0;
	wcx.hInstance		= hInst;
	wcx.hCursor			= LoadCursor (NULL, IDC_ARROW);
	wcx.hbrBackground	= (HBRUSH)0;
	wcx.lpszMenuName	= MAKEINTRESOURCE(IDR_MENU1);
	wcx.lpszClassName	= g_szAppName;
	wcx.hIcon			= LoadImage(hInst, MAKEINTRESOURCE(IDI_ICON1), IMAGE_ICON, 32, 32, LR_CREATEDIBSECTION);
	wcx.hIconSm			= LoadImage(hInst, MAKEINTRESOURCE(IDI_ICON1), IMAGE_ICON, 16, 16, LR_CREATEDIBSECTION);

	RegisterClassEx(&wcx);
}

//
//	Create a top-level window
//
HWND CreateMainWnd()
{
	return CreateWindowEx(0,
				g_szAppName,			// window class name
				g_szAppName,			// window caption
				WS_OVERLAPPEDWINDOW,//|WS_CLIPCHILDREN,
				CW_USEDEFAULT,			// initial x position
				CW_USEDEFAULT,			// initial y position
				CW_USEDEFAULT,			// initial x size
				CW_USEDEFAULT,			// initial y size
				NULL,					// parent window handle
				NULL,					// use window class menu
				GetModuleHandle(0),		// program instance handle
				NULL);					// creation parameters
}

TCHAR **GetArgvCommandLine(int *argc)
{
#ifdef UNICODE
	return CommandLineToArgvW(GetCommandLineW(), argc);
#else
	*argc = __argc;
	return __argv;
#endif
}

TCHAR * GetArg(TCHAR *ptr, TCHAR *buf, int len)
{
	int i  = 0;
	int ch;

	// make sure there's something to parse
	if(ptr == 0 || *ptr == '\0')
	{
		*buf = '\0';
		return 0;
	}

	ch = *ptr++;

	// skip leading whitespace
	while(ch == ' ' || ch == '\t')
		ch = *ptr++;

	// quoted filenames
	if(ch == '\"')
	{
		ch = *ptr++;
		while(i < len - 1 && ch && ch != '\"')
		{
			buf[i++] = ch;
			if(ch = *ptr) *ptr++;
		}
	}
	// grab a token
	else
	{
		while(i < len - 1 && ch && ch != ' ' && ch != '\t')
		{
			buf[i++] = ch;
			if(ch = *ptr) *ptr++;
		}
	}

	buf[i] = '\0';

	// skip trailing whitespace
	while(*ptr == ' ' || *ptr == '\t')
		ptr++;

	return ptr;
}

//
//	Entry-point for text-editor application
//
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int iShowCmd)
{
	MSG			msg;
	HACCEL		hAccel;
	TCHAR		**argv;
	int			argc;
	//TCHAR		*pszCmdlineFile = 0;
	TCHAR		arg[MAX_PATH];

	//
	// get the first commandline argument
	//
	TCHAR *pszCmdline = GetArg(GetCommandLineW(), arg, MAX_PATH);
	argv = GetArgvCommandLine(&argc);

	// check if we have any options
	if(pszCmdline && *pszCmdline == '-')
	{
		pszCmdline = GetArg(pszCmdline, arg, MAX_PATH);

		// do the user-account-control thing
		if(lstrcmpi(arg, _T("-uac")) == 0)
		{
			//return 0;
		}
		// image-file-execute-options
		else if(lstrcmpi(arg, _T("-ifeo")) == 0)
		{
			// skip notepad.exe
			pszCmdline = GetArg(pszCmdline, arg, MAX_PATH);
		}
		// unrecognised option
		else
		{
		}
	}


	// has a prior instance elevated us to admin in order to 
	// modify some system-wide settings in the registry?
	if(argv && argc == 4 && lstrcmpi(argv[1], _T("-uac")) == 0)
	{
		g_fAddToExplorer	= _ttoi(argv[2]);
		g_fReplaceNotepad	= _ttoi(argv[3]);

		if(SetExplorerContextMenu(g_fAddToExplorer) &&
			SetImageFileExecutionOptions(g_fReplaceNotepad))
		{
			SaveRegSysSettings();
			return 0;
		}
		else
		{
			return ERROR_ACCESS_DENIED;
		}
	}

	// default to the built-in resources
	g_hResourceModule = hInst;

	OleInitialize(0);

	// initialize window classes
	InitMainWnd();
	InitTextView();

	LoadRegSettings();

	// create the main window!
	g_hwndMain = CreateMainWnd();

	// open file specified on commmand line
	if(pszCmdline && *pszCmdline)
	{
		// check to see if it's a quoted filename
		if(*pszCmdline == '\"')
		{
			GetArg(pszCmdline, arg, MAX_PATH);
			pszCmdline = arg;
		}

		NeatpadOpenFile(g_hwndMain, pszCmdline);
		LoadFileData(pszCmdline, g_hwndMain);
	}
	// automatically create new document if none specified
	else
	{
		PostMessage(g_hwndMain, WM_COMMAND, IDM_FILE_NEW, 0);
	}

	ApplyRegSettings();
	ShowWindow(g_hwndMain, iShowCmd);

	//
	// load keyboard accelerator table
	//
	hAccel = LoadAccelerators(g_hResourceModule, MAKEINTRESOURCE(IDR_ACCELERATOR1));

	
	//
	// message-loop
	//
	while(GetMessage(&msg, NULL, 0, 0) > 0)
	{
		if(!TranslateAccelerator(g_hwndMain, hAccel, &msg))
		{
			if((!IsWindow(g_hwndSearchDlg) || !IsDialogMessage(g_hwndSearchDlg, &msg)))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
	}

	if(g_fSaveOnExit)
		SaveRegSettings();

	OleUninitialize();
	ExitProcess(0);
	return 0;
}