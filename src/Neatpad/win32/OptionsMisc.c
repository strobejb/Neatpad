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
    BOOL       enabled;

    switch (msg)
    {
    case WM_INITDIALOG:
        CheckDlgButton(hwnd, IDC_ADDCONTEXT, IsExplorerContextMenuEnabled());
        return TRUE;

    case WM_NOTIFY:
        notification = (PSHNOTIFY *)lParam;
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

            SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }

    return FALSE;
}
