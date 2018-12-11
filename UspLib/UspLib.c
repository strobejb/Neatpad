//
//	UspLib.c
//
//	USPLIB is a Unicode text-display support library. It is a wrapper
//	around the low-level Uniscribe API and provides routines
//	used to display Unicode text with the ability to
//	apply styles (colours/fonts etc) with a user-supplied attribute-run-list 
//	
//	UspAllocate
//	UspFree
//	UspAnalyze
//
//	UspApplySelection
//	UspApplyAttributes
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
#include <stdlib.h>
#include <malloc.h>
#include "usplib.h"

// UspCtrl.c
int  CtrlCharWidth(USPFONT *uspFont, HDC hdc, ULONG chValue);

//
//	Return an item-run based on visual-order
//
ITEM_RUN *GetItemRun(USPDATA *uspData, int visualIdx)
{
	int logicalIdx = uspData->visualToLogicalList[visualIdx];
	return &uspData->itemRunList[logicalIdx];
}

//
//	Locate tabs in the original character array, and then modify the
//	corresponding glyph-width (which will be zero) to a value reflecting
//	the size of the tab in pixels. This is all that is required to support
//	tabs, the mouse+drawing routines just treat them as regular glyphs
//	after this.
//
static 
BOOL ExpandTabs(USPDATA *uspData, WCHAR *wstr, int wlen, SCRIPT_TABDEF *tabdef)
{
	int i;
	int xpos	 = tabdef->iTabOrigin;
	int tabidx   = 0;
	int tabWidth;
	int tab;

	// calculate average character-width
	int charWidth = uspData->uspFontList ? uspData->uspFontList[0].tm.tmAveCharWidth :
										   uspData->defaultFont.tm.tmAveCharWidth; 

 	// validate the SCRIPT_TABDEF structure
	if(tabdef->cTabStops != 0 && tabdef->pTabStops == 0 || tabdef->cTabStops < 0)
		return FALSE;

	// All tab-stops are the length of the first entry in pTabStops
	if(tabdef->cTabStops == 1)
	{
		tabWidth = tabdef->pTabStops[0];
	}
	// Tab-stops occur every eight average-character widths
	else
	{
		tabWidth = charWidth;
	}

	// Do the scaling
	if(tabdef->iScale != 0)
	{
		xpos	 *= tabdef->iScale / 4;
		tabWidth *= tabdef->iScale / 4;
	}
	else
	{
		xpos	 *= charWidth;
		tabWidth *= charWidth;
	}

	// scan item-runs in *visual* order (think this is right!)
	for(i = 0; i < uspData->itemRunCount; i++)
	{
		ITEM_RUN *itemRun = GetItemRun(uspData, i);

		// match tabs
		if(wstr[itemRun->charPos] == '\t')
		{
			// calculate distance to next tab-stop position
			if(tabdef->cTabStops <= 1)
			{
				tab = tabWidth - (xpos % tabWidth);
			}
			else 
			{
				// search for the next tab-stop position
				for( ; tabidx < tabdef->cTabStops; tabidx++)
				{
					if(xpos > tabdef->pTabStops[tabidx])
					{
						tab = 0; 
						break;
					}
				}

				//tab = uspData->
			}

			uspData->widthList[itemRun->glyphPos] = tab;
			itemRun->width	= tab;
			itemRun->tab	= TRUE;
		}

		xpos += itemRun->width;
	}

	return TRUE;
}

//
//	Non-complex scripts can be merged into a single item
//
static
int MergeSimpleScripts(SCRIPT_ITEM *itemList, int itemCount)
{
	// global script-table, used for merging non-complex runs together :)
	SCRIPT_PROPERTIES **propList;
	int					propCount;
	int					i;

	// get pointer to the global script table
	ScriptGetProperties(&propList, &propCount);

	//	coalesce item-runs that are based on simple-scripts
	for(i = 0; i < itemCount - 1; i++)
	{
		// use each item-run's SCRIPT_ANALYSIS::eScript member to lookup the
		// appropriate script in the global-table
		if(propList[itemList[i+0].a.eScript]->fComplex == FALSE && 
		   propList[itemList[i+1].a.eScript]->fComplex == FALSE)
		{
			// be careful which SCRIPT_ITEM we overwrite as we need to
			// maintain correct iCharPos members
			memmove(&itemList[i+1], &itemList[i+2], (itemCount-i-1) * sizeof(SCRIPT_ITEM));
			itemCount--;

			itemList[i].a.eScript = SCRIPT_UNDEFINED;
		}
	}

	return itemCount;
}

