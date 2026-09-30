//
//	TextView public interface - include this file
//	when you want a TextView edit control
//
#ifndef TEXTVIEW_INCLUDED
#define TEXTVIEW_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

//
//	TextView API declared here
//
BOOL InitTextView();
HWND CreateTextView(HWND hwndParent);

//
//	TextView Window Messages defined here
//

//
//	TextView Macros defined here
//
#define TEXTVIEW_CLASS _T("TextView32")


#ifdef __cplusplus
}
#endif

#endif