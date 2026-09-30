//
//	UspCtrl.c
//
//	Contains support routines for painting control-characters
//	
//	Written by J Brown 2006 Freeware
//	www.catch22.net
//

#define _WIN32_WINNT 0x501

#ifndef _UNICODE
#define _UNICODE
#endif

#ifndef UNICODE
#error  "Please build as Unicode only!"
#define UNICODE
#endif

#define STRICT
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <usp10.h>
#include <tchar.h>
#include "usplib.h"

#if _MSC_VER == 1200
#define swprintf _snwprintf
#endif

// 'ASCII' control characters U+0000 - U+001F
static const WCHAR *asciireps[] = 
{
	L"NUL", L"SOH", L"STX", L"ETX", L"EOT", L"ENQ", L"ACK", L"BEL",
	L"BS",  L"HT",  L"LF",  L"VT",  L"FF",  L"CR",  L"SO",  L"SI",
	L"DLE", L"DC1", L"DC2", L"DC3", L"DC4", L"NAK", L"SYN", L"ETB",
	L"CAN", L"EM",  L"SUB", L"ESC", L"FS",  L"GS",  L"RS",  L"US"
};

// C1 control characters U+0080 - U+00A0
static const WCHAR *c1controls[] =	
{
	L"80",  L"81",  L"BPH", L"NBH", L"IND", L"NEL", L"SSA", L"ESA",
	L"HTS", L"HTJ", L"VTS", L"PLD", L"PLU", L"RI",  L"SS2", L"SS3",
	L"DCS", L"PU1", L"PU2", L"STS", L"CCH", L"MW",  L"SPA", L"EPA",
	L"SOS", L"99",  L"SCI", L"CSI", L"ST",  L"OSC", L"PM",  L"APC",
	L"NBSP",  
	L"SHY",	// 0xAD
};

