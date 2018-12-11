#ifndef USPLIB_INCLUDED
#define USPLIB_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#include <usp10.h>
#pragma comment(lib, "usp10.lib")

//
//	ATTR - use to define the attributes of a single run of text
//
typedef struct _ATTR
{
	COLORREF	fg;
	COLORREF	bg;
	int			len			: 16;		// length of this run (in code-units)
	int			font		: 7;		// font-index
	int			sel			: 1;		// selection flag (yes/no)
	int			ctrl		: 1;		// show as an isolated control-character
	int			eol			: 1;		// when set, prevents cursor from selecting character. only valid for last char in line
	int			reserved	: 6;		// possible underline/other styles (must be NULL)

} ATTR, *PATTR;

//
//	USPFONT - use to provide UspAnalyze with font. 
//	Only the yoffset member can be modified by the caller - after
//  UspInitFont has been called
//
typedef struct _USPFONT
{
	HFONT			hFont;				// handle to FONT
	SCRIPT_CACHE	scriptCache;		// must be initialized to NULL
	TEXTMETRIC		tm;					//
	int				yoffset;			// height-adjustment when drawing font (set to zero)

	// reserved for internal use
	int				capheight;			// height of capital letters
	int				numheight;			// height of numeric characters
	int				xborder;
	int				yborder;

} USPFONT, *PUSPFONT;

//
//	ITEM_RUN - private to USPLIB
//
typedef struct _ITEM_RUN
{
	SCRIPT_ANALYSIS		analysis;
	int					charPos;
	int					len;			// length of run in WCHARs
	int					font;			// font index
	
	int					width;			// total width in pixels of this run
	int					glyphPos;		// position within USPDATA's lists
	int					glyphCount;		// number of glyphs in this run
	
	int					chcode	 : 21;	// codepoint (only used when ::ctrl is TRUE)
	int					tab		 : 1;	// run is a single tab-character
	int					ctrl	 : 1;	// run is a single control-character
	int					eol		 : 1;	// prevents cursor from selecting character (only sensible for last char)
	int					selstate : 2;	// whole run selection state (0=none, 1=all, 2=partial)

} ITEM_RUN, *PITEM_RUN;


//
//	USPDATA - opaque data structure, do not assume
//	anything about the internal layout of this structure
//
typedef struct _USPDATA
{
	//
	//	Array of merged SCRIPT_ITEM runs 
	//
	int				  itemRunCount;
	int				  itemRunAllocLen;			
	ITEM_RUN		* itemRunList;
	int				* visualToLogicalList;
	BYTE			* bidiLevels;				

	SCRIPT_ITEM		* tempItemList;			// only used during string-analysis
	int				  tempItemAllocLen;

	//
	//	Logical character/cluster information (1 unit per original WCHAR)
	//
	int				  stringLen;			// length of current string (in WCHARs)
	int				  stringAllocLen;		// actual allocation count of string and arrays:
	WORD			* clusterList;			// logical cluster info
	ATTR			* attrList;				// flattened attribute-list
	SCRIPT_LOGATTR	* breakList;			// holds results of script-break

	//
	//	Glyph information for the entire paragraph
	//	Each ITEM_RUN references a position within these lists:
	//
	int				  glyphCount;			// count of glyphs currently stored
	int				  glyphAllocLen;		// actual allocated length of buffers
	WORD			* glyphList;
	int				* widthList;
	GOFFSET			* offsetList;
	SCRIPT_VISATTR	* svaList;

	SIZE			  size;

	//	colours of the selection-highlight
	COLORREF		  selFG;
	COLORREF		  selBG;

	//	stored flags from UspAnalyze
	UINT			  analyzeFlags;
	
	//
	//	external, user-maintained font-table
	//
	USPFONT			* uspFontList;
	USPFONT			  defaultFont;		// if no user-list

} USPDATA, *PUSPDATA;

//
//	UspAnalyze flags
//
#define USP_CTLCHR_DEC			0x10
#define USP_CTLCHR_HEX			0x20
#define USP_CTLCHR_ASC			0x40


USPDATA * WINAPI UspAllocate (
	);

VOID WINAPI UspFree (	
		USPDATA *uspData 
	);

BOOL WINAPI UspAnalyze (	
		USPDATA			* uspData,
		HDC				  hdc,
		WCHAR			* wstr, 
		int				  wlen, 
		ATTR			* attrRunList,
		UINT			  flags,
		USPFONT			* uspFont,
		SCRIPT_CONTROL	* scriptControl,
		SCRIPT_STATE	* scriptState,
		SCRIPT_TABDEF   * scriptTabDef
	);

VOID WINAPI UspApplyAttributes  (	
		USPDATA	  *	uspData, 
		ATTR      *	attrRunList
	);

VOID WINAPI UspApplySelection (	
		USPDATA   *	uspData, 
		int			selStart,
		int			selEnd
	);

int WINAPI UspTextOut (	
		USPDATA  *  uspData,
		HDC			hdc, 
		int			xpos, 
		int			ypos, 
		int			lineHeight,
		int			lineAdjustY,
		RECT	 *	rect
	);

BOOL WINAPI UspSnapXToOffset (	
		USPDATA	  * uspData,		
		int			xpos,			
		int       * snappedX,		// out, optional
		int       * charPos,		// out
		BOOL	  * fRTL			// out, optional
	);

BOOL WINAPI UspXToOffset (	
		USPDATA	  * uspData,		
		int			xpos,			
		int       * charPos,		// out
		BOOL	  * trailing,		// out
		BOOL	  * fRTL			// out, optional
	);

BOOL WINAPI UspOffsetToX (	
		USPDATA  *	uspData, 
		int			offset, 
		BOOL		trailing, 
		int		  *	xpos			// out
	);

VOID WINAPI UspSetSelColor (
		USPDATA   *	uspData, 
		COLORREF	fg, 
		COLORREF	bg
	);

VOID WINAPI UspInitFont	(	
		USPFONT   *	uspFont,		// in/out
		HDC			hdc,			// in
		HFONT	    hFont			// in
	);

VOID WINAPI UspFreeFont (	
		USPFONT   *	uspFont
	);

BOOL WINAPI UspGetSize (
		USPDATA * uspData,
		SIZE    * size
	);

BOOL WINAPI UspBuildAttr (
		ATTR	  *	attr,
		COLORREF    colfg,	
		COLORREF    colbg,
		int			len,
		int			font,
		int			sel,
		int			ctrl,
		int			eol
	);

SCRIPT_LOGATTR * WINAPI UspGetLogAttr (	
		USPDATA			* uspData
	);

#ifdef __cplusplus
}
#endif

#endif