//
//	Neatpad
//
//	Printing.c
//
//	Not currently used but might be in the future
//
//	www.catch22.net
//

#define STRICT

#include <windows.h>
#include <commdlg.h>

static HANDLE g_hDevMode		= NULL;
static HANDLE g_hDevNames		= NULL;
extern HWND	  g_hwndTextView;
static int	  g_nPrinterWidth	= 0;

int GetPrinterWidth(HDC hdcPrn);

#pragma comment(lib, "DelayImp.lib")
#pragma comment(linker, "/DELAYLOAD:Comdlg32.dll")

HDC ShowPrintDlg(HWND hwndParent)
{
	// if Windows 2000 and above
	if((GetVersion() & 0xff) >= 5)
	{
		PRINTDLGEX	pdlgx	= { sizeof(pdlgx), hwndParent, g_hDevMode, g_hDevNames };
		pdlgx.Flags			= PD_RETURNDC | PD_NOPAGENUMS | PD_NOCURRENTPAGE | PD_NOWARNING | PD_HIDEPRINTTOFILE;
		pdlgx.nStartPage	= START_PAGE_GENERAL;

		if(S_OK == PrintDlgEx(&pdlgx) && pdlgx.dwResultAction != PD_RESULT_CANCEL)
		{
			g_hDevMode		= pdlgx.hDevMode;
			g_hDevNames		= pdlgx.hDevNames;
			g_nPrinterWidth = GetPrinterWidth(pdlgx.hDC);

			return pdlgx.hDC;
		}
	}
	// Win9x and WinNT need the old-style print dialog
	else
	{
		PRINTDLG	pdlg	= { sizeof(pdlg),  hwndParent, g_hDevMode, g_hDevNames };
		pdlg.Flags			= PD_RETURNDC | PD_NOPAGENUMS | PD_HIDEPRINTTOFILE;

		if(PrintDlg(&pdlg))
		{
			g_hDevMode		= pdlg.hDevMode;
			g_hDevNames		= pdlg.hDevNames;
			g_nPrinterWidth = GetPrinterWidth(pdlg.hDC);

			return pdlg.hDC;
		}
	}

	return NULL;
}

HDC GetDefaultPrinterDC()
{
	PRINTDLG pdlg	= { sizeof(pdlg),  0, 0, 0 };
	pdlg.Flags		= PD_RETURNDC|PD_RETURNDEFAULT;
	
	if(PrintDlg(&pdlg))
	{
		g_hDevMode		= pdlg.hDevMode;
		g_hDevNames		= pdlg.hDevNames;

		return pdlg.hDC;
	}
	else
	{
		return NULL;
	}
}

int GetPrinterWidth(HDC hdcPrn)
{
	HDC hdcScr	= GetDC(0);

	int nPrinterDPI		= GetDeviceCaps(hdcPrn, LOGPIXELSX);
	int nPrinterWidth	= GetDeviceCaps(hdcPrn, HORZRES);
	int nScreenDPI		= GetDeviceCaps(hdcScr, LOGPIXELSX);

	ReleaseDC(0, hdcScr);

	return nPrinterWidth * nScreenDPI / nPrinterDPI;
}


int GetCurrentPrinterWidth()
{
	if(g_nPrinterWidth == 0)
	{
		HDC hdcPrn		= GetDefaultPrinterDC();
		g_nPrinterWidth = GetPrinterWidth(hdcPrn);

		DeleteDC(hdcPrn);
	}

	return g_nPrinterWidth;
}