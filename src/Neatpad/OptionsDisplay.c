//
//	Neatpad
//	OptionsDisplay.c
//
//	www.catch22.net
//

#define STRICT
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <tchar.h>
#include "Neatpad.h"
#include "resource.h"
#include "..\TextView\TextView.h"

//
//	Dialogbox procedure for the FONT pane
//
INT_PTR CALLBACK DisplayOptionsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PSHNOTIFY *pshn;

    switch (msg)
    {
    case WM_INITDIALOG:

        SetWindowLongPtr(hwnd, DWLP_USER, ((PROPSHEETPAGE *)lParam)->lParam);
        CheckDlgButton(hwnd, IDC_LONGLINES, g_fLongLines);
        SendDlgItemMessage(hwnd, IDC_SPIN3, UDM_SETRANGE32, 1, 10000);
        SendDlgItemMessage(hwnd, IDC_LONGLINELIM, EM_LIMITTEXT, 5, 0);
        EnableDlgItem(hwnd, IDC_SPIN3, g_fLongLines);

        SetDlgItemInt(hwnd, IDC_LONGLINELIM, g_nLongLineLimit, FALSE);
        EnableDlgItem(hwnd, IDC_LONGLINELIM, g_fLongLines);

        CheckDlgButton(hwnd, IDC_LINENOS, g_fLineNumbers);
        CheckDlgButton(hwnd, IDC_SELMARGIN, g_fSelMargin);
        CheckDlgButton(hwnd, IDC_CHANGEINDICATORS, g_fChangeIndicators);
        CheckDlgButton(hwnd, IDC_HIGHLIGHTCHANGES, g_fHighlightChanges);
        EnableDlgItem(hwnd, IDC_CHANGEINDICATORS, g_fSelMargin);

        CheckDlgButton(hwnd, IDC_HIGHLIGHTCURLINE, g_nHLCurLine);

        SetDlgItemInt(hwnd, IDC_TABWIDTH, g_nTabWidth, FALSE);
        SendDlgItemMessage(hwnd, IDC_SPIN1, UDM_SETRANGE32, 1, TV_MAX_TAB_WIDTH);
        SendDlgItemMessage(hwnd, IDC_TABWIDTH, EM_LIMITTEXT, 2, 0);
        CheckDlgButton(hwnd, IDC_INSERTSPACES, g_fInsertSpaces);
        CheckDlgButton(hwnd, IDC_AUTOINDENT, g_fAutoIndent);

        SetDlgItemInt(hwnd, IDC_FILTERCONTEXT, g_nFilterContext, FALSE);
        SendDlgItemMessage(hwnd, IDC_FILTERCONTEXTSPIN, UDM_SETRANGE32, 0, TV_MAX_FILTER_CONTEXT);
        SendDlgItemMessage(hwnd, IDC_FILTERCONTEXT, EM_LIMITTEXT, 3, 0);

        return TRUE;

    case WM_CLOSE:
        return TRUE;

    case WM_NOTIFY:

        pshn = (PSHNOTIFY *)lParam;

        if (pshn->hdr.code == PSN_APPLY)
        {
            OPTIONS *options = (OPTIONS *)GetWindowLongPtr(hwnd, DWLP_USER);
            BOOL     valid;
            UINT     context = GetDlgItemInt(hwnd, IDC_FILTERCONTEXT, &valid, FALSE);

            if (!valid || context > TV_MAX_FILTER_CONTEXT)
            {
                MessageBox(hwnd, _T("Enter a context line count between 0 and 100."), _T("Filter context"),
                           MB_OK | MB_ICONWARNING);
                SetFocus(GetDlgItem(hwnd, IDC_FILTERCONTEXT));
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE);
                return TRUE;
            }
            UINT width = GetDlgItemInt(hwnd, IDC_TABWIDTH, &valid, FALSE);

            if (!valid || width < 1 || width > TV_MAX_TAB_WIDTH)
            {
                MessageBox(hwnd, _T("Enter a tab width between 1 and 32."), _T("Tab width"), MB_OK | MB_ICONWARNING);
                SetFocus(GetDlgItem(hwnd, IDC_TABWIDTH));
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE);
                return TRUE;
            }

            BOOL highlight = IsDlgButtonChecked(hwnd, IDC_LONGLINES) == BST_CHECKED;
            UINT limit     = GetDlgItemInt(hwnd, IDC_LONGLINELIM, &valid, FALSE);

            if (highlight && (!valid || limit < 1 || limit > 10000))
            {
                MessageBox(hwnd, _T("Enter a column between 1 and 10000."), _T("Long lines"), MB_OK | MB_ICONWARNING);
                SetFocus(GetDlgItem(hwnd, IDC_LONGLINELIM));
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE);
                return TRUE;
            }

            options->nTabWidth         = (LONG)width;
            options->fInsertSpaces     = IsDlgButtonChecked(hwnd, IDC_INSERTSPACES);
            options->fAutoIndent       = IsDlgButtonChecked(hwnd, IDC_AUTOINDENT);
            options->nFilterContext    = (LONG)context;
            options->fLineNumbers      = IsDlgButtonChecked(hwnd, IDC_LINENOS);
            options->fSelMargin        = IsDlgButtonChecked(hwnd, IDC_SELMARGIN);
            options->fChangeIndicators = IsDlgButtonChecked(hwnd, IDC_CHANGEINDICATORS);
            options->fHighlightChanges = IsDlgButtonChecked(hwnd, IDC_HIGHLIGHTCHANGES);
            options->fLongLines        = highlight;
            if (valid && limit >= 1 && limit <= 10000)
            {
                options->nLongLineLimit = (int)limit;
            }
            options->fHighlightCurrentLine = IsDlgButtonChecked(hwnd, IDC_HIGHLIGHTCURLINE);
            SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);

            return TRUE;
        }

        return FALSE;

    case WM_COMMAND:

        switch (LOWORD(wParam))
        {
        case IDC_SELMARGIN:
            if (HIWORD(wParam) == BN_CLICKED)
            {
                EnableDlgItem(hwnd, IDC_CHANGEINDICATORS, IsDlgButtonChecked(hwnd, IDC_SELMARGIN) == BST_CHECKED);
            }
            return TRUE;

        case IDC_LONGLINES:
            if (HIWORD(wParam) == BN_CLICKED)
            {
                BOOL enabled = IsDlgButtonChecked(hwnd, IDC_LONGLINES) == BST_CHECKED;

                EnableDlgItem(hwnd, IDC_LONGLINELIM, enabled);
                EnableDlgItem(hwnd, IDC_SPIN3, enabled);
            }
            return TRUE;

        case IDCANCEL:
            return TRUE;
        }

        return FALSE;
    }

    return FALSE;
}