//	
//	Wrapper around ScriptItemize. Merges the results of ScriptItemize with
//  "application-defined style runs". In our case, we take the ATTR[] array 
//  of runs and inspect the font and control-character flags *only* - colours
//  and selection flags are *not* used to split the runs.
//
//	Note that for scripts that do *not* have the fComplex property set, (obtained
//  via ScriptGetProperties) we can merge item-runs together to form a single
//	run with SCRIPT_UNDEFINED...this makes text-display *much* faster for simple
//  scripts such as english.
//
static 
BOOL BuildMergedItemRunList (
		USPDATA			* uspData, 
		WCHAR			* wstr, 
		int				  wlen, 
		ATTR			* attrList,
		SCRIPT_CONTROL	* scriptControl,
		SCRIPT_STATE	* scriptState
	)
{
	// attribute-list index+position
	int				  a = 0;
	int				  attrPos = 0;
	int				  attrLen = 0;
	ATTR			  attr = attrList[0];

	// item-list index+position (only needed in this function)
	int				  i = 0;
	int				  itemPos;
	int				  itemLen;
	int				  itemCount;
	SCRIPT_ITEM		* itemList		 = uspData->tempItemList;
	int				  allocLen		 = max(uspData->tempItemAllocLen, 16);

	// merged-list index+position
	int				  m = 0;
	int				  mergePos		 = 0;
	ITEM_RUN		* mergedList	 = uspData->itemRunList;
	int				  mergedAllocLen = uspData->itemRunAllocLen;

	HRESULT			  hr;

	//	Create the Uniscribe SCRIPT_ITEM list which just describes
	//  the spans of plain unicode text (grouped by script)
	do
	{
		// allocate memory for SCRIPT_ITEM list
		if(uspData->tempItemAllocLen < allocLen)
		{
			itemList = realloc(itemList, allocLen * sizeof(SCRIPT_ITEM));
			
			if(itemList == 0)
				return FALSE;

			// store this temporary-item list so we don't have to free/alloc all the time
			uspData->tempItemAllocLen	= allocLen;
			uspData->tempItemList		= itemList;
		}

		hr = ScriptItemize(
				wstr, 
				wlen, 
				allocLen, 
				scriptControl, 
				scriptState, 
				itemList, 
				&itemCount
			);

		if(hr != S_OK && hr != E_OUTOFMEMORY)
			return FALSE;

		allocLen *= 2;
	}
	while(hr != S_OK);

	// any non-complex scripts can be merged into a single item
	// itemCount could be decremented after this call
	itemCount = MergeSimpleScripts(itemList, itemCount);

	//
	//	Merge SCRIPT_ITEMs with the ATTR runlist to produce finer-grained 
	//  item-runs which describes spans of text *and* style 
	//
	while(i < itemCount)
	{
		if(attrPos + attrLen < wlen)
			attr = attrList[a];

		// grow the merge-list if necessary
		if(m >= mergedAllocLen)
		{
			mergedAllocLen += 16;
			mergedList = realloc(mergedList, mergedAllocLen * sizeof(ITEM_RUN));
		}

		// build an ITEM_RUN with default settings
		ZeroMemory(&mergedList[m], sizeof(ITEM_RUN));
		mergedList[m].analysis		= itemList[i].a;
		mergedList[m].font			= attr.font;
		mergedList[m].ctrl			= attr.ctrl;
		mergedList[m].eol			= attr.eol;

		// control-characters have their Unicode code-point stored in the item-run
		// (there will always be 1 ctrlchar per run)
		if(mergedList[m].ctrl)
			mergedList[m].chcode = wstr[mergePos];

		// safeguard against incorrect font usage when the user didn't specify a font-list
		if(uspData->uspFontList == &uspData->defaultFont)
			mergedList[m].font = 0;

		itemPos = itemList[i].iCharPos;
		itemLen = itemList[i+1].iCharPos - itemPos;

		//
		// coalesce identical attribute cells into one contiguous run. Ignore all style 
		// attributes except the ::font flags so even ATTR with different colours 
		// will be merged together if they share the same font. 
		//
		// However, any ATTR with the ::ctrl  flag set will be isolated as a single 
		// ITEM_RUN representing one control-character exactly.
		//
		if(attrLen == 0)
		{
			while(attrPos + attrLen < wlen)
			{
				attrLen += attrList[a].len;		

				if(attrPos + attrLen < wlen &&
				  (attrList[a].font != attrList[a+1].font ||
				   attrList[a].ctrl != attrList[a+1].ctrl ||
				   attrList[a].eol  != attrList[a+1].eol  ||
				   attrList[a].ctrl ||
				   attrList[a].eol)
				  ) 
				   break;

				// skip to next (coalesce)
				a++;
			} 
		}

		//	Detect overlapping run boundaries 
		if(attrPos+attrLen < itemPos+itemLen)
		{
			mergedList[m].charPos	= mergePos;
			mergedList[m].len		= (attrPos+attrLen) - mergePos;

			attrPos += attrLen;
			attrLen = 0;
			a++;
		}
		else if(attrPos+attrLen >= itemPos+itemLen)
		{
			mergedList[m].charPos	= mergePos;
			mergedList[m].len		= (itemPos+itemLen) - mergePos;
			i++;

			if(attrPos+attrLen == itemPos+itemLen)
			{
				attrPos += attrLen;
				attrLen = 0;
				a++;
			}
		}

		// advance position in merge-list
		mergePos += mergedList[m].len;
		m++;
	}

	// store the results
	uspData->itemRunList	   = mergedList;
	uspData->itemRunCount	   = m;
	uspData->itemRunAllocLen   = mergedAllocLen;

	return TRUE;
}

