// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

//
// Neatpad - Explorer integration for the current user.
//

#define STRICT
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <tchar.h>
#include <commctrl.h>
#include <shellapi.h>
#include "Neatpad.h"
#include "resource.h"

INT_PTR CALLBACK MiscOptionsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PSHNOTIFY *notification;
    OPTIONS   *options = (OPTIONS *)GetWindowLongPtr(hwnd, DWLP_USER);
    BOOL       enabled;

    switch (msg)
    {
    case WM_INITDIALOG:
        options = (OPTIONS *)((PROPSHEETPAGE *)lParam)->lParam;
        SetWindowLongPtr(hwnd, DWLP_USER, (LONG_PTR)options);
        CheckDlgButton(hwnd, IDC_ADDCONTEXT, IsExplorerContextMenuEnabled());
        CheckDlgButton(hwnd, IDC_SHOWSTATUSBAR, options->fShowStatusbar ? BST_CHECKED : BST_UNCHECKED);
        return TRUE;

    case WM_NOTIFY:
        notification = (PSHNOTIFY *)lParam;
        if (notification->hdr.code == PSN_SETACTIVE)
            OptionsPageActivated(hwnd);

        if (notification->hdr.code == PSN_APPLY)
        {
            enabled = IsDlgButtonChecked(hwnd, IDC_ADDCONTEXT) == BST_CHECKED;
            if (enabled != IsExplorerContextMenuEnabled() && !SetExplorerContextMenu(enabled))
            {
                MessageBox(
                    hwnd,
                    _T("The Explorer context menu could not be updated. Your other changes have not been applied."),
                    APP_TITLE, MB_OK | MB_ICONERROR);
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE);
                return TRUE;
            }

            options->fShowStatusbar = IsDlgButtonChecked(hwnd, IDC_SHOWSTATUSBAR) == BST_CHECKED;
            SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }

    return FALSE;
}
