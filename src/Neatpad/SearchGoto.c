//
//	SearchGoto.c
//
//	The Go To Line dialog. A line in the indexed part of the file - all of it,
//	for files up to 4 MB - is found at once. One further into a large file
//	can't be found until indexing gets there: the dialog offers to index the
//	file, shows how far it has got, and lets you go to the line once it's past.
//
#define _CRT_SECURE_NO_DEPRECATE
#define STRICT

#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <stdlib.h>
#include "Neatpad.h"
#include "..\TextView\TextView.h"
#include "resource.h"

// While the dialog is open it checks on indexing this often
#define GOTO_TIMER		1
#define GOTO_TIMER_MS	250

//
//	The line typed, counted from 1: 0 for none
//
static unsigned __int64 TypedLine(HWND hwnd)
{
	TCHAR text[32];

	GetDlgItemText(hwnd, IDC_GOTO_LINE, text, 32);
	return _tcstoui64(text, 0, 10);
}

//
//	Change a control's text only when it differs, so the timer doesn't flicker it
//
static void SetText(HWND hwnd, int nCtrlId, TCHAR *text)
{
	TCHAR current[100];

	GetDlgItemText(hwnd, nCtrlId, current, 100);

	if(lstrcmp(current, text) != 0)
		SetDlgItemText(hwnd, nCtrlId, text);
}

//
//	Show what can be done with the typed line: go to it, index the file to
//	find it, or wait while indexing gets there
//
static void UpdateGotoLine(HWND hwnd)
{
	unsigned __int64 line   = TypedLine(hwnd);
	TEXT_RESULT		 result = line ? TextView_CanGotoLine(g_hwndTextView, line - 1) : TDR_FAILED;
	TVNINDEXINFO	 info   = { 0 };
	TCHAR			 count[32];
	TCHAR			 text[100];

	TextView_GetIndexInfo(g_hwndTextView, &info);
	FormatCount(count, 32, info.nLineCount ? info.nLineCount : 1);

	// the range, once every line is counted
	if(info.nPercent == 100)
		wsprintf(text, _T("&Line number (1 \x2013 %s):"), count);
	else
		lstrcpy(text, _T("&Line number:"));

	SetText(hwnd, IDC_GOTO_LABEL, text);

	// a line past the index: offer to index the file
	if(result == TDR_PENDING_INDEX && !info.fIndexing)
	{
		SetText(hwnd, IDOK, _T("&Index file"));
		SetText(hwnd, IDC_GOTO_STATUS, _T("That line hasn't been indexed yet."));
		EnableWindow(GetDlgItem(hwnd, IDOK), TRUE);
		return;
	}

	text[0] = 0;

	if(result == TDR_PENDING_INDEX)
		wsprintf(text, _T("Indexing %u%%..."), info.nPercent);
	else if(result == TDR_FAILED && line > 0 && info.nPercent == 100)
		wsprintf(text, _T("The file has %s lines."), count);

	SetText(hwnd, IDOK, _T("&Go To"));
	SetText(hwnd, IDC_GOTO_STATUS, text);
	EnableWindow(GetDlgItem(hwnd, IDOK), result == TDR_OK);
}

//
//	Go to the typed line, or start indexing to find it
//
static void GotoLine(HWND hwnd)
{
	unsigned __int64 line   = TypedLine(hwnd);
	TEXT_RESULT		 result = line ? TextView_CanGotoLine(g_hwndTextView, line - 1) : TDR_FAILED;

	if(result == TDR_PENDING_INDEX)
	{
		TextView_IndexFile(g_hwndTextView, TRUE);
		UpdateGotoLine(hwnd);
	}
	else if(result == TDR_OK && TextView_GotoLine(g_hwndTextView, line - 1) == TDR_OK)
	{
		EndDialog(hwnd, IDOK);
	}
}

static INT_PTR CALLBACK GotoLineDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	TVNCURSORINFO ci;
	TCHAR		  text[32];

	UNREFERENCED_PARAMETER(lParam);

	switch(msg)
	{
	case WM_INITDIALOG:

		CenterWindow(hwnd);

		// start with the caret's line, when it's known
		if(TextView_GetCursorInfo(g_hwndTextView, &ci) && ci.fLineNoKnown)
		{
			_sntprintf(text, 32, _T("%I64u"), ci.nLineNo + 1);
			text[31] = 0;
			SetDlgItemText(hwnd, IDC_GOTO_LINE, text);
		}

		SendDlgItemMessage(hwnd, IDC_GOTO_LINE, EM_SETSEL, 0, -1);
		SetTimer(hwnd, GOTO_TIMER, GOTO_TIMER_MS, 0);
		UpdateGotoLine(hwnd);
		return TRUE;

	case WM_TIMER:
		UpdateGotoLine(hwnd);
		return TRUE;

	case WM_COMMAND:

		switch(LOWORD(wParam))
		{
		case IDC_GOTO_LINE:
			if(HIWORD(wParam) == EN_CHANGE)
				UpdateGotoLine(hwnd);
			return TRUE;

		case IDOK:
			GotoLine(hwnd);
			return TRUE;

		case IDCANCEL:
			EndDialog(hwnd, IDCANCEL);
			return TRUE;
		}

		break;

	case WM_DESTROY:
		KillTimer(hwnd, GOTO_TIMER);
		break;
	}

	return FALSE;
}

//
//	Show the Go To Line dialog
//
void ShowGotoLineDlg(HWND hwndMain)
{
	DialogBox(g_hResourceModule, MAKEINTRESOURCE(IDD_GOTOLINE), hwndMain, GotoLineDlgProc);
}