static 
BOOL BuildVisualMapping (
		ITEM_RUN *	mergedRunList, 
		int			mergedRunCount, 
		BYTE		bidiLevels[], 
		int			visualToLogicalList[]
	)
{
	int		i;

	//	Manually extract bidi-embedding-levels ready for ScriptLayout
	for(i = 0; i < mergedRunCount; i++)
		bidiLevels[i] = (BYTE)mergedRunList[i].analysis.s.uBidiLevel;

	//	Build a visual-to-logical mapping order
	ScriptLayout(
			mergedRunCount,
			bidiLevels,
			visualToLogicalList,
			0
		);

	return TRUE;
}

/*
// 
//	Reverse logical-cluster array, and the clusters within the array
//	For RTL runs, this changes the logical-cluster list to visual-order 
//
//	Unused but left just in-case I need it again
//
static
void ReverseClusterRun(WORD *sourceList, WORD *destList, int runLen)
{
	int i, lasti;

	for(i = runLen - 1, lasti = i; i >= -1; i--)
	{
		if(i == -1 || sourceList[i] != sourceList[lasti])
		{
			int clusterlen = lasti - i;
			int pos = sourceList[lasti] - (clusterlen - 1);

			while(clusterlen--)
				*destList++ = pos;

			lasti = i;
		}
	}
}*/

