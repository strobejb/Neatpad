//
//	OpenSave.c
//
//	Open+Save dialog support for Neatpad
//
//	www.catch22.net
//	Written by J Brown 2004
//
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

// The open file as it was when opened or saved, to tell when another program
// changes it: its size and when it was written, if it was on disk
static WIN32_FILE_ATTRIBUTE_DATA g_FileStamp;
static BOOL g_fFileOnDisk;

// Following the file as another program appends to it: it's read-only, and
// CheckFileChange adds what's appended
static BOOL g_fTailing;

static void NoteFileStamp(TCHAR *szFileName)
{
	g_fFileOnDisk = GetFileAttributesEx(szFileName, GetFileExInfoStandard, &g_FileStamp);
}

static ULONGLONG FileSize(const WIN32_FILE_ATTRIBUTE_DATA *stamp)
{
	return ((ULONGLONG)stamp->nFileSizeHigh << 32) | stamp->nFileSizeLow;
}

static BOOL SameFileStamp(const WIN32_FILE_ATTRIBUTE_DATA *a, const WIN32_FILE_ATTRIBUTE_DATA *b)
{
	return FileSize(a) == FileSize(b) && CompareFileTime(&a->ftLastWriteTime, &b->ftLastWriteTime) == 0;
}

//
//	Called once a second: if another program has changed or deleted the open
//	file since Neatpad last looked, say so in the notification bar. A file
//	being followed takes in what's been appended instead, until it's deleted
//	or rewritten shorter.
//
void CheckFileChange(void)
{
	WIN32_FILE_ATTRIBUTE_DATA now;
	BOOL onDisk;

	// an untitled file isn't on disk
	if(g_szFileTitle[0] == 0)
		return;

	onDisk = GetFileAttributesEx(g_szFileName, GetFileExInfoStandard, &now);

	// it's only deleted once it can't be found, not while it's out of reach for a moment
	if(!onDisk && GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND)
		return;

	if(onDisk == g_fFileOnDisk && (!onDisk || SameFileStamp(&now, &g_FileStamp)))
		return;

	// are we tailing the file?
	if(g_fTailing && onDisk && FileSize(&now) >= FileSize(&g_FileStamp) && TextView_UpdateTail(g_hwndTextView))
	{
		g_FileStamp = now;
		return;
	}

	g_FileStamp   = now;
	g_fFileOnDisk = onDisk;
	g_fTailing    = FALSE;

	ShowNotifyBar(onDisk ? NOTIFY_CHANGED : NOTIFY_DELETED);
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

// The line format list's entries, in order, as TextView line breaks
static const UINT g_LineBreaks[] = { 0, TXL_CRLF, TXL_LF, TXL_CR };

//
//	Show the byte-order mark choice for the encoding selected. Only UTF-8 is
//	written either way: UTF-16 and UTF-32 keep their mark, which is the only way
//	to tell them apart, and ASCII has none. A UTF-8 file starts with its own
//	choice; one converted to UTF-8 starts without a mark, as UTF-8 usually is.
//
static void ShowBomChoice(HWND hwnd, const SAVEOPTIONS *pOptions)
{
	UINT nEncoding = (UINT)SendDlgItemMessage(hwnd, IDC_ENCODINGLIST, CB_GETCURSEL, 0, 0);
	BOOL fBom;

	if(nEncoding == NCP_UTF8)
		fBom = pOptions->nEncoding == NCP_UTF8 && pOptions->fBom;
	else
		fBom = nEncoding != NCP_ASCII;

	CheckDlgButton(hwnd, IDC_INCLUDEBOM, fBom ? BST_CHECKED : BST_UNCHECKED);
	EnableWindow(GetDlgItem(hwnd, IDC_INCLUDEBOM), nEncoding == NCP_UTF8);
}

//
//	Hook procedure for the SaveAs dialog, used to center the dialog on 1st
//	invokation and to manage its own controls: the encoding, byte-order mark and
//	line format. The dialog's lCustData points to the SAVEOPTIONS they start
//	from, which receives what they're left as.
//
UINT_PTR CALLBACK SaveHookProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	HWND hwndCombo;
	RECT rect;
	SAVEOPTIONS *pOptions;
	LRESULT sel;

	switch(msg)
	{
	case WM_INITDIALOG:

		if(g_fFirstTime)
		{
			CenterWindow(GetParent(hwnd));
			g_fFirstTime = FALSE;
		}

		pOptions = (SAVEOPTIONS *)((OPENFILENAME *)lParam)->lCustData;
		SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pOptions);

		// in NCP_* order
		SendDlgItemMessage(hwnd, IDC_ENCODINGLIST, CB_ADDSTRING, 0, (LPARAM)_T("Ascii"));
		SendDlgItemMessage(hwnd, IDC_ENCODINGLIST, CB_ADDSTRING, 0, (LPARAM)_T("Unicode (UTF-8)"));
		SendDlgItemMessage(hwnd, IDC_ENCODINGLIST, CB_ADDSTRING, 0, (LPARAM)_T("Unicode (UTF-16)"));
		SendDlgItemMessage(hwnd, IDC_ENCODINGLIST, CB_ADDSTRING, 0, (LPARAM)_T("Unicode (UTF-16, Big Endian)"));
		SendDlgItemMessage(hwnd, IDC_ENCODINGLIST, CB_ADDSTRING, 0, (LPARAM)_T("Unicode (UTF-32)"));
		SendDlgItemMessage(hwnd, IDC_ENCODINGLIST, CB_ADDSTRING, 0, (LPARAM)_T("Unicode (UTF-32, Big Endian)"));
		SendDlgItemMessage(hwnd, IDC_ENCODINGLIST, CB_SETCURSEL, pOptions->nEncoding, 0);

		// in g_LineBreaks order
		SendDlgItemMessage(hwnd, IDC_LINEFMTLIST, CB_ADDSTRING, 0, (LPARAM)_T("Keep Existing"));
		SendDlgItemMessage(hwnd, IDC_LINEFMTLIST, CB_ADDSTRING, 0, (LPARAM)_T("Windows (CR/LF)"));
		SendDlgItemMessage(hwnd, IDC_LINEFMTLIST, CB_ADDSTRING, 0, (LPARAM)_T("Unix (LF)"));
		SendDlgItemMessage(hwnd, IDC_LINEFMTLIST, CB_ADDSTRING, 0, (LPARAM)_T("Mac (CR)"));
		SendDlgItemMessage(hwnd, IDC_LINEFMTLIST, CB_SETCURSEL, 0, 0);

		ShowBomChoice(hwnd, pOptions);
		return TRUE;

	case WM_COMMAND:

		// each encoding has its own byte-order mark choice
		if(LOWORD(wParam) == IDC_ENCODINGLIST && HIWORD(wParam) == CBN_SELCHANGE)
			ShowBomChoice(hwnd, (SAVEOPTIONS *)GetWindowLongPtr(hwnd, GWLP_USERDATA));

		return 0;

	case WM_NOTIFY:

		// the user chose the file: take the choices with it
		if(((NMHDR *)lParam)->code == CDN_FILEOK)
		{
			pOptions = (SAVEOPTIONS *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
			pOptions->nEncoding = (UINT)SendDlgItemMessage(hwnd, IDC_ENCODINGLIST, CB_GETCURSEL, 0, 0);
			pOptions->fBom      = IsDlgButtonChecked(hwnd, IDC_INCLUDEBOM) == BST_CHECKED;

			sel = SendDlgItemMessage(hwnd, IDC_LINEFMTLIST, CB_GETCURSEL, 0, 0);
			pOptions->nLineBreak = sel > 0 && sel < ARRAYSIZE(g_LineBreaks) ? g_LineBreaks[sel] : 0;
		}

		return 0;

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
//	Show the GetSaveFileName common dialog. *pOptions are the encoding, byte-order
//	mark and line format it starts with, and come back as the user left them.
//
BOOL ShowSaveFileDlg(HWND hwnd, TCHAR *pstrFileName, TCHAR *pstrTitleName, SAVEOPTIONS *pOptions)
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
	ofn.lCustData		= (LPARAM)pOptions;
	
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
//	Show the file's encoding: checked in the menu, and in the statusbar
//
void ShowFileFormat(HWND hwndMain)
{
	static const UINT fmtlook[] =
	{
		IDM_VIEW_ASCII, IDM_VIEW_UTF8, IDM_VIEW_UTF16, IDM_VIEW_UTF16BE
	};

	UINT fmt = TextView_GetFormat(g_hwndTextView);
	UINT i;

	if(fmt < ARRAYSIZE(fmtlook))
	{
		CheckMenuRadioItem(GetMenu(hwndMain),
			IDM_VIEW_ASCII, IDM_VIEW_UTF16BE,
			fmtlook[fmt], MF_BYCOMMAND);
	}
	else
	{
		// the menu has no UTF-32 entries
		for(i = 0; i < ARRAYSIZE(fmtlook); i++)
			CheckMenuItem(GetMenu(hwndMain), fmtlook[i], MF_BYCOMMAND | MF_UNCHECKED);
	}

	UpdateStatusBarFileInfo();
}

//
//	Open the specified file
//
//
//	Show a file that's just been opened: its name in the title, its format in
//	the statusbar, and the notification bar if another program is writing it
//
static void ShowOpenedFile(HWND hwndMain, TCHAR *szFileName, TCHAR *szFileTitle)
{
    RecoveryReset();
    SetWindowFileName(hwndMain, szFileTitle, FALSE);
	g_fFileChanged = FALSE;
	g_fTailing     = FALSE;

	ShowFileFormat(hwndMain);
	NoteFileStamp(szFileName);

	if(TextView_GetEditMode(g_hwndTextView) == MODE_READONLY)
		ShowNotifyBar(NOTIFY_READONLY);
	else
		HideNotifyBar();
}

void ShowRecoveredFile(HWND hwnd, const TCHAR *filename)
{
    const TCHAR *title = _tcsrchr(filename, _T('\\'));

    lstrcpyn(g_szFileName, filename, MAX_PATH);
    lstrcpyn(g_szFileTitle, title ? title + 1 : filename, MAX_PATH);

    g_fFileChanged = TRUE;
    g_fTailing     = FALSE;

    NoteFileStamp(g_szFileName);
    SetWindowFileName(hwnd, g_szFileTitle[0] ? g_szFileTitle : _T("Untitled"), TRUE);
    ShowFileFormat(hwnd);
    ShowNotifyBar(g_szFileTitle[0] ? NOTIFY_RECOVERED : NOTIFY_RECOVERED_UNTITLED);
}

BOOL DoOpenFile(HWND hwndMain, TCHAR *szFileName, TCHAR *szFileTitle)
{
	if(TextView_OpenFile(g_hwndTextView, szFileName))
	{
		ShowOpenedFile(hwndMain, szFileName, szFileTitle);
		AddRecentFile(szFileName);
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

//
//	How the file is now, for saving it as it is
//
static void CurrentSaveOptions(SAVEOPTIONS *pOptions)
{
	pOptions->nEncoding  = TextView_GetFormat(g_hwndTextView);
	pOptions->fBom       = TextView_GetBom(g_hwndTextView);
	pOptions->nLineBreak = 0;
}

//
//	Save to the specified file. Converting it opens it again, in its new form.
//
BOOL DoSaveFile(HWND hwndMain, TCHAR *szFileName, TCHAR *szFileTitle, const SAVEOPTIONS *pOptions)
{
    BOOL converted  = pOptions->nEncoding != TextView_GetFormat(g_hwndTextView) || pOptions->nLineBreak != 0;
    BOOL recoveryOK = converted || RecoveryBeforeSave();

    if(TextView_SaveFile(g_hwndTextView, szFileName, pOptions->nEncoding, pOptions->fBom, pOptions->nLineBreak))
	{
		SetWindowFileName(hwndMain, szFileTitle, FALSE);
		g_fFileChanged = FALSE;
		ShowFileFormat(hwndMain);

		// what's on disk is now this text, whatever another program did before
		NoteFileStamp(szFileName);
		HideNotifyBar();
        if (converted)
            RecoveryReset();
        else
            RecoverySaved();
        if (!recoveryOK)
            ShowNotifyBar(NOTIFY_RECOVERY_WRITE_FAILED);
        return TRUE;
	}
	else
	{
		FmtErrorMsg(hwndMain, MB_OK|MB_ICONWARNING, GetLastError(), _T("Error saving \'%s\'\r\n\r\n"), szFileName);
		return FALSE;
	}
}

//
//	File > Save, as the file is now. An untitled file asks for a name first, as
//	does a read-only one: another program is writing it.
//
BOOL NeatpadSaveFile(HWND hwnd)
{
	SAVEOPTIONS options;

	if(g_szFileTitle[0] == '\0' || TextView_GetEditMode(g_hwndTextView) == MODE_READONLY)
		return NeatpadSaveFileAs(hwnd);

	CurrentSaveOptions(&options);
	return DoSaveFile(hwnd, g_szFileName, g_szFileTitle, &options);
}

//
//	File > Save As, perhaps in another encoding or line format. The new name
//	becomes the file's name once it's saved.
//
BOOL NeatpadSaveFileAs(HWND hwnd)
{
	TCHAR szFileName[MAX_PATH];
	TCHAR szFileTitle[MAX_PATH] = _T("");
	SAVEOPTIONS options;
	BOOL fReadOnly = TextView_GetEditMode(g_hwndTextView) == MODE_READONLY;

	// File > New clears the title but leaves the old file name behind
	lstrcpy(szFileName, g_szFileTitle[0] ? g_szFileName : _T(""));
	CurrentSaveOptions(&options);

	if(!ShowSaveFileDlg(hwnd, szFileName, szFileTitle, &options))
		return FALSE;

	// Unicode to Ascii can't keep every character
	if(options.nEncoding == NCP_ASCII && TextView_GetFormat(g_hwndTextView) != NCP_ASCII &&
	   MessageBox(hwnd, _T("Characters that your system's code page can't represent will be saved as '?'.\r\n\r\nSave as Ascii anyway?"),
				  APP_TITLE, MB_YESNO | MB_ICONWARNING) != IDYES)
		return FALSE;

	if(!DoSaveFile(hwnd, szFileName, szFileTitle, &options))
		return FALSE;

	lstrcpy(g_szFileName, szFileName);
	lstrcpy(g_szFileTitle, szFileTitle);
	AddRecentFile(g_szFileName);

	// a read-only file saved as another is that file now, to edit as usual
	if(fReadOnly)
		DoOpenFile(hwnd, g_szFileName, g_szFileTitle);

	return TRUE;
}

//
//	Opening the file again loses unsaved changes: ask first
//
static BOOL DiscardChanges(HWND hwnd)
{
	return !TextView_GetModify(g_hwndTextView) ||
		   MessageBox(hwnd, _T("Discard your unsaved changes?"), APP_TITLE, MB_YESNO | MB_ICONWARNING) == IDYES;
}

//
//	Open the file again, as it is on disk now, with the caret back on its line
//
BOOL NeatpadReloadFile(HWND hwnd)
{
	TVNCURSORINFO ci;

	if(!DiscardChanges(hwnd))
		return FALSE;

	TextView_GetCursorInfo(g_hwndTextView, &ci);

	if(!DoOpenFile(hwnd, g_szFileName, g_szFileTitle))
		return FALSE;

	if(ci.fLineNoKnown)
		TextView_GotoLine(g_hwndTextView, ci.nLineNo);

	return TRUE;
}

//
//	Follow the file as another program appends to it: open it again, read-only,
//	at its end, for CheckFileChange to add what's appended
//
BOOL NeatpadTailFile(HWND hwnd)
{
	if(!DiscardChanges(hwnd))
		return FALSE;

	if(!TextView_TailFile(g_hwndTextView, g_szFileName))
	{
		FmtErrorMsg(hwnd, MB_OK|MB_ICONWARNING, GetLastError(), _T("Error opening '%s'\r\n\r\n"), g_szFileName);
		return FALSE;
	}

	ShowOpenedFile(hwnd, g_szFileName, g_szFileTitle);
	g_fTailing = TRUE;
	ShowNotifyBar(NOTIFY_TAILING);
	return TRUE;
}

//
//	Stop following the file, and open it as usual: editable, unless another
//	program is still writing it
//
BOOL NeatpadStopTail(HWND hwnd)
{
	g_fTailing = FALSE;
	return NeatpadReloadFile(hwnd);
}

//
//	Before the text is closed or replaced, offer to save its changes. Returns
//	FALSE to keep the text: the user cancelled, or the save didn't happen.
//
BOOL QuerySaveChanges(HWND hwnd)
{
	TCHAR msg[MAX_PATH + 64];

	if(!TextView_GetModify(g_hwndTextView))
		return TRUE;

	wsprintf(msg, _T("Do you want to save changes to\r\n%s?"), g_szFileTitle[0] ? g_szFileName : _T("Untitled"));

	switch(MessageBox(hwnd, msg, APP_TITLE, MB_YESNOCANCEL | MB_ICONQUESTION))
	{
	case IDYES:	return NeatpadSaveFile(hwnd);
	case IDNO:	return TRUE;
	default:	return FALSE;
	}
}

BOOL NeatpadOpenFile(HWND hwnd, TCHAR *szFile)
{
	TCHAR *name;
	TCHAR szOldFileName[MAX_PATH];
	TCHAR szOldFileTitle[MAX_PATH];

	lstrcpy(szOldFileName, g_szFileName);
	lstrcpy(szOldFileTitle, g_szFileTitle);

	// save current file's position!
	SaveFileData(g_szFileName, hwnd);

	_tcscpy(g_szFileName, szFile);

	name = _tcsrchr(g_szFileName, '\\');
	_tcscpy(g_szFileTitle, name ? name+1 : szFile);

	if(DoOpenFile(hwnd, g_szFileName, g_szFileTitle))
		return TRUE;

	lstrcpy(g_szFileName, szOldFileName);
	lstrcpy(g_szFileTitle, szOldFileTitle);
	return FALSE;
}

//
//	How to process WM_DROPFILES
//
void HandleDropFiles(HWND hwnd, HDROP hDrop)
{
	TCHAR buf[MAX_PATH];
	
	if(DragQueryFile(hDrop, 0, buf, MAX_PATH) && QuerySaveChanges(hwnd))
	{
		TCHAR tmp[MAX_PATH];
		
		if(ResolveShortcut(buf, tmp, MAX_PATH))
			lstrcpy(buf,tmp);

		NeatpadOpenFile(hwnd, buf);
	}
	
	DragFinish(hDrop);
}
