//
//	UspMouse.c
//
//	Contains routines useful for converting between 
//	mouse-coordinates and character-positions, designed
//  to emulate the functionality provided by the
//	ScriptStringXtoCP and ScriptStringCPToX
//	
//	UspSnapXToOffset
//	UspXToOffset
//	UspOffsetToX
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
#include "usplib.h"

ITEM_RUN *GetItemRun(USPDATA *uspData, int visualIdx);

//
//	Translate the specified screen-x-coordinate (usually the mouse position)
//	into a character-offset within the specified line. Calling this function
//  is preferable to using UspXToOffset and UspOffsetToX separately as it
//  gives more natural results for mouse-placement.
//
//	The adjusted x-coordinate is also returned after it has been snapped to
//	the nearest cluster boundary, in *snappedX. 
//
//	The script direction is returned (right/left) in *fRTL
//
BOOL WINAPI UspSnapXToOffset (	
		USPDATA	  * uspData,		
		int			xpos,			
		int       * snappedX,		// out, optional
		int       * charPos,		// out
		BOOL	  * fRTL			// out, optional
	)
{
	int i, cp, trailing, runx = 0;
	int runCount = uspData->itemRunCount;

	if(charPos)		*charPos	= 0;
	if(snappedX)	*snappedX	= 0;
	if(fRTL)		*fRTL		= 0;

	if(xpos < 0)
		xpos = 0;

	// don't allow mouse to move into the CR-LF sequence. This is incorrect
	// for RTL and mixed LTR/RTL runs!!!
	for(i = runCount-1; i >= 0; i--)
	{
		if(uspData->itemRunList[i].eol)
			runCount--;
	}

	//if(runCount > 0 && uspData->itemRunList[runCount-1].eol)
	//	runCount--;

	//
	// process each "run" or span of text in visual order
	//
	for(i = 0; i < runCount; i++)
	{
		ITEM_RUN *itemRun;

		// get width of this span of text
		if((itemRun = GetItemRun(uspData, i)) == 0)
			break;

		if(itemRun->eol)
		{
			runx += itemRun->width;			
			continue;
		}

		// does the mouse fall within this run of text?
		if((i == runCount - 1) || xpos >= runx && xpos < runx + itemRun->width)
		{
			// get the character position
			ScriptXtoCP(xpos - runx, 
				itemRun->len,
				itemRun->glyphCount,
				uspData->clusterList	+ itemRun->charPos,
				uspData->svaList		+ itemRun->glyphPos,
				uspData->widthList		+ itemRun->glyphPos,
				&itemRun->analysis,
				&cp,
				&trailing
				);
				
			// convert this character position back to a x-coord
			if(snappedX)
			{
				ScriptCPtoX(cp+trailing,
					FALSE,
					itemRun->len,
					itemRun->glyphCount,
					uspData->clusterList	+ itemRun->charPos,
					uspData->svaList		+ itemRun->glyphPos,
					uspData->widthList		+ itemRun->glyphPos,
					&itemRun->analysis,
					snappedX
					);
					
				// return the adjusted x-coordinate
				*snappedX += runx;
			}

			// return the character-offset
			*charPos = cp + trailing + itemRun->charPos;

			// return script-direction for this run
			if(fRTL)
				*fRTL = itemRun->analysis.fRTL;

			return TRUE;
		}

		// advance xpos to next run
		runx += itemRun->width;
	}

	return FALSE;
}

//
//	Translate the specified screen-x-coordinate (usually the mouse position)
//	into a character-offset within the specified line. This is function
//	duplicates the behaviour of ScriptStringXToCP
//
//	The script direction is also returned (right/left) in *fRTL
//
BOOL WINAPI UspXToOffset (
		USPDATA	  * uspData,		
		int			xpos,			
		int       * charPos,		// out
		BOOL	  * trailing,		// out
		BOOL	  * fRTL			// out, optional
	)
{
	int i, runx = 0;
	int runCount = uspData->itemRunCount;

	if(xpos < 0)
		xpos = 0;

	if(runCount > 0 && uspData->itemRunList[runCount-1].eol)
		runCount--;

	//
	// process each "run" or span of text 
	//
	for(i = 0; i < runCount; i++)
	{
		ITEM_RUN *itemRun;

		// get width of this span of text
		if((itemRun = GetItemRun(uspData, i)) == 0)
			break;

		// does the mouse fall within this run of text?
		if((i == runCount - 1) || xpos >= runx && xpos < runx + itemRun->width)
		{
			// get the character position
			ScriptXtoCP(xpos - runx, 
				itemRun->len,
				itemRun->glyphCount,
				uspData->clusterList	+ itemRun->charPos,
				uspData->svaList		+ itemRun->glyphPos,
				uspData->widthList		+ itemRun->glyphPos,
				&itemRun->analysis,
				charPos,
				trailing
				);
				
			*charPos += itemRun->charPos;

			// return script-direction for this run
			if(fRTL)
				*fRTL = itemRun->analysis.fRTL;

			return TRUE;
		}

		// advance xpos to next run
		runx += itemRun->width;
	}

	*trailing = 0;
	*charPos  = 0;
	if(fRTL) *fRTL = 0;
	return FALSE;
}

//
//	Translate the specified character-position to a pixel-based
//	coordinate, relative to the start of the string
//	Duplicates the behaviour of ScriptStringCPToX
//
BOOL WINAPI UspOffsetToX (
		USPDATA		* uspData, 
		int			  charPos, 
		BOOL		  trailing,		// out
		int			* px			// out
	)
{
	int i;
	int xpos = 0;
	ITEM_RUN *itemRun;

	*px = 0;

	//
	// process each "run" or span of text in visual order
	//
	for(i = 0; i < uspData->itemRunCount; i++)
	{
		// get width of this span of text
		if((itemRun = GetItemRun(uspData, i)) == 0)
			break;

		// does the mouse fall within this run of text?
		if((i == uspData->itemRunCount - 1) || 
			charPos >= itemRun->charPos && charPos < itemRun->charPos + itemRun->len)
		{
			ScriptCPtoX(charPos - itemRun->charPos,
				trailing,
				itemRun->len,
				itemRun->glyphCount,
				uspData->clusterList	+ itemRun->charPos,
				uspData->svaList		+ itemRun->glyphPos,
				uspData->widthList		+ itemRun->glyphPos,
				&itemRun->analysis,
				px
			  );

			*px += xpos;
			return TRUE;
		}

		xpos += itemRun->width;
	}

	return FALSE;
}