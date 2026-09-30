#ifndef USPLIB_PRIVATE_INCLUDED
#define USPLIB_PRIVATE_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#include <usp10.h>
#pragma comment(lib, "usp10.lib")

typedef int (* MEASURE_GLYPH_PROC)(int glyphIdx);
typedef int (* DRAW_GLYPH_PROC)(HDC hdc, int xpos, int ypos, RECT *rect, int glyphIdx);

typedef struct _ATTR
{
	COLORREF	fg;
	COLORREF	bg;
	int			len			: 16;		// length of this run (in code-units)
	int			font		: 7;		// font-index
	int			sel			: 1;		// selection flag (yes/no)
	int			ctrl		: 1;		// show as an isolated control-character
	int			eol			: 1;		// when set, prevents cursor from selecting character. only valid for last char in line
	int			style		: 5;		// possible underline/other styles
	int			reserved	: 1;		// reserved for future use

} ATTR, *PATTR;

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
	WORD			* visclusterList;		// visual-order cluster info
	ATTR			* vattrList;			// visual-order flattened attribute-list

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

	BOOL			  valid;
	COLORREF		  selFG;
	COLORREF		  selBG;
	SCRIPT_TABDEF	  tabdef;

	//	stored flags from USP_Analyze
	UINT			  analyzeFlags;
	
	//
	//	external, user-maintained font-table
	//
	USPFONT			* uspFontList;
	USPFONT			  defaultFont;		// if no user-list

} USPDATA, *PUSPDATA;

//
//	USP_Analyze flags
//
#define USP_CTLCHR_DEC			0x10
#define USP_CTLCHR_HEX			0x20
#define USP_CTLCHR_ASC			0x40


USPDATA * USP_Allocate	  ( );
void	  USP_Free		  (	USPDATA *uspData );

BOOL USP_Analyze		  (	USPDATA	  * uspData,
							HDC			hdc,
							WCHAR	  * wstr, 
							int			wlen, 
							ATTR	  * attrRunList,
							UINT		flags,
							USPFONT	  *	uspFont
						  );


void USP_ApplyAttributes  (	USPDATA	  *	uspData, 
							ATTR      *	attrRunList
					  	  );

void USP_ApplySelection	  (	USPDATA   *	uspData, 
							int			selStart,
							int			selEnd
						  );

int USP_TextOut			  (	USPDATA  *  uspData,
							HDC			hdc, 
							int			xpos, 
							int			ypos, 
							RECT	 *	rect
						  );

BOOL USP_SnapXToOffset	  (	USPDATA	  * uspData,		
							int			mouseX,			
							int       * snappedX,		// out, optional
							int       * charPos,		// out
							BOOL	  * fRTL			// out, optional
						  );

BOOL USP_XToOffset		  (	USPDATA	  * uspData,		
							int			mouseX,			
							int       * charPos,		// out
							BOOL	  * trailing,		// out
							BOOL	  * fRTL			// out, optional
						  );

BOOL USP_OffsetToX		  (	USPDATA  *	uspData, 
							int			offset, 
							BOOL		trailing, 
							int		  *	xpos			// out
						  );

BOOL USP_Invalid		  (	USPDATA   *	uspData );

void USP_Invalidate		  (	USPDATA   *	uspData );

void USP_SetSelColor	  (	USPDATA   *	uspData, 
							COLORREF	fg, 
							COLORREF	bg
						  );

void USP_InitFont		  (	USPFONT   *	uspFont,		// in/out
							HDC			hdc,			// in
							HFONT	    hFont			// in
						  );

void USP_FreeFont		(	USPFONT   *	uspFont
						  );

const SIZE *			USP_pSize	 ( USPDATA * uspData );

// coming soon
const SCRIPT_LOGATTR *  USP_pLogAttr ( USPDATA * uspData );

#ifdef __cplusplus
}
#endif

#endif