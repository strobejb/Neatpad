//
//	TextView public interface - include this file
//	when you want a TextView edit control
//
#ifndef TEXTVIEW_INCLUDED
#define TEXTVIEW_INCLUDED

#include "codepages.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef UNICODE
#pragma message("Warning: UNICODE is not defined\n")
#endif

//
//	TextView API declared here
//
BOOL	 InitTextView();
HWND	 CreateTextView(HWND hwndParent);
COLORREF RealizeColour(COLORREF col);


//
//	TextView edit modes
//
#define MODE_READONLY	0
#define MODE_INSERT		1
#define MODE_OVERWRITE	2

//
//	TextView Window Messages defined here
//
#define TXM_BASE				(WM_USER)
#define TXM_OPENFILE			(TXM_BASE + 0)
#define TXM_CLEAR				(TXM_BASE + 1)
#define TXM_SETLINESPACING		(TXM_BASE + 2)
#define TXM_ADDFONT				(TXM_BASE + 3)
#define TXM_SETCOLOR			(TXM_BASE + 4)
#define TXM_GETCOLOR			(TXM_BASE + 5)
#define TXM_SETSTYLE			(TXM_BASE + 6)
#define TXM_GETSTYLE			(TXM_BASE + 7)
#define TXM_SETCARETWIDTH		(TXM_BASE + 8)
#define TXM_SETIMAGELIST		(TXM_BASE + 9)
#define TXM_SETLONGLINE			(TXM_BASE + 10)
#define TXM_SETLINEIMAGE		(TXM_BASE + 11)
#define TXM_GETFORMAT			(TXM_BASE + 12)
#define TXM_UNDO				(TXM_BASE + 13)
#define TXM_REDO				(TXM_BASE + 14)
#define TXM_CANUNDO				(TXM_BASE + 15)
#define TXM_CANREDO				(TXM_BASE + 16)
#define TXM_GETSELSIZE			(TXM_BASE + 17)
#define TXM_SETSELALL			(TXM_BASE + 18)
#define TXM_GETCURPOS			(TXM_BASE + 19)
#define TXM_GETCURLINE			(TXM_BASE + 20)
#define TXM_GETCURCOL			(TXM_BASE + 21)
#define TXM_SETEDITMODE			(TXM_BASE + 22)
#define TXM_GETEDITMODE			(TXM_BASE + 23)
#define TXM_SETCONTEXTMENU		(TXM_BASE + 24)
#define TXM_GETCURLINEKNOWN		(TXM_BASE + 25)
#define TXM_GETLINEFORMAT		(TXM_BASE + 26)
#define TXM_ADDFONTFALLBACK		(TXM_BASE + 27)
#define TXM_CLEARFONTFALLBACKS	(TXM_BASE + 28)
#define TXM_GETDOCSTATS			(TXM_BASE + 29)
#define TXM_GETCURSORINFO		(TXM_BASE + 30)
#define TXM_FINDTEXT			(TXM_BASE + 31)
#define TXM_SAVEFILE			(TXM_BASE + 32)
#define TXM_GETBOM				(TXM_BASE + 33)
#define TXM_INDEXFILE			(TXM_BASE + 34)
#define TXM_GETINDEXINFO		(TXM_BASE + 35)
#define TXM_GOTOLINE			(TXM_BASE + 36)
#define TXM_CANGOTOLINE			(TXM_BASE + 37)
#define TXM_CONVERTLINEBREAKS	(TXM_BASE + 38)
#define TXM_SETINMEMORYLIMIT	(TXM_BASE + 39)
#define TXM_TAILFILE			(TXM_BASE + 40)
#define TXM_UPDATETAIL			(TXM_BASE + 41)
#define TXM_REPLACETEXT			(TXM_BASE + 42)
#define TXM_SHOWMATCHES			(TXM_BASE + 43)

//
//	TextView Notification Messages defined here - 
//	sent via the WM_NOTIFY message
//
#define TVN_BASE				(WM_USER)
#define TVN_CURSOR_CHANGE		(TVN_BASE + 0)
#define TVN_SELECTION_CHANGE	(TVN_BASE + 1)
#define TVN_EDITMODE_CHANGE		(TVN_BASE + 2)
#define TVN_CHANGED				(TVN_BASE + 3)
#define TVN_INDEX_CHANGE		(TVN_BASE + 4)

//
//	The caret position: sent with TVN_CURSOR_CHANGE, and filled by TXM_GETCURSORINFO.
//	The line number is an estimate past the counted part of a large file, and
//	in UTF-8 and UTF-32 the document offset is only known over the counted part.
//
typedef struct _TVNCURSORINFO
{
	NMHDR			 hdr;
	unsigned __int64 nLineNo;
	unsigned __int64 nColumnNo;			// UTF-16 units from the start of the line
	unsigned __int64 nOffset;			// UTF-16 units from the start of the document
	BOOL			 fLineNoKnown;
	BOOL			 fOffsetKnown;
} TVNCURSORINFO;

