//
//	UspPaint.c
//
//	Contains routines used for the display of styled Unicode text
//	
//	UspTextOut
//	UspInitFont
//	UspFreeFont
//	UspSetSelColor
//	
//	Written by J Brown 2006 Freeware
//	www.catch22.net
//

#define _WIN32_WINNT 0x501

#ifndef _UNICODE
#define _UNICODE
#endif

#ifndef UNICODE
#define UNICODE
#endif

#define STRICT
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <usp10.h>
#include <tchar.h>
#include "usplib.h"

// UspLib.c
ITEM_RUN *GetItemRun(USPDATA *uspData, int visualIdx);

// UspCtrl.c
void	InitCtrlChar(HDC hdc, USPFONT *uspFont);
void	PaintCtrlCharRun(USPDATA *uspData, USPFONT *uspFont, ITEM_RUN *itemRun, HDC hdc, int xpos, int ypos);

//
//	Locate the glyph-index positions for the specified logical character positions
//
static
void GetGlyphClusterIndices (  
		ITEM_RUN	* itemRun, 
		WORD		* clusterList,
		int			  clusterIdx1, 
		int			  clusterIdx2, 
		int			* glyphIdx1, 
		int			* glyphIdx2
	)
{
	// RTL scripts
	if(itemRun->analysis.fRTL)
	{
		*glyphIdx1 = clusterIdx1 < itemRun->len ? clusterList[clusterIdx1] + 1 : 0;
		*glyphIdx2 = clusterList[clusterIdx2];
	}
	// LTR scripts
	else
	{
		*glyphIdx1 = clusterList[clusterIdx2];
		*glyphIdx2 = clusterIdx1 < itemRun->len ? clusterList[clusterIdx1] - 1 : itemRun->glyphCount - 1;
	}
}

//
//	Draw a rectangle in a single colour. Depending on the run-direction (left/right),
//	the rectangle's position may need to be mirrored within the run before output
//
static 
void PaintRectBG(USPDATA *uspData, ITEM_RUN *itemRun, HDC hdc, int xpos, RECT *rect, ATTR *attr)
{
	RECT rc = *rect;
	
	// rectangle must be mirrored within the run for RTL scripts
	if(itemRun->analysis.fRTL)
	{
		int rtlOffset = xpos*2 + itemRun->width - rc.right - rc.left;
		OffsetRect(&rc, rtlOffset, 0);
	}
	
	// draw selection highlight + add to clipping region
	if(attr->sel)
	{
		SetBkColor(hdc, uspData->selBG);
		ExtTextOut(hdc, 0, 0, ETO_OPAQUE|ETO_CLIPPED, &rc, 0, 0, 0);
		ExcludeClipRect(hdc, rc.left, rc.top, rc.right, rc.bottom);
	}
	// draw normal background 
	else
	{
		SetBkColor(hdc, attr->bg);
		ExtTextOut(hdc, 0, 0, ETO_OPAQUE|ETO_CLIPPED, &rc, 0, 0, 0);
	}
}

