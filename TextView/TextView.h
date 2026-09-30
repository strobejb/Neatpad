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
#define TXM_BASE				 (WM_USER)
#define TXM_OPENFILE			 (TXM_BASE + 0)
#define TXM_CLEAR				 (TXM_BASE + 1)
#define TXM_SETLINESPACING		 (TXM_BASE + 2)
#define TXM_ADDFONT				 (TXM_BASE + 3)
#define TXM_SETCOLOR			 (TXM_BASE + 5)


//
//	TextView Message Macros defined here
//
#define TextView_OpenFile(hwndTV, szFile)	\
	SendMessage((hwndTV), TXM_OPENFILE, 0, (LPARAM)(szFile))

#define TextView_Clear(hwndTV)	\
	SendMessage((hwndTV), TXM_CLEAR, 0, 0)

#define TextView_SetLineSpacing(hwndTV, nAbove, nBelow) \
	SendMessage((hwndTV), TXM_SETLINESPACING, (int)(nAbove), (int)(nBelow))

#define TextView_AddFont(hwndTV, hFont) \
	SendMessage((hwndTV), TXM_ADDFONT, (WPARAM)(HFONT)(hFont), 0)

#define TextView_SetColor(hwndTV, nIdx, rgbColor) \
	SendMessage((hwndTV), TXM_SETCOLOR, (WPARAM)(nIdx), (LPARAM)(rgbColor))

//
//	TextView Macros defined here
//
#define TEXTVIEW_CLASS _T("TextView32")

//
//	TextView colours
//
#define TXC_FOREGROUND			0			// normal foreground colour
#define TXC_BACKGROUND			1			// normal background colour
#define TXC_HIGHLIGHTTEXT		2			// normal text highlight colour
#define TXC_HIGHLIGHT			3			// normal background highlight colour
#define TXC_HIGHLIGHTTEXT2		4			// inactive text highlight colour
#define TXC_HIGHLIGHT2			5			// inactive background highlight colour

#define TXC_MAX_COLOURS			6			// keep this updated!

#define SYSCOL(COLOR_IDX)   ( 0x80000000 | COLOR_IDX)
#define SYSCOLIDX(COLREF)   (~0x80000000 & COLREF)

#define REALIZE_SYSCOL(col) (col & 0x80000000 ? GetSysColor(col & ~0x80000000) : col)

#ifdef __cplusplus
}
#endif

#endif