//
//	Call ScriptShape and ScriptPlace to return glyph information
//	for the specified run of text. 
//
static 
BOOL ShapeAndPlaceItemRun(USPDATA *uspData, ITEM_RUN *itemRun, HDC hdc, WCHAR *wrunstr)
{
	ABC			abc;
	HRESULT		hr;
	USPFONT   *	uspFont		= 0;
	HANDLE		holdFont	= 0;
	int			reallocSize = 0;
	
	// select the appropriate font
	uspFont  = &uspData->uspFontList[itemRun->font];
	holdFont = SelectObject(hdc, uspFont->hFont);

	// glyph data for this run is appended to the end 
	itemRun->glyphPos = uspData->glyphCount;

	//
	// Generate glyph information for each character in the run
	// keep looping until we find a buffer big enough
	//
	do
	{
		// guess at 1.5x the run-length
		reallocSize += itemRun->len * 3 / 2;

		// perform memory allocations. Let ScriptShape catch any alloc-failures
		if(uspData->glyphCount + reallocSize >= uspData->glyphAllocLen)
		{
			uspData->glyphAllocLen += reallocSize;

			uspData->glyphList	 = realloc(uspData->glyphList,	 uspData->glyphAllocLen * sizeof(WORD));
			uspData->offsetList  = realloc(uspData->offsetList,  uspData->glyphAllocLen * sizeof(GOFFSET));
			uspData->widthList	 = realloc(uspData->widthList,	 uspData->glyphAllocLen * sizeof(int));
			uspData->svaList     = realloc(uspData->svaList,	 uspData->glyphAllocLen * sizeof(SCRIPT_VISATTR));
		}

		//
		//	Convert the unicode-text into an array of glyphs
		//
		hr = ScriptShape(
			hdc,
			&uspFont->scriptCache, 
			wrunstr, 
			itemRun->len, 
			uspData->glyphAllocLen - uspData->glyphCount, 
			&itemRun->analysis, 
			uspData->glyphList		+ itemRun->glyphPos,
			uspData->clusterList	+ itemRun->charPos,		// already allocated in UspAnalyze
			uspData->svaList		+ itemRun->glyphPos,
			&itemRun->glyphCount
			);
	
		// no glyphs in the font - try again
		if(hr == USP_E_SCRIPT_NOT_IN_FONT)
		{
			itemRun->analysis.eScript = SCRIPT_UNDEFINED;
		}
		// unknown failure
		else if(hr != S_OK && hr != E_OUTOFMEMORY)
		{
			SelectObject(hdc, holdFont);
			return FALSE;
		}

	} while(hr != S_OK);

	// expand the glyph-list to include this item-run
	uspData->glyphCount += itemRun->glyphCount;


	//
	//	Generate glyph advance-widths for this run
	//
	ScriptPlace(
		hdc,
		&uspFont->scriptCache,
		uspData->glyphList	+ itemRun->glyphPos,
		itemRun->glyphCount,
		uspData->svaList	+ itemRun->glyphPos,
		&itemRun->analysis, 
		uspData->widthList	+ itemRun->glyphPos,
		uspData->offsetList	+ itemRun->glyphPos,
		&abc
		);

	// 
	//	Control-characters require special handling
	//
	if(itemRun->ctrl && itemRun->chcode != '\t')
	{
		// chcode is only valid for control-characters
		int chwidth = CtrlCharWidth(uspFont, hdc, itemRun->chcode);
		
		uspData->widthList[itemRun->glyphPos]	= chwidth;
		itemRun->width							= chwidth;
	}
	else
	{
		// remember the item-run width
		itemRun->width = abc.abcA + abc.abcB + abc.abcC;
	}

	// restore the font
	SelectObject(hdc, holdFont);
	return TRUE;
}

//
//	Remember the selection-state of an ITEM_RUN:
//
//	0 - no characters selected
//  1 - all characters selected
//  2 - some characters selected
//
//	This is a useful optimization used for drawing - under some
//  circumstances, an ITEM_RUN can be skipped if it's neighbouring
//  runs share the same selection-state
//
static
void IdentifyRunSelections(USPDATA *uspData, ITEM_RUN *itemRun)
{
	int c;
	int numsel = 0;
	
	// analyze run to see which characters are selected
	for(c = itemRun->charPos; c < itemRun->charPos + itemRun->len; c++)
		if(uspData->attrList[c].sel)
			numsel++;

	// set the selection state accordingly
	if(numsel == 0)					itemRun->selstate = 0;
	else if(numsel == itemRun->len)	itemRun->selstate = 1;
	else							itemRun->selstate = 2;
}

//
//	Update the USPDATA with a new attribute-run-list.
//
//	The font-information stored in ATTR::font is ignored, as are
//  the ::ctrl and ::eol flags
//
VOID WINAPI UspApplyAttributes(USPDATA *uspData, ATTR *attrRunList)
{
	int i, a, c=0;

	//
	//	'Flatten' the user-supplied attribute-run-list to an array
	//	of single ATTR structures, each 1-unit long.
	//
	for(a = 0, i = 0; i < uspData->stringLen; i++)
	{
		uspData->attrList[i]		= attrRunList[a];
		uspData->attrList[i].len	= 1;

		if(++c == attrRunList[a].len)
		{
			a++;
			c = 0;
		}
	}

	//	Identify the selection state of each run (none,all,partial)
	for(i = 0; i < uspData->itemRunCount; i++)
	{
		IdentifyRunSelections(uspData, &uspData->itemRunList[i]);
	}
}

