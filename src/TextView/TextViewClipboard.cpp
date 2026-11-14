//
//	MODULE:		TextViewClipboard.cpp
//
//	PURPOSE:	Basic clipboard support for TextView
//				Just uses GetClipboardData/SetClipboardData until I migrate
//				to the OLE code from my drag+drop tutorials
//
//	NOTES:		www.catch22.net
//

#include "TextViewInternal.h"

//
//	Paste the clipboard's text
//
bool TextView::OnPaste()
{
	if(m_nEditMode == MODE_READONLY)
		return false;

	return m_pHost->ReadClipboard([&](const tchar_t *szText, size_t length)
	{
		uint32_t textlen = (uint32_t)length;

		EnterText((tchar_t *)szText, textlen);

		if(textlen > 1)
			m_pTextDoc->undo_group_break();
	}) ? true : false;
}

//
//	Copy the currently selected text to the clipboard
//
bool TextView::OnCopy()
{
	TextCoord	selStart, selEnd;
	size_w		sellen;

	if(!GetSelection(&selStart, &selEnd))
		return false;

	// a byte never decodes to more than one UTF-16 unit, or three UTF-8 ones (as
	// U+FFFD), so this is always enough room
	sellen = (selEnd - selStart) * (TCHAR_UTF8 ? 3 : 1);

	// reader.read takes a uint32_t, and (sellen + 1) units must not overflow the
	// allocation size (a 32-bit size_t on Win32 wraps at 2GB of text)
	if(sellen > (size_w)UINT32_MAX || sellen >= (size_w)((size_t)-1 / sizeof(tchar_t)))
		return false;

	uint32_t len = (uint32_t)sellen;

	return m_pHost->WriteClipboard(len, [&](tchar_t *ptr) -> size_t
	{
		TextReader reader = m_pTextDoc->text_from_range(selStart, selEnd);

		return reader.read(ptr, len);
	}) ? true : false;
}

//
//	Remove current selection and copy to the clipboard
//
bool TextView::OnCut()
{
	bool success = false;

	if(m_nEditMode == MODE_READONLY)
		return false;

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
bool TextView::OnClear()
{
	bool success = false;

	if(m_nEditMode == MODE_READONLY)
		return false;

	if(SelectionSize() > 0)
	{
		ForwardDelete();
		success = true;
	}

	return success;
}

