//
//	MODULE:		TextView.cpp
//
//	PURPOSE:	Implementation of the TextView control
//
//	NOTES:		www.catch22.net
//

#include <windows.h>
#include <tchar.h>
#include "TextView.h"
#include "TextViewInternal.h"

#define COOKIE_BEGIN 0
#define COOKIE_END   1

//typedef struct LEX;

typedef struct
{
	ATTR		attr;
	ULONG	   type;
	
} TYPE;

typedef struct LEX _LEX;
struct LEX
{
	TYPE *type;

	int	  num_branches;
	_LEX  *branch[1];
};

LEX firstchar[256];

ULONG lexer(ULONG cookie, TCHAR *buf, ULONG len, ATTR *attr)
{
	LEX *state = 0;
	int	  i  = 0;
	ULONG ch = buf[i++];

	// start at the correct place in our state machine
	LEX *node = &state[cookie];

	// look for a match against the current character
//	for(i = 0; i < node->num_branches; i++)
//	{
	//	if(node->branch[i].
//	}
	

	if(ch < 256)
	{

	}


	//switc
	return COOKIE_END;	
}

TCHAR *gettok(TCHAR *ptr, TCHAR *buf, int buflen)
{
	int ch = *ptr++;
	int i = 0;

	if(buf == 0)
		return 0;

	// end of string?
	if(ptr == 0 || ch == 0)
	{
		buf[0] = '\0';	
		return 0;
	}

	// strip any leading whitespace
	// whitspace: { <space> | <tab> | <new line> }
	while(ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')
	{
		if(*ptr == 0)
			return 0;

		ch = *ptr++;
	}

	// found a quote - skip to matching pair
/*	if(ch == '\"')
	{
		ch = *ptr++;
		while(i < buflen && ch != '\0' && ch != '\"' && ch != '\n' && ch != '\r')
		{
			buf[i++] = ch;
			ch = *ptr++;
		}
	}
	else*/
	{
		while(i < buflen && ch)
		{
			if(ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')// || ch == '\"')
				break;

			buf[i++] = ch;

			if(ch = *ptr) ptr++;
		}
	}

	buf[i] = 0;

	return ptr;
}

void xSetStyle(ATTR *attr, ULONG nPos, ULONG nLen, COLORREF fg, COLORREF bg, int bold=0)
{
	for(int i = nPos; i < nPos+nLen; i++)
	{
		attr[i].fg = fg;
		attr[i].bg = bg;
		//attr[i].style = bold;
	}
}

typedef struct
{
	TCHAR firstchar[256];

} SYNTAX_NODE;

int TextView::SyntaxColour(TCHAR *szText, ULONG nTextLen, ATTR *attr)
{
	TCHAR tok[128];

	TCHAR *ptr1 = szText;
	TCHAR *ptr2;

	while((ptr2 = gettok(ptr1, tok, 128)) != 0)
	{
		if(isdigit(tok[0]))
		{
			xSetStyle(attr, ptr1 - szText, ptr2-ptr1, RGB(200,0,0), RGB(255,255,255), 0);
		}
		if(memcmp(tok, _T("if"), 2) == 0 || 
			memcmp(tok, _T("for"), 3) == 0)
		{
			xSetStyle(attr, ptr1 - szText, ptr2-ptr1, RGB(0,0,128), RGB(255,255,255), 1);
		}
		if(tok[0] == '\"' || tok[0] == '<' || tok[0] == '\'')
		{
			xSetStyle(attr, ptr1 - szText, ptr2-ptr1, RGB(128,0,128), RGB(255,255,255));
		}
		if(lstrcmp(tok, _T("#include")) == 0)
		{
			//SetStyle(attr, ptr1 - szText, ptr2 - ptr1, RGB(255,255,0), RGB(255,0,0));
			xSetStyle(attr, ptr1 - szText, ptr2-ptr1, RGB(65,102,190), RGB(255,255,255));
		}		
		else if(lstrcmp(tok, _T("ULONG")) == 0)
		{
			xSetStyle(attr, ptr1 - szText, ptr2 - ptr1, RGB(0,0,255), RGB(255,255,255));
		}
		else if(lstrcmp(tok, _T("//")) == 0)
		{
			xSetStyle(attr, ptr1 - szText, nTextLen - (ptr1-szText), RGB(128,90,20), RGB(255,255,255));
			break;
		}

		ptr1 = ptr2;
	}

	return 0;
}