//
//	Update USPDATA with new selection information - only modify
//	the ATTR::sel values within our internal attribute-list
//  (i.e. leave all other text styles untouched)
//
VOID WINAPI UspApplySelection(USPDATA *uspData, int selStart, int selEnd)
{
	int i,p;

	if(selStart >= selEnd)
	{
		int t = selStart;
		selStart = selEnd;
		selEnd   = t;
	}

	for(i = 0; i < uspData->itemRunCount; i++)
	{
		ITEM_RUN *itemRun = &uspData->itemRunList[i];

		for(p = itemRun->charPos; p < itemRun->charPos + itemRun->len; p++)
			uspData->attrList[p].sel = (p >= selStart && p < selEnd);

		IdentifyRunSelections(uspData, itemRun);
	}
}

//
//	Initialize the USPDATA line-state with a new text-string,
//  and an optional attribute-run-list. We reuse any previously
//  allocated arrays in the USPDATA structure, minimizing heap-access
//
//	*attrRunList can be NULL, in which case the line of text is initialized
//	with the default Windows text-colours and with font#0.
//	Otherwise, attrRunList is expected to describe a range of text the
//	same length as the text-input buffer.
//
//	*uspFontList can also be NULL, in which case the currently selected font
//	is used to shape the text. This same font *must* be reselected into
//	the device context when UspTextOut is called.
//
//	Any change to the fonts in uspFontList requires UspAnalyze to be called again.
//
BOOL WINAPI UspAnalyze (
		USPDATA			* uspData, 
		HDC				  hdc, 
		WCHAR			* wstr, 
		int				  wlen, 
		ATTR			* attrRunList, 
		UINT			  flags, 
		USPFONT			* uspFontList,
		SCRIPT_CONTROL	* scriptControl,
		SCRIPT_STATE	* scriptState,
		SCRIPT_TABDEF   * scriptTabdef
	)
{
	ATTR	defAttr;
	int		itemRunAllocLen;
	int		i;

	if(uspData == 0)
		return FALSE;

	// reset the lists
	uspData->itemRunCount	= 0;
	uspData->glyphCount		= 0;
	uspData->uspFontList	= uspFontList;
	uspData->stringLen		= wlen;

	// nothing to do?
	if(wstr == 0 || wlen == 0)
		return TRUE;

	// remember current allocation-size of itemRunList
	itemRunAllocLen = uspData->itemRunAllocLen;

	// use the default font if no user-supplied list
	if(uspFontList == 0)
	{
		uspData->defaultFont.hFont = 0;
		uspData->uspFontList = &uspData->defaultFont;
	}

	//
	// if no attributes specified then default to a single span
	// covering the entire length of the text
	//
	if(attrRunList == 0)
	{
		// create the default attribute
		defAttr.fg		= GetSysColor(COLOR_WINDOWTEXT);
		defAttr.bg		= GetSysColor(COLOR_WINDOW);
		defAttr.font	= 0;
		defAttr.sel		= 0;
		defAttr.ctrl	= 0;
		defAttr.eol		= 0;
		defAttr.len		= wlen;

		attrRunList		= &defAttr;
	}

	//
	//	Build a list of runs by calling ScriptItemize and merging
	//  those spans with the attribute-list. When BuildMergedItemRunList
	//  returns, the itemRunList array has been stored in uspData
	//
	if(!BuildMergedItemRunList(
				uspData,
				wstr, 
				wlen, 
				attrRunList,
				scriptControl,
				scriptState)
		)
	{
		return FALSE;
	}
 
	//	reallocate BIDI-arrays if item-run-list changed size
	if(itemRunAllocLen < uspData->itemRunAllocLen)
	{
		uspData->bidiLevels	= realloc(uspData->bidiLevels, uspData->itemRunAllocLen * sizeof(BYTE));
		uspData->visualToLogicalList = realloc(uspData->visualToLogicalList, uspData->itemRunAllocLen * sizeof(int));
	}

	//	Analyze the resulting runlist and build the BIDI-level array	
	if(!BuildVisualMapping( uspData->itemRunList, 
							uspData->itemRunCount, 
							uspData->bidiLevels,
							uspData->visualToLogicalList)
			)
	{
		return FALSE;
	}

	// Rellocate logical cluster+attribute arrays prior to shaping
	if(uspData->stringAllocLen < wlen)
	{
		uspData->stringAllocLen = wlen;
		uspData->clusterList	= realloc(uspData->clusterList,	wlen * sizeof(WORD));
		uspData->attrList		= realloc(uspData->attrList,	wlen * sizeof(ATTR));
		uspData->breakList		= realloc(uspData->breakList,	wlen * sizeof(SCRIPT_LOGATTR));
	}

	// Generate the word-break information in logical order
	for(i = 0; i < uspData->itemRunCount; i++)
	{
		ITEM_RUN *itemRun = &uspData->itemRunList[i];

		ScriptBreak(
				wstr + itemRun->charPos, 
				itemRun->len, 
				&itemRun->analysis, 
				uspData->breakList + itemRun->charPos
			);
	}

	// Perform shaping + generate glyph data
	for(i = 0; i < uspData->itemRunCount; i++)
	{
		ITEM_RUN *itemRun = GetItemRun(uspData, i);//;//&uspData->itemRunList[i];	//

		ShapeAndPlaceItemRun(
				uspData, 
				itemRun, 
				hdc, 
				wstr + itemRun->charPos
			);
	}

	//
	// locate tab-characters and expand the corresponding glyph-widths appropriately.
	// this must happen after *all* shaping/widths have been generated
	//
	if(scriptTabdef)
	{
		if(ExpandTabs(uspData, wstr, wlen, scriptTabdef))
		{
			// modify the SCRIPT_LOGATTR list to make tabs whitespace
			for(i = 0; i < wlen; i++)
			{
				if(wstr[i] == '\t')
					uspData->breakList[i].fWhiteSpace = TRUE;
			}
		}
		else
		{
			return FALSE;
		}
	}


	//
	//	Keep a flattened copy of the attribute-run-list, but 
	//	with one element per character rather than larger runs.
	//
	UspApplyAttributes(uspData, attrRunList);

	return TRUE;
}

