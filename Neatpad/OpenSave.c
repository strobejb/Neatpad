//
//	OpenSave.c
//
//	Open+Save dialog support for Neatpad
//
//	www.catch22.net
//	Written by J Brown 2004
//
//	Freeware
//
#define _CRT_SECURE_NO_DEPRECATE
#define _CRT_NON_CONFORMING_SWPRINTFS
#define _WIN32_WINNT 0x501
#define STRICT

#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <stdarg.h>
#include <commctrl.h>
#include "Neatpad.h"
#include "..\TextView\TextView.h"
#include "resource.h"

static BOOL g_fFirstTime = TRUE;

typedef struct 
{
	TCHAR	szFile[MAX_PATH];
	HWND	hwndNotify;
	HANDLE	hQuitEvent;
	UINT	uMsg;
} NOTIFY_DATA;

DWORD WINAPI ChangeNotifyThread(NOTIFY_DATA *pnd)
{
	HANDLE hChange;
	DWORD  dwResult;
	TCHAR  szDirectory[MAX_PATH];

	lstrcpy(szDirectory, pnd->szFile);

	// get the directory name from filename
	if(GetFileAttributes(szDirectory) != FILE_ATTRIBUTE_DIRECTORY)
	{
		TCHAR *slash = _tcsrchr(szDirectory, _T('\\'));
		if(slash) *slash = '\0';
	}
	
	// watch the specified directory for changes
	hChange = FindFirstChangeNotification(szDirectory, FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE);

	do
	{
		HANDLE hEventList[2] = { hChange, pnd->hQuitEvent };
		
		if((dwResult = WaitForMultipleObjects(1, hEventList, FALSE, INFINITE)) == WAIT_OBJECT_0)
		{
			PostMessage(pnd->hwndNotify, pnd->uMsg, 0, (LPARAM)pnd);
		}

		FindNextChangeNotification(hChange);
	} 
	while(dwResult == WAIT_OBJECT_0);

	// cleanup
	FindCloseChangeNotification(hChange);
	free(pnd);

	return 0;
}

BOOL NotifyFileChange(TCHAR *szPathName, HWND hwndNotify, HANDLE hQuitEvent)
{
	NOTIFY_DATA *pnd = malloc(sizeof(NOTIFY_DATA));

	pnd->hQuitEvent = 0;
	pnd->hwndNotify = hwndNotify;
	pnd->uMsg		= WM_USER;
	lstrcpy(pnd->szFile, szPathName);

	CreateThread(0, 0, ChangeNotifyThread, pnd, 0, 0);

	return TRUE;
}


//
//	Hook procedure for the Open dialog, 
//	used to center the dialog on 1st invokation
//
UINT_PTR CALLBACK OpenHookProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch(msg)
	{
	case WM_INITDIALOG:

		if(g_fFirstTime)
		{
			CenterWindow(GetParent(hwnd));
			g_fFirstTime = FALSE;
		}

		return TRUE;
	}

	return 0;
}

//
//	Show the GetOpenFileName common dialog
//
BOOL ShowOpenFileDlg(HWND hwnd, TCHAR *pstrFileName, TCHAR *pstrTitleName)
{
	TCHAR *szFilter		= _T("Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0\0");
	
	OPENFILENAME ofn	= { sizeof(ofn) };

	ofn.hwndOwner		= hwnd;
	ofn.hInstance		= g_hResourceModule;
	ofn.lpstrFilter		= szFilter;
	ofn.lpstrFile		= pstrFileName;
	ofn.lpstrFileTitle	= pstrTitleName;
	ofn.lpfnHook		= OpenHookProc;
	
	ofn.nFilterIndex	= 1;
	ofn.nMaxFile		= _MAX_PATH;
	ofn.nMaxFileTitle	= _MAX_FNAME + _MAX_EXT; 

	// flags to control appearance of open-file dialog
	ofn.Flags			=	OFN_EXPLORER			| 
							OFN_ENABLESIZING		|
							OFN_ALLOWMULTISELECT	| 
							OFN_FILEMUSTEXIST		|
							0;//OFN_ENABLEHOOK			;

	return GetOpenFileName(&ofn);
}