//
//	Paint a single run's background
//
static 
void PaintItemRunBackground ( 
		USPDATA		* uspData,
		ITEM_RUN	* itemRun,
		HDC			  hdc, 
		int			  xpos, 
		int			  ypos,
		int			  lineHeight,
		RECT		* bounds
	)
{
	int i, lasti;

	RECT rect = { xpos, ypos, xpos, ypos+lineHeight };

	WORD  * clusterList	 = uspData->clusterList		+ itemRun->charPos; 
	ATTR  * attrList	 = uspData->attrList		+ itemRun->charPos;
	int   * widthList	 = uspData->widthList		+ itemRun->glyphPos;

	ATTR attr = attrList[0];

	// loop over the character-positions in the run, in logical order
	for(i = 0, lasti = 0; i <= itemRun->len; i++)
	{
		// search for a cluster boundary (or end of run)
		if(i == itemRun->len || clusterList[lasti] != clusterList[i])
		{
			int clusterWidth;
			int advanceWidth;
			int a;
			int glyphIdx1, glyphIdx2;

			// locate glyph-positions for the cluster
			GetGlyphClusterIndices(itemRun, clusterList, i, lasti, &glyphIdx1, &glyphIdx2);
			
			// measure cluster width
			for(clusterWidth = 0; glyphIdx1 <= glyphIdx2; )
				clusterWidth += widthList[glyphIdx1++];

			// divide the cluster-width by the number of code-points that cover it
			// advanceWidth = clusterWidth / (i - lasti);
			advanceWidth = MulDiv(clusterWidth, 1, i-lasti);
				
			// interpolate the characters-positions (between lasti and i) 
			// over the whole cluster
			for(a = lasti; a <= i; a++)
			{
				// adjust for possible rounding errors in the 
				// last iteration (due to the division)
				if(a == i)
					rect.right += clusterWidth - advanceWidth * (i-lasti);

				// look for change in attribute background
				if(a == itemRun->len ||
				   attr.bg  != attrList[a].bg || 
				   attr.sel != attrList[a].sel)
				{
					PaintRectBG(uspData, itemRun, hdc, xpos, &rect, &attr);
					rect.left = rect.right;
				}

				if(a != i)
				{
					attr = attrList[a];
					rect.right += advanceWidth;
				}
			}

			lasti = i;
		}
	}
}

//
//	Paint the entire line's background
//
static 
void PaintBackground (
		USPDATA   * uspData,
		HDC			hdc, 
		int			xpos, 
		int			ypos, 
		int			lineHeight,
		RECT	  * bounds
	)
{
	int			i;
	ITEM_RUN *	itemRun;

	//
	//	Loop over each item-run in turn
	//
	for(i = 0; i < uspData->itemRunCount; i++)
	{
		//	Generate glyph data for this run
		if((itemRun = GetItemRun(uspData, i)) == 0)
			break;

		// if run is not visible then don't bother displaying it!
		if(xpos + itemRun->width < bounds->left || xpos > bounds->right)
		{
			xpos += itemRun->width;
			continue;
		}

		// paint the background of the specified item-run
		PaintItemRunBackground(
			uspData,
			itemRun,
			hdc,
			xpos,
			ypos,
			lineHeight,
			bounds
 		  );

		xpos += itemRun->width;
	}
}

//
//	PaintItemRunForeground
//
//	Output a whole ITEM_RUN of text. Use clusterList and attrList 
//	to break the glyphs into whole-cluster runs before calling ScriptTextOut
//
//	We don't attempt to interpolate colours over each cluster. If there
//	is more than one ATTR per cluster then tough - only one gets used and
//	whole clusters (even if they contain multiple glyphs) get painted in
//	a single colour
//
static 
void PaintItemRunForeground (	
		USPDATA		* uspData,
		USPFONT		* uspFont,
		ITEM_RUN	* itemRun,	
		HDC			  hdc,
		int			  xpos,
		int			  ypos,
		RECT		* bounds,
		BOOL		  forcesel
	)
{
	HRESULT hr;

	int  i, lasti, g;
	int  glyphIdx1;
	int  glyphIdx2;
	int  runWidth;
	int	 runDir = 1;
	UINT oldMode;

	// make pointers to the run's various glyph buffers
	ATTR	* attrList		= uspData->attrList			+ itemRun->charPos;
	WORD	* clusterList	= uspData->clusterList		+ itemRun->charPos;
	WORD	* glyphList		= uspData->glyphList		+ itemRun->glyphPos;
	int		* widthList		= uspData->widthList		+ itemRun->glyphPos;
	GOFFSET	* offsetList	= uspData->offsetList		+ itemRun->glyphPos;

	// right-left runs can be drawn backwards for simplicity
	if(itemRun->analysis.fRTL)
	{
		oldMode = SetTextAlign(hdc, TA_RIGHT);
		xpos += itemRun->width;
		runDir = -1;
	}

	// loop over all the logical character-positions
	for(lasti = 0, i = 0; i <= itemRun->len; i++)
	{
		// find a change in attribute
		if(i == itemRun->len || attrList[i].fg != attrList[lasti].fg )
		{
			// scan forward to locate end of cluster (we must always
			// handle whole-clusters because the attr[] might fall in the middle)
			for( ; i < itemRun->len; i++)
				if(clusterList[i - 1] != clusterList[i])
					break;

			// locate glyph-positions for the cluster [i,lasti]
			GetGlyphClusterIndices(itemRun, clusterList, i, lasti, &glyphIdx1, &glyphIdx2);

			// measure the width (in pixels) of the run
			for(runWidth = 0, g = glyphIdx1; g <= glyphIdx2; g++)
				runWidth += widthList[g];

			// only need the text colour as we are drawing transparently
			SetTextColor(hdc, forcesel ? uspData->selFG : attrList[lasti].fg);
			
			//
			//	Finally output the run of glyphs
			//
			hr = ScriptTextOut(
				hdc, 
				&uspFont->scriptCache,
				xpos,
				ypos,
				0,
				NULL,					
				&itemRun->analysis, 
				0,
				0,
				glyphList  + glyphIdx1,
				glyphIdx2  - glyphIdx1 + 1,
				widthList  + glyphIdx1,
				0,
				offsetList + glyphIdx1
			);	
			
			// +ve/-ve depending on run direction
			xpos     += runWidth * runDir;		
			lasti     = i;
		}
	}

	// restore the mapping mode
	if(itemRun->analysis.fRTL)
		oldMode = SetTextAlign(hdc, oldMode);
}