//
//	Create the control structure
//
USPDATA * WINAPI UspAllocate()
{
	USPDATA *uspData = malloc(sizeof(USPDATA));

	if(uspData)
	{
		// set all members to zero including run-counts etc
		memset(uspData, 0, sizeof(USPDATA));

		uspData->selFG = GetSysColor(COLOR_HIGHLIGHTTEXT);
		uspData->selBG = GetSysColor(COLOR_HIGHLIGHT);
	}
		
	return uspData;
}

//
//	Free everything
//
VOID WINAPI UspFree(USPDATA *uspData)
{
	if(uspData)
	{
		// free the script-cache (will be NULL if a user-supplied fontlist was specified)
		ScriptFreeCache(&uspData->defaultFont.scriptCache);

		// free the glyph-buffers
		free(uspData->glyphList);
		free(uspData->svaList);
		free(uspData->widthList);
		free(uspData->offsetList);
		
		// free the item-run buffers
		free(uspData->itemRunList);
		free(uspData->visualToLogicalList);
		free(uspData->bidiLevels);
		free(uspData->tempItemList);
		
		// free the logical character-buffers
		free(uspData->clusterList);
		free(uspData->attrList);

		// free the control structure
		free(uspData);
	}
}

BOOL WINAPI UspGetSize(USPDATA *uspData, SIZE * size)
{
	int i;
	
	size->cx = 0;
	size->cy = 0;

	for(i = 0; i < uspData->itemRunCount; i++)
	{
		size->cx += uspData->itemRunList[i].width;
	}

	return TRUE;
}

SCRIPT_LOGATTR * WINAPI UspGetLogAttr(USPDATA *uspData)
{
	if(uspData && uspData->breakList)
	{
		return uspData->breakList;
	}
	else
	{
		return NULL;
	}
}

BOOL WINAPI UspBuildAttr (
		ATTR	  *	attr,
		COLORREF    colfg,	
		COLORREF    colbg,
		int			len,
		int			font,
		int			sel,
		int			ctrl,
		int			eol
	)
{
	if(attr == 0)
		return FALSE;

	attr->fg		= colfg;
	attr->bg		= colbg;
	attr->len		= len;
	attr->font		= font;
	attr->sel		= sel;
	attr->ctrl		= ctrl;
	attr->eol		= eol;
	attr->reserved	= 0;

	return TRUE;
}