//
//	TextView's definitions that don't depend on the platform: edit modes,
//	styles, colours and find results. TextView.h (Win32's API) includes this,
//	and so does the engine. It's C, for the C applications that use them.
//
#ifndef TEXTVIEW_DEFS_INCLUDED
#define TEXTVIEW_DEFS_INCLUDED

//
//	TextView edit modes
//
#define MODE_READONLY	0
#define MODE_INSERT		1
#define MODE_OVERWRITE	2

#define TV_MAX_FILTER_CONTEXT 100
#define TV_MAX_TAB_WIDTH 32

#define TVFR_NOTFOUND			0
#define TVFR_FOUND				1
#define TVFR_FOUNDWRAPPED		2

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
#define TXS_INSERTSPACES 64             // typing Tab inserts spaces to the next tab stop
#define TXS_AUTOINDENT 128              // Enter continues the current line's leading whitespace
#define TXS_CHANGEMARKERS 256           // show change outlines and deletion triangles in the selection margin
#define TXS_HIGHLIGHTCHANGES 512        // colour inserted and replaced text

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
#define TXC_CHANGEDTEXT          16          // inserted/replaced text and deletion markers
#define TXC_CHANGEDLINE          17          // changed-line indicator outline
#define TXC_CHANGEDLINEBG        18          // changed-line indicator fill
#define TXC_MAX_COLOURS          19          // keep this updated!

#define SYSCOL(COLOR_IDX)					   ( 0x80000000 |                     COLOR_IDX  )
#define MIXED_SYSCOL(COLOR_IDX1, COLOR_IDX2)   ( 0xC0000000 | (COLOR_IDX2 << 8) | COLOR_IDX1 )
#define MIXED_SYSCOL2(COLOR_IDX1, COLOR_IDX2)  ( 0xE0000000 | (COLOR_IDX2 << 8) | COLOR_IDX1 )

#define SYSCOLIDX(COLREF)   ( 0x00FFFFFF & COLREF )

#endif