//
// optimization: if the two neighbouring item-runs share the
// same 'selection state' as the current run, then we can completely skip drawing
//
static
BOOL CanSkip(USPDATA *uspData, int i, BOOL forcesel)
{
	BOOL canSkip = FALSE;

	if(i > 0 && i < uspData->itemRunCount - 1)
	{
		ITEM_RUN *prevRun = GetItemRun(uspData, i - 1);
		ITEM_RUN *thisRun = GetItemRun(uspData, i    );
		ITEM_RUN *nextRun = GetItemRun(uspData, i + 1);

		// skip drawing regular text if all three runs are selected
		if(forcesel == FALSE && prevRun->selstate == 1 && thisRun->selstate == 1 && nextRun->selstate == 1 &&
								prevRun->width != 0    && thisRun->width != 0    && nextRun->width != 0)
		{
			return TRUE;
		}
			   
		// skip drawing selected text if all three runs are unselected
		if(forcesel == TRUE  && prevRun->selstate == 0 && thisRun->selstate == 0 && nextRun->selstate == 0 &&
							    prevRun->width != 0    && thisRun->width != 0    && nextRun->width != 0)
		{
			return TRUE;
		}
	}

	return FALSE;
}

//
//	Paint the entire line of text
//
static
int PaintForeground ( 
		USPDATA   * uspData,
		HDC			hdc, 
		int			xpos, 
		int			ypos, 
		RECT	  * bounds, 
		BOOL		forcesel
	)
{
	int			i;
	int			yoff;
	ITEM_RUN  * itemRun;
	USPFONT	  * uspFont;

	//
	//	Loop over each item-run in turn
	//
	for(i = 0; i < uspData->itemRunCount; i++)
	{
		//	Generate glyph data for this run
		if((itemRun = GetItemRun(uspData, i)) == 0)
			break;

		// if run is not visible then don't bother displaying it!
		if(xpos + itemRun->width < bounds->left || xpos > bounds->right)
		{
			xpos += itemRun->width;
			continue;
		}

		// see if we can avoid drawing this run
		if(CanSkip(uspData, i, forcesel))
		{
			xpos += itemRun->width;
			continue;
		}
		
		// select the appropriate font
		uspFont = &uspData->uspFontList[itemRun->font];
		SelectObject(hdc, uspFont->hFont);
		yoff = uspFont->yoffset;
	
		if(itemRun->ctrl && !itemRun->tab)
		{
			// draw control-characters, as long as it's not a tab
			PaintCtrlCharRun(
				uspData,
				uspFont,
				itemRun,
				hdc, 
				xpos, 
				ypos + yoff
			);
		}
		else
		{
			// display a whole run of glyphs
			PaintItemRunForeground(
				uspData, 
				uspFont,
				itemRun,
				hdc,
				xpos, 
				ypos + yoff, 
				bounds, 
				forcesel
				);
		}

		xpos += itemRun->width;
	}

	return xpos;
}