//
//	Hook procedure for the SaveAs dialog,
//	used to center the dialog on 1st invokation and manage the 'encoding' combobox
//
UINT_PTR CALLBACK SaveHookProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	HWND hwndCombo;
	RECT rect;

	switch(msg)
	{
	case WM_INITDIALOG:

		if(g_fFirstTime)
		{
			CenterWindow(GetParent(hwnd));
			g_fFirstTime = FALSE;
		}

		SendDlgItemMessage(hwnd, IDC_ENCODINGLIST, CB_ADDSTRING, 0, (LPARAM)_T("Ascii"));
		SendDlgItemMessage(hwnd, IDC_ENCODINGLIST, CB_ADDSTRING, 0, (LPARAM)_T("Unicode (UTF-8)"));
		SendDlgItemMessage(hwnd, IDC_ENCODINGLIST, CB_ADDSTRING, 0, (LPARAM)_T("Unicode (UTF-16)"));
		SendDlgItemMessage(hwnd, IDC_ENCODINGLIST, CB_ADDSTRING, 0, (LPARAM)_T("Unicode (UTF-16, Big Endian)"));
		SendDlgItemMessage(hwnd, IDC_ENCODINGLIST, CB_SETCURSEL, 0, 0);

		SendDlgItemMessage(hwnd, IDC_LINEFMTLIST, CB_ADDSTRING, 0, (LPARAM)_T("Keep Existing"));
		SendDlgItemMessage(hwnd, IDC_LINEFMTLIST, CB_ADDSTRING, 0, (LPARAM)_T("Windows (CR/LF)"));
		SendDlgItemMessage(hwnd, IDC_LINEFMTLIST, CB_ADDSTRING, 0, (LPARAM)_T("Unix (LF)"));
		SendDlgItemMessage(hwnd, IDC_LINEFMTLIST, CB_ADDSTRING, 0, (LPARAM)_T("Mac (CR)"));
		SendDlgItemMessage(hwnd, IDC_LINEFMTLIST, CB_ADDSTRING, 0, (LPARAM)_T("Unicode (LT)"));
		SendDlgItemMessage(hwnd, IDC_LINEFMTLIST, CB_SETCURSEL, 0, 0);


		return TRUE;

	case WM_SIZE:

		// Get the coordinates of the 'Save As Type' combo
		hwndCombo = GetDlgItem(GetParent(hwnd), 0x470);

		if(hwndCombo)
		{
			GetWindowRect(hwndCombo, &rect);
			ScreenToClient(hwnd, (POINT *)&rect.left);
			ScreenToClient(hwnd, (POINT*)&rect.right);
		
			// Resize our 'Line Format' combo to be the same width
			hwndCombo = GetDlgItem(hwnd, IDC_LINEFMTLIST);
			SetWindowPos(hwndCombo, 0, 0, 0, rect.right-rect.left, rect.bottom-rect.top, 
				SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);

			// Resize our 'Encoding' combo to be the same width
			hwndCombo = GetDlgItem(hwnd, IDC_ENCODINGLIST);
			SetWindowPos(hwndCombo, 0, 0, 0, rect.right-rect.left, rect.bottom-rect.top, 
				SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
		}

		return 0;

	default:
		break;
	}

	return 0;
}

