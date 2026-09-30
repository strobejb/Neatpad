//
//	MODULE:		TextViewClipboard.cpp
//
//	PURPOSE:	Clipboard support for TextView
//
//	NOTES:		www.catch22.net
//

#include <windows.h>
#include <tchar.h>
#include "TextView.h"
#include "TextViewInternal.h"

LONG TextView::OnPaste()
{
	return 0;
}

LONG TextView::OnCopy()
{
	ULONG  selstart = min(m_nSelectionStart, m_nSelectionEnd);
	ULONG  sellen	= SelectionSize();

	if(sellen  == 0)
		return 0;

	if(OpenClipboard(m_hWnd))
	{
		HANDLE hMem	= GlobalAlloc(GPTR, sellen * sizeof(TCHAR));

		EmptyClipboard();

		TextIterator itor = m_pTextDoc->iterate(selstart);
		itor.gettext((TCHAR *)hMem, sellen);

		SetClipboardData(CF_UNICODETEXT, hMem);
		CloseClipboard();
	}

	return 0;
}

LONG TextView::OnCut()
{
	return 0;
}