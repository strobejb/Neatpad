//
//	TextView public interface - include this file
//	when you want a TextView edit control
//
#ifndef TEXTVIEW_INCLUDED
#define TEXTVIEW_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

//
//	TextView API declared here
//
BOOL InitTextView();
HWND CreateTextView(HWND hwndParent);

//
//	TextView Window Messages defined here
//
#define TXM_BASE         (WM_USER)
#define TXM_OPENFILE     (TXM_BASE + 0)
#define TXM_CLEAR        (TXM_BASE + 1)

//
//	TextView Message Macros defined here
//
#define TextView_OpenFile(hwndTV, szFile)	SendMessage((hwndTV), TXM_OPENFILE, 0, (LPARAM)(szFile))
#define TextView_Clear(hwndTV)				SendMessage((hwndTV), TXM_CLEAR, 0, 0)

//
//	TextView Macros defined here
//
#define TEXTVIEW_CLASS _T("TextView32")

//
//	TextView colours
//
#define TXC_BACKGROUND		0			// normal background colour
#define TXC_FOREGROUND		1			// normal foreground colour

#define TXC_MAX_COLOURS		2			// keep this updated!

#ifdef __cplusplus
}
#endif

#endif