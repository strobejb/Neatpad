#ifndef NEATPAD_INCLUDED
#define NEATPAD_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

//
//	Global variables used for Neatpad settings
//
extern LONG		g_nFontSize;
extern BOOL		g_fFontBold;
extern TCHAR	g_szFontName[];
extern LONG		g_nPaddingAbove;
extern LONG		g_nPaddingBelow;
extern LONG		g_fPaddingFlags;
extern COLORREF g_rgbColourList[];
extern COLORREF g_rgbCustColours[];


#define COURIERNEW	1
#define LUCIDACONS	2

//	Global runtime variables
extern HWND		g_hwndTextView;
extern HWND		g_hwndMain;
extern HFONT	g_hFont;

#define REGLOC _T("SOFTWARE\\Catch22\\Neatpad")

//
//	Global functions
//
HFONT EasyCreateFont(int nPointSize, BOOL fBold, TCHAR *szFace);

void ApplyRegSettings();
void LoadRegSettings();
void SaveRegSettings();





#ifdef __cplusplus
}
#endif

#endif