//
//	MODULE:		TextViewClipboard.cpp
//
//	PURPOSE:	Basic clipboard support for TextView
//				Just uses GetClipboardData/SetClipboardData until I migrate
//				to the OLE code from my drag+drop tutorials
//
//	NOTES:		www.catch22.net
//

#define STRICT
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <tchar.h>
#include "TextView.h"
#include "TextViewInternal.h"

#ifdef UNICODE
#define CF_TCHARTEXT CF_UNICODETEXT
#else
#define CF_TCHARTEXT CF_TEXT
#endif

//
//	Paste any CF_TEXT/CF_UNICODE text from the clipboard
//
BOOL TextView::OnPaste()
{
	BOOL success = FALSE;

	if(m_nEditMode == MODE_READONLY)
		return FALSE;

	if(OpenClipboard(m_hWnd))
	{
		HANDLE hMem		= GetClipboardData(CF_TCHARTEXT);
		TCHAR *szText	= (TCHAR *)GlobalLock(hMem);

		if(szText)
		{
			ULONG textlen = lstrlen(szText);
			EnterText(szText, textlen);

			if(textlen > 1)
				m_pTextDoc->undo_group_break();

			GlobalUnlock(hMem);
			
			success = TRUE;
		}

		CloseClipboard();
	}

	return success;
}

//
//	Copy the currently selected text to the clipboard as CF_TEXT/CF_UNICODE
//
BOOL TextView::OnCopy()
{
	TextCoord	selStart, selEnd;
	size_w		sellen;
	BOOL		success		= FALSE;

	if(!GetSelection(&selStart, &selEnd))
		return FALSE;

	// a byte never decodes to more than one UTF-16 unit, so this is always enough room
	sellen = selEnd.byte_anchor - selStart.byte_anchor;

	// reader.read takes a ULONG, and (sellen + 1) TCHARs must not overflow the
	// allocation size (a 32-bit SIZE_T on Win32 wraps at 2GB of text)
	if(sellen > (size_w)ULONG_MAX || sellen >= (size_w)((SIZE_T)-1 / sizeof(TCHAR)))
		return FALSE;

	if(OpenClipboard(m_hWnd))
	{
		HANDLE hMem;
		TCHAR  *ptr;
		ULONG  len = (ULONG)sellen;

		if((hMem = GlobalAlloc(GPTR, ((SIZE_T)len + 1) * sizeof(TCHAR))) != 0)
		{
			if((ptr = (TCHAR *)GlobalLock(hMem)) != 0)
			{
				TextReader reader = m_pTextDoc->text_from_range(&selStart, &selEnd);

				EmptyClipboard();

				ptr[reader.read(ptr, len)] = 0;

				SetClipboardData(CF_TCHARTEXT, hMem);
				success = TRUE;

				GlobalUnlock(hMem);
			}
		}

		CloseClipboard();
	}

	return success;
}

//
//	Remove current selection and copy to the clipboard
//
BOOL TextView::OnCut()
{
	BOOL success = FALSE;

	if(m_nEditMode == MODE_READONLY)
		return FALSE;

	if(SelectionSize() > 0)
	{
		// copy selected text to clipboard then erase current selection
		success = OnCopy();
		success = success && ForwardDelete();
	}

	return success;
}

//
//	Remove the current selection
//
BOOL TextView::OnClear()
{
	BOOL success = FALSE;

	if(m_nEditMode == MODE_READONLY)
		return FALSE;

	if(SelectionSize() > 0)
	{
		ForwardDelete();
		success = TRUE;
	}

	return success;
}

