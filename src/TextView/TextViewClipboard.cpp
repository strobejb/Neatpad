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

//
//	Paste the clipboard's text
//
BOOL TextView::OnPaste()
{
	if(m_nEditMode == MODE_READONLY)
		return FALSE;

	return m_pHost->ReadClipboard([&](const tchar_t *szText, size_t length)
	{
		ULONG textlen = (ULONG)length;

		EnterText((TCHAR *)szText, textlen);

		if(textlen > 1)
			m_pTextDoc->undo_group_break();
	}) ? TRUE : FALSE;
}

//
//	Copy the currently selected text to the clipboard
//
BOOL TextView::OnCopy()
{
	TextCoord	selStart, selEnd;
	size_w		sellen;

	if(!GetSelection(&selStart, &selEnd))
		return FALSE;

	// a byte never decodes to more than one UTF-16 unit, so this is always enough room
	sellen = selEnd - selStart;

	// reader.read takes a ULONG, and (sellen + 1) TCHARs must not overflow the
	// allocation size (a 32-bit SIZE_T on Win32 wraps at 2GB of text)
	if(sellen > (size_w)ULONG_MAX || sellen >= (size_w)((SIZE_T)-1 / sizeof(TCHAR)))
		return FALSE;

	ULONG len = (ULONG)sellen;

	return m_pHost->WriteClipboard(len, [&](tchar_t *ptr) -> size_t
	{
		TextReader reader = m_pTextDoc->text_from_range(selStart, selEnd);

		return reader.read(ptr, len);
	}) ? TRUE : FALSE;
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