static
const WCHAR * CtrlStrRep(DWORD ch)
{
	if(ch < 0x20)
		return asciireps[ch];

	if(ch >= 0x80 && ch < 0xA0)
		return c1controls[ch-0x80];

	switch(ch)
	{
	// Arabic control-characters
	case 0x0600:  return L"ANS";		// Arabic Number Sign
	case 0x0601:  return L"ASS";		// Arabic Sign Sanah
	case 0x0602:  return L"ANSN";		// Arabic Number Sign
	case 0x0603:  return L"ASNS";		// Arabic Sign Safha
	case 0x06DD:  return L"AEY";		// Arabic End Of Ayah

	// tag characters
	case 0xE0001: return L"LTAG";		// Language Tag
	case 0xE007F: return L"CTAG";		// Cancel Tag
		
	case 0x0085:  return L"NEL";		// Next Line (EBCDIC)
	case 0x00A0:  return L"NBSP";		// No Break Space
	case 0x034F:  return L"CGJ";		// Combining Grapheme Joiner

	// general punc
	case 0x2000:  return L"NQSP";		// EnQuad
	case 0x2001:  return L"MQSP";		// EmQuad
	case 0x2002:  return L"ENSP";		// EnSpace
	case 0x2003:  return L"NQSP";		// EmSpace
	case 0x2004:  return L"3MSP";		// 3 per em Space
	case 0x2005:  return L"4MSP";		// 4 per em Space
	case 0x2006:  return L"6MPS";		// 6 per em Space
	case 0x2007:  return L"FSP";		// Figure space
	case 0x2008:  return L"PSP";		// Punctuation Space
	case 0x2009:  return L"THSP";		// Thin Space
	case 0x200A:  return L"HSP";		// Hair Space
	case 0x200B:  return L"ZWSP";		// Zero Width Space
	case 0x200C:  return L"ZWNJ";		// Zero Width Non Joiner
	case 0x200D:  return L"ZWJ";		// Zero width Joiner
	case 0x200E:  return L"LRM";		// Left Right Mark
	case 0x200F:  return L"RLM";		// Right Left Mark
	case 0x2010:  return L"HYP";		// Hyphen
	case 0x2011:  return L"NB";			// Non Break Hyphen
	case 0x2015:  return L"HB";			// Horizontal Bar
	case 0x2028:  return L"LSEP";		// Line Separator
	case 0x2029:  return L"PSEP";		// Paragraph Separator
	case 0x202A:  return L"LRE";		// Left to Right Embedding
	case 0x202B:  return L"RLE";		// Right to Left Embedding
	case 0x202C:  return L"PDF";		// Pop Directional Formatting
	case 0x202D:  return L"LRO";		// Left to Right Override
	case 0x202E:  return L"RLO";		// Right to Left Override
	case 0x202F:  return L"NNMSP";		// Narrow No Break Space
	case 0x205F:  return L"MMSP";		// Medium Mathematical Space
	case 0x2060:  return L"WJ";			// Word Joiner
	case 0x2061:  return L"F()";		// Function Application 
	case 0x2062:  return L"X";			// Invisible Times
	case 0x2063:  return L",";			// Invisible Separator
	case 0x206A:  return L"ISS";		// Inhibit Symmetric Swapping
	case 0x206B:  return L"ASS";		// Activate Symmetric Swapping
	case 0x206C:  return L"IAFS";		// Inhibit Arabic Form Shaping
	case 0x206D:  return L"AAFS";		// Activate Arabic Form Shaping
	case 0x206E:  return L"NADS";		// National Digit Shapes
	case 0x206F:  return L"NODS";		// Nominal Digit Shapes

	case 0xFEFF:  return L"ZWNBS";		// Zero Width No Break Space
	case 0xFFA0:  return L"HWHF";		// halfwidth hangul filler
	case 0xFFF9:  return L"ILAA";		// Interlinear Annotation Anchor
	case 0xFFFA:  return L"ILAS";		// Interlinear Annotation Separator
	case 0xFFFB:  return L"ILAT";		// Interlinear Annotation Terminator
	case 0xFFFC:  return L"OBJ";		// Object Replacement Character
	case 0xFFFD:  return L"REP";		// Replacement Character
	case 0:		  return L"RS";			// Record Separator
	case 2:		  return L"US";			// Unit Separator

	case 0x070F:  return L"SAM";		// syriac abbreviation mark (SAM)
	case 0x0F0C:   return L"NB";			// Tibetan-Mark-Delimeter-Tsheg-Bstar (NB)
	case 0x115F:  return L"HCF";		// Hangul-Choseong-Filler (HCF)
	case 0x1160:  return L"HJF";		// Hangul-Jungseong-Filler (HCF)
	case 0x17B4:  return L"KIVAQ";		// Khmer-Vowel-Inherent AQ (KIVAQ)
	case 0x17B5:  return L"KIVAA";		// Khmer-Vowel-Inherent AA (KIVAA)
	case 0x180B:  return L"FVS1";		// Mongolian-Free-Variation-Selector-One   (FVS1)
	case 0x180C:  return L"FVS2";		// Mongolian-Free-Variation-Selector-Two   (FVS2)
	case 0x180D:  return L"FVS3";		// Mongolian-Free-Variation-Selector-Three (FVS3)
	case 0x180E:  return L"MVS";		// Mongolian-Vowel-Separator (MVS)

	case 0x1D159:  return L"MSNNH";		// Music Symbol Null Note Head 
	case 0x1D173:  return L"MSBB";		// Music Symbol begin beam 
	case 0x1D174:  return L"MSEB";		// Music Symbol end beam 
	case 0x1D175:  return L"MSBT";		// Music Symbol begin tie
	case 0x1D176:  return L"MSET";		// Music Symbol end tie 
	case 0x1D177:  return L"MSBS";		// Music Symbol begin slur
	case 0x1D178:  return L"MSES";		// Music Symbol end slur
	case 0x1D179:  return L"MSBP";		// Music Symbol begin phrase
	case 0x1D17A:  return L"MSEP";		// Music Symbol end  phrase
	default:	   return NULL;
	}

	// control-pictures (2400+)
	
	// 3000 Ideographic-Space (IDSP)

	// 3164 Hangul-filler(HF)

	// variation-selectors (FE00 - FE0F, VS1-VS16)
	// tags (E0001 - E007f)
	// variation-selector-supplement (e0100-e01ef , 'vs17 - vs256)
}




static
WCHAR * CtrlStr(DWORD ch, int mode, WCHAR *buf, size_t len)
{
	const WCHAR *ctrlstr;

	switch(mode)
	{
	case USP_CTLCHR_ASC: 
		ctrlstr = CtrlStrRep(ch);
		
		if(ctrlstr)	swprintf(buf, len, L"%s", ctrlstr);
		else		swprintf(buf, len, L"%02X", ch);
		break;

	case USP_CTLCHR_DEC: 
		swprintf(buf, len, L"%02d", ch);
		break;

	case USP_CTLCHR_HEX: 
		swprintf(buf, len, L"%02X", ch);
		break;
	}

	return buf;
}

int CtrlCharWidth(USPFONT *uspFont, HDC hdc, ULONG chValue)
{
	SIZE  size;
	WCHAR str[16];
	int	  mode = USP_CTLCHR_HEX;//ASC;
	
	CtrlStr(chValue, mode, str, 16);
	GetTextExtentPoint32(hdc, str, wcslen(str), &size);

	return size.cx+uspFont->xborder*2 + uspFont->yborder;
}