//
//	Show the GetSaveFileName common dialog
//
BOOL ShowSaveFileDlg(HWND hwnd, TCHAR *pstrFileName, TCHAR *pstrTitleName)
{
	TCHAR *szFilter		= _T("Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0\0");
	
	OPENFILENAME ofn	= { sizeof(ofn) };

	ofn.hwndOwner		= hwnd;
	ofn.hInstance		= g_hResourceModule;
	ofn.lpstrFilter		= szFilter;
	ofn.lpstrFile		= pstrFileName;
	ofn.lpstrFileTitle	= pstrTitleName;
	ofn.lpTemplateName	= MAKEINTRESOURCE(IDD_SAVEFILE);
	ofn.lpfnHook		= SaveHookProc;
	
	ofn.nFilterIndex	= 1;
	ofn.nMaxFile		= _MAX_PATH;
	ofn.nMaxFileTitle	= _MAX_FNAME + _MAX_EXT; 

	// flags to control appearance of open-file dialog
	ofn.Flags			=	OFN_EXPLORER			| 
							OFN_ENABLESIZING		|
							OFN_OVERWRITEPROMPT		|
							OFN_ENABLETEMPLATE		|
							OFN_ENABLEHOOK			;

	return GetSaveFileName(&ofn);
}

UINT FmtErrorMsg(HWND hwnd, DWORD dwMsgBoxType, DWORD dwError, TCHAR *szFmt, ...)
{
	TCHAR *lpMsgBuf;
	TCHAR *ptr;
	UINT   msgboxerr;
	va_list varg;

	va_start(varg, szFmt);

	FormatMessage( 
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, dwError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), 
		(LPTSTR) &lpMsgBuf, 0, NULL 
		);

	ptr = LocalAlloc(LPTR, LocalSize(lpMsgBuf) + 1000 * sizeof(TCHAR));
	_vstprintf(ptr, szFmt, varg);
	_tcscat(ptr, lpMsgBuf);

	msgboxerr = MessageBox(hwnd, ptr, APP_TITLE, dwMsgBoxType);
	
	LocalFree( lpMsgBuf );
	LocalFree( ptr );
	va_end(varg);

	return msgboxerr;
}

//
//	Open the specified file
//
BOOL DoOpenFile(HWND hwndMain, TCHAR *szFileName, TCHAR *szFileTitle)
{
	int fmt, fmtlook[] = 
	{
		IDM_VIEW_ASCII, IDM_VIEW_UTF8, IDM_VIEW_UTF16, IDM_VIEW_UTF16BE 
	};

	if(TextView_OpenFile(g_hwndTextView, szFileName))
	{
		SetWindowFileName(hwndMain, szFileTitle, FALSE);
		g_fFileChanged   = FALSE;

		fmt = TextView_GetFormat(g_hwndTextView);

		CheckMenuRadioItem(GetMenu(hwndMain), 
			IDM_VIEW_ASCII, IDM_VIEW_UTF16BE, 
			fmtlook[fmt], MF_BYCOMMAND);

		NotifyFileChange(szFileName, hwndMain, 0);
		return TRUE;
	}
	else
	{
		FmtErrorMsg(hwndMain, MB_OK|MB_ICONWARNING, GetLastError(), _T("Error opening \'%s\'\r\n\r\n"), szFileName);
		//FormatMessage
		//MessageBox(hwndMain, _T("Error opening file"), APP_TITLE, MB_ICONEXCLAMATION);
		return FALSE;
	}
}

void NeatpadOpenFile(HWND hwnd, TCHAR *szFile)
{
	TCHAR *name;

	// save current file's position!
	SaveFileData(g_szFileName, hwnd);

	_tcscpy(g_szFileName, szFile);

	name = _tcsrchr(g_szFileName, '\\');
	_tcscpy(g_szFileTitle, name ? name+1 : szFile);

	DoOpenFile(hwnd, g_szFileName, g_szFileTitle);
}

//
//	How to process WM_DROPFILES
//
void HandleDropFiles(HWND hwnd, HDROP hDrop)
{
	TCHAR buf[MAX_PATH];
	
	if(DragQueryFile(hDrop, 0, buf, MAX_PATH))
	{
		TCHAR tmp[MAX_PATH];
		
		if(ResolveShortcut(buf, tmp, MAX_PATH))
			lstrcpy(buf,tmp);

		NeatpadOpenFile(hwnd, buf);
	}
	
	DragFinish(hDrop);
}
