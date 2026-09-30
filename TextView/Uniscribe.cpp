//
//	Multi font+colour Uniscribe wrapper
//

#define _WIN32_WINNT 0x501
#include <windows.h>
#include <usp10.h>
#include <tchar.h>

#include "TextView.h"
#include "TextViewInternal.h"

#pragma comment(lib, "usp10.lib")

typedef struct
{
	SCRIPT_ITEM item;
	ATTR		attr;

} ITEM_ATTR;


//
//	CreateMergedItemAttrList
//
//	Wrapper around ScriptItemize
//
//
ITEM_ATTR *CreateMergedItemAttrList(WCHAR *wstr, int wlen, ATTR *attrList, int attrCount, int *mergedCount)
{
	SCRIPT_CONTROL	scriptControl = { 0 };
	SCRIPT_STATE	scriptState   = { 0 };

	SCRIPT_ITEM		*itemList = (SCRIPT_ITEM *)malloc((wlen + 1) * 2 * sizeof(SCRIPT_ITEM));
	int				 itemCount;

	int				 a = 0;
	int				 i = 0;
	int				 attrPos = 0;
	int				 attrLen = 0;
	int				 itemPos;
	int				 itemLen;

	ITEM_ATTR		 *mergedList;
	int				  mergePos = 0;
	int				  m = 0;			// index into the merge list

	//
	//	Create the Uniscribe SCRIPT_ITEM list which just describes
	//  the spans of plain unicode text (grouped by script)
	//
	ScriptItemize(
			wstr, 
			wlen, 
			wlen*2, 
			&scriptControl, 
			&scriptState, 
			itemList, 
			&itemCount
		);

	// create list to return to caller
	mergedList = (ITEM_ATTR *)malloc(max(itemCount, attrCount) * sizeof(ITEM_ATTR));

	//
	//	Merge it with the runlist to produce a finer-grained span-list
	//  which describes spans of text *and* style 
	//
	while(i < itemCount && a < attrCount)
	{
		// first do a straight copy
		mergedList[m].attr			= attrList[a];
		mergedList[m].item			= itemList[i];

		itemPos = itemList[i].iCharPos;
		itemLen = itemList[i+1].iCharPos - itemPos;

		//
		// coalesce identical attribute cells into one contiguous run
		// ignore all style attributes except font - so even ATTR with different
		// colours will be merged together if they share the same font
		//
		if(attrLen == 0)
		{
			while(a < attrCount)
			{
				attrLen += attrList[a].len;

				if(attrList[a].font != attrList[a+1].font)
				{
					break;
				}

				// skip to next
				a++;
			} 
		}

		//
		//	Detect overlapping run boundaries 
		//
		if(attrPos+attrLen < itemPos+itemLen)
		{
			mergedList[m].item.iCharPos = mergePos;
			mergedList[m].attr.len		= (attrPos+attrLen) - mergePos;

			attrPos += attrLen;
			attrLen = 0;
			a++;
		}
		else if(attrPos+attrLen >= itemPos+itemLen)
		{
			mergedList[m].item.iCharPos = mergePos;
			mergedList[m].attr.len		= (itemPos+itemLen) - mergePos;
			i++;

			if(attrPos+attrLen == itemPos+itemLen)
			{
				attrPos += attrLen;
				attrLen = 0;
				a++;
			}
		}

		//
		// advance position in merge-list
		//
		mergePos += mergedList[m].attr.len;
		m++;
	}

	// return the new list
	*mergedCount = m;
	return mergedList;
}