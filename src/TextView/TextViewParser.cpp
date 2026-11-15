// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

//
//	MODULE:		TextViewParser.cpp
//
//	PURPOSE:	Parser for the Syntax-Description-Language
//
//	NOTES:		www.catch22.net
//

#define STRICT
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <tchar.h>
#include "TextView.h"
#include "TextViewInternal.h"

struct Rule
{
	tchar_t		ruleName[32];
	uint32_t	fg;
	uint32_t	bg;
};

enum TEST { testNone, testEqual, testNotEqual, testInRange, testOutRange };

struct State
{
	tchar_t		arg1;
	tchar_t		arg2;
	TEST		test;
	int			next;
};

bool Init(tchar_t *buf, int len)
{
	return true;
}

void Machine(tchar_t *str, int len, int initialState)
{
	State machine[] = 
	{
		{ 'a', 0, testEqual },
		{ 'b', 0, testEqual }
	};

	int state = initialState;

	// match string character-by-character
	for(int i = 0; i < len; i++)
	{
		TEST status = testNone;

		switch(machine[state].test)
		{
		case testEqual: 
			if(str[i] == machine[state].arg1)	
				status = testEqual;

			break;

		case testNotEqual: 
			if(str[i] != machine[state].arg1)	
				status = testNotEqual;
			break;
			
		case testInRange: 
			if(str[i] >= machine[state].arg1 && str[i] <= machine[state].arg2)	
				status = testInRange;

			break;
			
		case testOutRange: 
			if(str[i] < machine[state].arg1 || str[i] > machine[state].arg2)	
				status = testOutRange;

			break;
		}

		if(status == testNone)
			state++;
		else
			state = machine[state].test;
	}
}