//
//	How far indexing has got: sent with TVN_INDEX_CHANGE, and filled by
//	TXM_GETINDEXINFO. Line numbers are exact over the indexed part of the
//	document, which starts at the top. Opening a file indexes its first 4 MB;
//	TXM_INDEXFILE indexes the rest in the background, when asked.
//
typedef struct _TVNINDEXINFO
{
	NMHDR			 hdr;
	UINT			 nPercent;			// of the document indexed: 100 once all of it is
	BOOL			 fIndexing;			// indexing is running
	unsigned __int64 nLineCount;		// exact once nPercent is 100, an estimate before
} TVNINDEXINFO;

typedef struct _TEXTVIEWSTATS
{
	unsigned __int64 byte_count;
	unsigned __int64 char_count;
	BOOL			 char_count_known;	// also covers sel_char_count
	unsigned __int64 sel_byte_count;	// 0 when nothing is selected
	unsigned __int64 sel_char_count;
	UINT			 linebreaks_seen;	// TXL_* kinds of line break found so far (a large file is counted lazily)
} TEXTVIEWDOCSTATS;

#define TVFR_NOTFOUND			0
#define TVFR_FOUND				1
#define TVFR_FOUNDWRAPPED		2

typedef struct _TEXTVIEWFIND
{
	TCHAR *text;
	UINT length;
	BOOL backwards;
	BOOL wrap;
	BOOL matchcase;
	TCHAR *replace;			// for TextView_Replace: what goes in place of a match
	UINT replacelength;
} TEXTVIEWFIND;

//
//	TextView Window Styles defined here
//	(set using TXM_SETSTYLE)
//
#define TXS_SELMARGIN			1
#define TXS_LINENUMBERS			2
#define TXS_TREECTRL			4
#define TXS_LONGLINES			8
#define TXS_HIGHLIGHTCURLINE	16
#define TXS_SHOWLINEENDINGS		32		// show every line ending, not just those that don't match the line format

//
//	End-of-line mode: the TXL_ line break kinds come from codepages.h,
//	shared with TextDocument
//

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
#define TXC_SELMARGIN1			6			// selection margin colour#1
#define TXC_SELMARGIN2			7			// selection margin colour#2
#define TXC_LINENUMBERTEXT		8			// line number text
#define TXC_LINENUMBER			9			// line number background
#define TXC_LONGLINETEXT		10			// long-line text
#define TXC_LONGLINE			11			// long-line background
#define TXC_CURRENTLINETEXT		12			// active line text
#define TXC_CURRENTLINE			13			// active line background
#define TXC_SEARCHMATCHTEXT		14			// search match text, as TextView_ShowMatches shows them
#define TXC_SEARCHMATCH			15			// search match background
#define TXC_MAX_COLOURS			16			// keep this updated!

#define SYSCOL(COLOR_IDX)					   ( 0x80000000 |                     COLOR_IDX  )
#define MIXED_SYSCOL(COLOR_IDX1, COLOR_IDX2)   ( 0xC0000000 | (COLOR_IDX2 << 8) | COLOR_IDX1 )
#define MIXED_SYSCOL2(COLOR_IDX1, COLOR_IDX2)  ( 0xE0000000 | (COLOR_IDX2 << 8) | COLOR_IDX1 )

#define SYSCOLIDX(COLREF)   ( 0x00FFFFFF & COLREF )
#define REALIZE_SYSCOL(col) (RealizeColour(col))


//
//	TextView Message Macros defined here
//
//	A file another program is writing opens read-only: TextView_GetEditMode says
//	MODE_READONLY.
#define TextView_OpenFile(hwndTV, szFile)	\
	SendMessage((hwndTV), TXM_OPENFILE, 0, (LPARAM)(TCHAR *)(szFile))

//	Open a file to follow as another program appends to it: read-only, with the
//	caret at the end. TextView_UpdateTail adds what has been appended since, and a
//	caret at the end stays there. It's FALSE once the file has got shorter: it's
//	been rewritten, and needs opening again.
#define TextView_TailFile(hwndTV, szFile)	\
	((BOOL)SendMessage((hwndTV), TXM_TAILFILE, 0, (LPARAM)(TCHAR *)(szFile)))

#define TextView_UpdateTail(hwndTV)	\
	((BOOL)SendMessage((hwndTV), TXM_UPDATETAIL, 0, 0))

//	Put find->replace in place of the match the selection holds - as
//	TextView_FindText leaves it - with the caret after it. fAll replaces every
//	match in the document instead, as one undo step. Returns how many it replaced.
#define TextView_Replace(hwndTV, find, fAll)	\
	((size_t)SendMessage((hwndTV), TXM_REPLACETEXT, (WPARAM)(fAll), (LPARAM)(TEXTVIEWFIND *)(find)))