void InitCtrlChar(HDC hdc, USPFONT *uspFont)
{
	int x, y;

	// create a temporary off-screen bitmap
	HDC		hdcTemp = CreateCompatibleDC(hdc);
	HBITMAP hbmTemp = CreateBitmap(uspFont->tm.tmAveCharWidth, uspFont->tm.tmHeight, 1, 1, 0);
	HANDLE  hbmOld  = SelectObject(hdcTemp, hbmTemp);
	HANDLE  hfnOld	= SelectObject(hdcTemp, uspFont->hFont);

	// black-on-white text
	SetTextColor(hdcTemp,	RGB(0,0,0));
	SetBkColor(hdcTemp,		RGB(255,255,255));
	SetBkMode(hdcTemp,		OPAQUE);

	// give default values just in case the scan fails
	uspFont->capheight = uspFont->tm.tmAscent - uspFont->tm.tmInternalLeading;

	TextOut(hdcTemp, 0, 0, _T("E"), 1);

	// scan downwards looking for the top of the letter 'E'
	for(y = 0; y < uspFont->tm.tmHeight; y++)
	{
		for(x = 0; x < uspFont->tm.tmAveCharWidth; x++)
		{
			COLORREF col;

			if((col = GetPixel(hdcTemp, x, y)) == RGB(0,0,0))
			{
				uspFont->capheight = uspFont->tm.tmAscent - y;
				y = uspFont->tm.tmHeight;
				break;
			}
		}
	}

	TextOut(hdcTemp, 0, 0, _T("0"), 1);

	// do the same for numbers (0) in case they are taller/shorter
	for(y = 0; y < uspFont->tm.tmHeight; y++)
	{
		for(x = 0; x < uspFont->tm.tmAveCharWidth; x++)
		{
			COLORREF col;

			if((col = GetPixel(hdcTemp, x, y)) == RGB(0,0,0))
			{
				uspFont->numheight = uspFont->tm.tmAscent - y;
				y = uspFont->tm.tmHeight;
				break;
			}
		}
	}

	// calculate border-size - 15% of total height
	uspFont->yborder = uspFont->capheight * 15 / 100;
	uspFont->yborder = max(1, uspFont->yborder);		// minimum of 1-pixel
	uspFont->xborder = max(2, uspFont->yborder);		// same as yborder, but min 2-pixel

	// cleanup
	SelectObject(hdcTemp, hbmOld);
	SelectObject(hdcTemp, hfnOld);
	DeleteDC(hdcTemp);
	DeleteObject(hbmTemp);
}

static
void PaintRect(HDC hdc, RECT *rect, COLORREF fill)
{
	fill = SetBkColor(hdc, fill);
	ExtTextOut(hdc, 0, 0, ETO_OPAQUE, rect, 0, 0, 0);	
	SetBkColor(hdc, fill);
}

static
int PaintCtrlChar(USPFONT *uspFont, HDC hdc, int xpos, int ypos, ULONG chValue, COLORREF fg, COLORREF bg)
{
	WCHAR str[8];
	RECT  rect;
	SIZE  size;
	int	  mode = USP_CTLCHR_HEX;
	int   height;

	// get the textual representation of the control-character
	CtrlStr(chValue, mode, str, 8);

	// get the text dimension (only need width)
	GetTextExtentPoint32(hdc, str, wcslen(str), &size);

	// center the control-character "glyph" 
	xpos += uspFont->yborder/2 + 1;

	if(mode == USP_CTLCHR_ASC || mode == USP_CTLCHR_HEX)
		height = max(uspFont->capheight, uspFont->numheight);
	else
		height = uspFont->numheight;

	SetRect(
		&rect, 
		xpos + 1, 
		ypos + uspFont->tm.tmAscent - height - uspFont->yborder,
		xpos + size.cx + uspFont->xborder * 2 - 2, 
		ypos + uspFont->tm.tmAscent + uspFont->yborder
	  );

	//if(rect.top < ypos+1)
	//	rect.top = ypos+1;
	
	// prepare the background 'round' rectangle
	PaintRect(hdc, &rect, bg);
	InflateRect(&rect, 1,-1);
	PaintRect(hdc, &rect, bg);

	// finally paint the text
	SetTextColor(hdc, fg);
	TextOut(hdc, xpos + uspFont->xborder, ypos, str, wcslen(str));

	return 0;
}

//
//	Note there is only 1 control-character per run so the loop below isn't really necessary
//
void PaintCtrlCharRun(USPDATA *uspData, USPFONT *uspFont, ITEM_RUN *itemRun, HDC hdc, int xpos, int ypos)
{
	int i;

	for(i = 0; i < itemRun->glyphCount; i++)
	{
		ATTR attr = uspData->attrList[itemRun->charPos+i];
		
		if(attr.sel)
			PaintCtrlChar(uspFont, hdc, xpos, ypos, itemRun->chcode, uspData->selBG, uspData->selFG);
		else
			PaintCtrlChar(uspFont, hdc, xpos, ypos, itemRun->chcode, attr.bg, attr.fg);

		xpos += uspData->widthList[itemRun->glyphPos+i];
	}
}