//
//	UspAttrTextOut
//
//	Display a line of text previously analyzed with UspAnalyze. The stored
//	ATTR[] visual-attribute list is used to stylize the text as it is drawn.
//  
//	Coloured text-display using Uniscribe is very complicated. 
//  Three passes are required if high-quality text output is the goal.
//
//	Returns: adjusted x-coordinate 
//
int WINAPI UspTextOut (
		USPDATA  *	uspData,
		HDC			hdc, 
		int			xpos, 
		int			ypos, 
		int			lineHeight,
		int			lineAdjustY,
		RECT	 *	bounds
	)
{
	HRGN hrgn, hrgnClip;

	if(uspData->stringLen == 0 || uspData->glyphCount == 0)
		return xpos;

	hrgnClip = CreateRectRgn(0,0,1,1); 
	GetClipRgn(hdc, hrgnClip);

	//
	//	1. draw all background colours, including selection-highlights;
	//	   selected areas are added to the HDC clipping region which prevents
	//	   step#2 (below) from drawing over them
	//
	SetBkMode(hdc, OPAQUE);
	PaintBackground(uspData, hdc, xpos, ypos, lineHeight, bounds);

	//
	//  2. draw the text normally. Selected areas are left untouched
	//	   because of the clipping-region created in step#1
	//
	SetBkMode(hdc, TRANSPARENT);
	PaintForeground(uspData, hdc, xpos, ypos + lineAdjustY, bounds, FALSE);

	//
	//  3. redraw the text using a single text-selection-colour (i.e. white)
	//	   in the same position, directly over the top of the text drawn in step#2
	//
	//	   Before we do this, the HDC clipping-region is inverted, 
	//	   so only selection areas are modified this time 
	//
	hrgn = CreateRectRgnIndirect(bounds);
	ExtSelectClipRgn(hdc, hrgn, RGN_XOR);

	SetBkMode(hdc, TRANSPARENT);
	xpos = 
	PaintForeground(uspData, hdc, xpos, ypos + lineAdjustY, bounds, TRUE);
	
	// remove clipping regions
	SelectClipRgn(hdc, hrgnClip);
	DeleteObject(hrgn);
	DeleteObject(hrgnClip);

	return xpos;
}

//
//	Set the colours used for drawing text-selection
//
void WINAPI UspSetSelColor (
		USPDATA		* uspData, 
		COLORREF	  fg, 
		COLORREF	  bg
	)
{
	uspData->selFG = fg;
	uspData->selBG = bg;
}

//
//	Used to initialize a font. 
//
//	Caller must call UspFreeFont when he is finished, and manually 
//	do a DeleteObject on the HFONT. That is, caller is responsible for
//	managing the lifetime of the HFONT
//
void WINAPI UspInitFont (
		USPFONT		* uspFont, 
		HDC			  hdc, 
		HFONT		  hFont
	)
{
	ZeroMemory(uspFont, sizeof(USPFONT));

	uspFont->hFont				= hFont;
	uspFont->scriptCache		= 0;

	hFont = SelectObject(hdc, hFont);

	GetTextMetrics(hdc, &uspFont->tm);
	InitCtrlChar(hdc, uspFont);

	SelectObject(hdc, hFont);
}

//
//	Free the font resources
//
void WINAPI UspFreeFont (
		USPFONT		* uspFont
	)
{
//	DeleteObject(uspFont->hFont);
	ScriptFreeCache(&uspFont->scriptCache);
}