//	Highlight every match of find->text (matching case if find->matchcase asks)
//	wherever the text is shown, in the TXC_SEARCHMATCH colours - or none, for 0.
//	fFilter shows only the lines holding a match, read-only, found in the
//	background in a large document. Filtering stops with the caret on the line
//	it was on, in the whole document. Meanwhile, TextView_GetEditMode, saving
//	and the document's details are still the whole document's.
#define TextView_ShowMatches(hwndTV, find, fFilter)	\
	SendMessage((hwndTV), TXM_SHOWMATCHES, (WPARAM)(fFilter), (LPARAM)(TEXTVIEWFIND *)(find))

//	Save in nEncoding (NCP_*), with or without its byte-order mark, and with every
//	line break made nLineBreak (TXL_CR, TXL_LF or TXL_CRLF; 0 keeps them).
//	Converting reopens the file, which starts the undo history again.
#define TextView_SaveFile(hwndTV, szFile, nEncoding, fBom, nLineBreak)	\
	((BOOL)SendMessage((hwndTV), TXM_SAVEFILE, \
		MAKEWPARAM(MAKEWORD((nEncoding), (nLineBreak)), (fBom) ? 1 : 0), (LPARAM)(TCHAR *)(szFile)))

#define TextView_Clear(hwndTV)	\
	SendMessage((hwndTV), TXM_CLEAR, 0, 0)

#define TextView_SetLineSpacing(hwndTV, nAbove, nBelow) \
	SendMessage((hwndTV), TXM_SETLINESPACING, (int)(nAbove), (int)(nBelow))

#define TextView_AddFont(hwndTV, hFont) \
	SendMessage((hwndTV), TXM_ADDFONT, (WPARAM)(HFONT)(hFont), 0)

#define TextView_AddFontFallback(hwndTV, nSlot, hFont) \
	SendMessage((hwndTV), TXM_ADDFONTFALLBACK, (WPARAM)(nSlot), (LPARAM)(HFONT)(hFont))

#define TextView_ClearFontFallbacks(hwndTV, nSlot) \
	SendMessage((hwndTV), TXM_CLEARFONTFALLBACKS, (WPARAM)(nSlot), 0)

#define TextView_SetColor(hwndTV, nIdx, rgbColor) \
	SendMessage((hwndTV), TXM_SETCOLOR, (WPARAM)(nIdx), (LPARAM)(rgbColor))

#define TextView_SetStyle(hwndTV, uMask, uStyles) \
	SendMessage((hwndTV), TXM_SETSTYLE, (WPARAM)(uMask), (LPARAM)(uStyles))

#define TextView_SetStyleBool(hwndTV, uStyle, fBoolean) \
	SendMessage((hwndTV), TXM_SETSTYLE, (WPARAM)(uStyle), (LPARAM)(fBoolean ? uStyle : 0))
	
#define TextView_SetCaretWidth(hwndTV, nWidth) \
	SendMessage((hwndTV), TXM_SETCARETWIDTH, (WPARAM)(nWidth), 0)

#define TextView_SetImageList(hwndTV, hImgList) \
	SendMessage((hwndTV), TXM_SETIMAGELIST, (WPARAM)(HIMAGELIST)(hImgList), 0)

#define TextView_SetLongLine(hwndTV, nLength) \
	SendMessage((hwndTV), TXM_SETLONGLINE, (WPARAM)(0), (LPARAM)(nLength))

#define TextView_SetLineImage(hwndTV, nLineNo, nImageIdx) \
	SendMessage((hwndTV), TXM_SETLINEIMAGE, (WPARAM)(ULONG)(nLineNo), (LPARAM)(ULONG)nImageIdx)

#define TextView_GetFormat(hwndTV) \
	((UINT)SendMessage((hwndTV), TXM_GETFORMAT, 0, 0))

// whether the file has a byte-order mark: as opened, then as last saved
#define TextView_GetBom(hwndTV) \
	((BOOL)SendMessage((hwndTV), TXM_GETBOM, 0, 0))

#define TextView_GetLineFormat(hwndTV) \
	((UINT)SendMessage((hwndTV), TXM_GETLINEFORMAT, 0, 0))

//	Make every line break in the selection, or the whole document without one,
//	TXL_CRLF, TXL_LF or TXL_CR, as one undo step. The whole document takes that
//	line format too. Converting to TextView_GetLineFormat's own format
//	normalizes: stray line breaks come to match the rest.
#define TextView_ConvertLineBreaks(hwndTV, nLineBreak) \
	((BOOL)SendMessage((hwndTV), TXM_CONVERTLINEBREAKS, (WPARAM)(nLineBreak), 0))

