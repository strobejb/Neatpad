//
//	Neatpad
//	OptionsMisc.c
//
//	www.catch22.net
//
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include "Neatpad.h"
#include "resource.h"

//
//	Dialogbox procedure for the FONT pane
//
BOOL CALLBACK MiscOptionsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	PSHNOTIFY *pshn;

	switch(msg)
	{
	case WM_INITDIALOG:
		return TRUE;

	case WM_CLOSE:
		return TRUE;

	case WM_NOTIFY:

		pshn = (PSHNOTIFY *)lParam;
		
		if(pshn->hdr.code == PSN_APPLY)
		{
			return TRUE;
		}

		return FALSE;

	case WM_COMMAND:

		switch(LOWORD(wParam))
		{
		case IDCANCEL:
			return TRUE;
		}

		return FALSE;
	}

	return FALSE;
}