// Files opened from now on that are shorter than nBytes are read into memory and
// closed, so others can write them. Longer ones are read from disk as they're
// needed, and nothing else can write them while they're open. 0, to start
// with, reads every file from disk.
#define TextView_SetInMemoryLimit(hwndTV, nBytes) \
	SendMessage((hwndTV), TXM_SETINMEMORYLIMIT, (WPARAM)(nBytes), 0)

#define TextView_Undo(hwndTV) \
	SendMessage((hwndTV), TXM_UNDO, 0, 0)

#define TextView_Redo(hwndTV) \
	SendMessage((hwndTV), TXM_REDO, 0, 0)

#define TextView_CanUndo(hwndTV) \
	((BOOL)SendMessage((hwndTV), TXM_CANUNDO, 0, 0))

#define TextView_CanRedo(hwndTV) \
	((BOOL)SendMessage((hwndTV), TXM_CANREDO, 0, 0))

#define TextView_GetModify(hwndTV) \
	((BOOL)SendMessage((hwndTV), EM_GETMODIFY, 0, 0))

#define TextView_GetSelSize(hwndTV) \
	((UINT_PTR)SendMessage((hwndTV), TXM_GETSELSIZE, 0, 0))

#define TextView_SelectAll(hwndTV) \
	SendMessage((hwndTV), TXM_SETSELALL, 0, 0)

#define TextView_GetCurPos(hwndTV) \
	((UINT_PTR)SendMessage((hwndTV), TXM_GETCURPOS, 0, 0))

#define TextView_GetCurLine(hwndTV) \
	((UINT_PTR)SendMessage((hwndTV), TXM_GETCURLINE, 0, 0))

#define TextView_GetCurLineKnown(hwndTV) \
	SendMessage((hwndTV), TXM_GETCURLINEKNOWN, 0, 0)

#define TextView_GetCurCol(hwndTV) \
	((UINT_PTR)SendMessage((hwndTV), TXM_GETCURCOL, 0, 0))

#define TextView_SetEditMode(hwndTV, nEditMode) \
	((UINT)SendMessage((hwndTV), TXM_SETEDITMODE, (WPARAM)(nEditMode), 0))

#define TextView_GetEditMode(hwndTV) \
	((UINT)SendMessage((hwndTV), TXM_GETEDITMODE, 0, 0))

#define TextView_GetDocStats(hwndTV, stats) \
	((BOOL)SendMessage((hwndTV), TXM_GETDOCSTATS, 0, (LPARAM)(TEXTVIEWDOCSTATS *)(stats)))

#define TextView_GetCursorInfo(hwndTV, info) \
	((BOOL)SendMessage((hwndTV), TXM_GETCURSORINFO, 0, (LPARAM)(TVNCURSORINFO *)(info)))

// Start (fIndex TRUE) or stop indexing the rest of the document in the background
#define TextView_IndexFile(hwndTV, fIndex) \
	SendMessage((hwndTV), TXM_INDEXFILE, (WPARAM)(BOOL)(fIndex), 0)

#define TextView_GetIndexInfo(hwndTV, info) \
	((BOOL)SendMessage((hwndTV), TXM_GETINDEXINFO, 0, (LPARAM)(TVNINDEXINFO *)(info)))

//	Move the caret to the start of a line, counted from 0: TDR_OK, TDR_PENDING_INDEX
//	for a line past the indexed part of the document (index further, then try
//	again), or TDR_FAILED for one it doesn't have. TextView_CanGotoLine answers
//	the same without moving. The line number travels in both wParam and lParam.
#define TextView_GotoLine(hwndTV, nLineNo) \
	((TEXT_RESULT)SendMessage((hwndTV), TXM_GOTOLINE, \
		(WPARAM)(DWORD)(nLineNo), (LPARAM)(DWORD)((unsigned __int64)(nLineNo) >> 32)))

#define TextView_CanGotoLine(hwndTV, nLineNo) \
	((TEXT_RESULT)SendMessage((hwndTV), TXM_CANGOTOLINE, \
		(WPARAM)(DWORD)(nLineNo), (LPARAM)(DWORD)((unsigned __int64)(nLineNo) >> 32)))

#define TextView_FindText(hwndTV, find) \
	((UINT)SendMessage((hwndTV), TXM_FINDTEXT, 0, (LPARAM)(TEXTVIEWFIND *)(find)))

#define TextView_SetContextMenu(hwndTV, hPopupMenu) \
	SendMessage((hwndTV), TXM_SETCONTEXTMENU, (WPARAM)(hPopupMenu), 0)

#define TextView_SetFont(hwndTV, hFont) \
	SendMessage((hwndTV), WM_SETFONT, (WPARAM)(HFONT)(hFont), 0)

#ifdef __cplusplus
}
#endif

#endif
