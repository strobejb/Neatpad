// Copyright (c) 2004-2026 James Brown, Catch22 Solutions
// SPDX-License-Identifier: MIT

// Neatpad's Appearance page.

#define STRICT
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <tchar.h>
#include <commctrl.h>
#include <shellapi.h>
#include "Neatpad.h"
#include "../../TextView/TextView.h"
#include "resource.h"

#define FONT_COMBO_BOLD_FONT _T("Neatpad.FontComboBoldFont")
#define FONT_COMBO_TYPE_ICON _T("Neatpad.FontComboTypeIcon")
#define PREVIEW_FONT _T("Neatpad.PreviewFont")
#define WM_UPDATE_PREVIEW_FONT (WM_APP + 1)

enum
{
    FONT_ITEM_FIXED_WIDTH = 1,
    FONT_ITEM_TRUETYPE    = 2,
    FONT_ICON_SIZE        = 16
};

static void UpdateStyleControl(HWND hwnd, LONG style)
{
    LRESULT palette = SendDlgItemMessage(hwnd, IDC_PALETTE, CB_GETCURSEL, 0, 0);
    BOOL flexible   = palette == PALETTE_ADWAITA || palette == PALETTE_SOLARIZED;

    SendDlgItemMessage(hwnd, IDC_APPEARANCESTYLE, CB_SETCURSEL, flexible ? style : -1, 0);
    EnableWindow(GetDlgItem(hwnd, IDC_APPEARANCESTYLE), flexible);
}

static int CALLBACK AddFontFamily(const LOGFONT *font, const TEXTMETRIC *metrics, DWORD type, LPARAM data)
{
    HWND combo = (HWND)data;

    if (font->lfFaceName[0] != _T('@') &&
        SendMessage(combo, CB_FINDSTRINGEXACT, -1, (LPARAM)font->lfFaceName) == CB_ERR)
    {
        LRESULT item  = SendMessage(combo, CB_ADDSTRING, 0, (LPARAM)font->lfFaceName);
        // In TEXTMETRIC this flag is set for variable-pitch fonts.
        LPARAM  flags = (metrics->tmPitchAndFamily & TMPF_FIXED_PITCH) ? 0 : FONT_ITEM_FIXED_WIDTH;

        if ((type & TRUETYPE_FONTTYPE) || font->lfOutPrecision == OUT_STROKE_PRECIS)
            flags |= FONT_ITEM_TRUETYPE;

        if (item >= 0)
            SendMessage(combo, CB_SETITEMDATA, (WPARAM)item, flags);
    }

    return 1;
}

static void InitFontDrawing(HWND hwnd, HWND combo, HDC dc)
{
    HFONT   normal = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
    HICON   icon   = (HICON)LoadImage(GetModuleHandle(0), MAKEINTRESOURCE(IDI_ICON2), IMAGE_ICON,
                                    FONT_ICON_SIZE, FONT_ICON_SIZE, LR_SHARED);
    LOGFONT font;
    int     height = FONT_ICON_SIZE;

    if (icon)
        SetProp(hwnd, FONT_COMBO_TYPE_ICON, icon);

    if (normal && GetObject(normal, sizeof(font), &font))
    {
        HFONT bold;

        font.lfWeight = FW_BOLD;
        bold = CreateFontIndirect(&font);
        if (bold && !SetProp(hwnd, FONT_COMBO_BOLD_FONT, bold))
            DeleteObject(bold);
    }

    if (dc)
    {
        HGDIOBJ    previous = normal ? SelectObject(dc, normal) : 0;
        TEXTMETRIC metrics;

        if (GetTextMetrics(dc, &metrics))
            height = max(height, metrics.tmHeight);
        if (previous)
            SelectObject(dc, previous);
    }

    // WM_MEASUREITEM arrives before the dialog font is available.
    SendMessage(combo, CB_SETITEMHEIGHT, (WPARAM)-1, height);
    SendMessage(combo, CB_SETITEMHEIGHT, 0, height);
}

static void DrawFontItem(HWND hwnd, const DRAWITEMSTRUCT *item)
{
    BOOL       disabled = (item->itemState & ODS_DISABLED) != 0;
    BOOL       selected = (item->itemState & ODS_SELECTED) != 0;
    BOOL       focus    = !(item->itemState & ODS_NOFOCUSRECT);
    int        textRole = selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT;
    int        fillRole = selected ? COLOR_HIGHLIGHT : COLOR_WINDOW;
    HFONT      font     = 0;
    HGDIOBJ    previous;
    COLORREF   foreground, background;
    TEXTMETRIC metrics  = {0};
    TCHAR      face[LF_FACESIZE] = {0};

    if (item->itemAction == ODA_FOCUS)
    {
        if (focus)
            DrawFocusRect(item->hDC, &item->rcItem);
        return;
    }

    if (item->itemID != (UINT)-1)
        SendMessage(item->hwndItem, CB_GETLBTEXT, item->itemID, (LPARAM)face);

    if (item->itemData & FONT_ITEM_FIXED_WIDTH)
        font = (HFONT)GetProp(hwnd, FONT_COMBO_BOLD_FONT);
    if (!font)
        font = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);

    previous   = font ? SelectObject(item->hDC, font) : 0;
    foreground = SetTextColor(item->hDC, GetSysColor(disabled ? COLOR_3DSHADOW : textRole));
    background = SetBkColor(item->hDC, GetSysColor(disabled ? COLOR_3DFACE : fillRole));
    GetTextMetrics(item->hDC, &metrics);

    int height = item->rcItem.bottom - item->rcItem.top;
    int top    = item->rcItem.top + (height - metrics.tmHeight) / 2;

    ExtTextOut(item->hDC, item->rcItem.left + FONT_ICON_SIZE + 4, top, ETO_CLIPPED | ETO_OPAQUE,
               &item->rcItem, face, (UINT)lstrlen(face), 0);

    if (item->itemID != (UINT)-1 && (item->itemData & FONT_ITEM_TRUETYPE))
    {
        HICON icon = (HICON)GetProp(hwnd, FONT_COMBO_TYPE_ICON);

        if (icon)
            DrawIconEx(item->hDC, item->rcItem.left + 2, item->rcItem.top + (height - FONT_ICON_SIZE) / 2,
                       icon, FONT_ICON_SIZE, FONT_ICON_SIZE, 0, 0, DI_NORMAL);
    }

    SetTextColor(item->hDC, foreground);
    SetBkColor(item->hDC, background);
    if (previous)
        SelectObject(item->hDC, previous);

    if (focus && (item->itemState & ODS_FOCUS))
        DrawFocusRect(item->hDC, &item->rcItem);
}

static void InitFontControls(HWND hwnd, const OPTIONS *options)
{
    static const UINT sizes[] = {8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 36, 48, 72};
    const TCHAR      *face   = options->szFontName[0] ? options->szFontName : DEFAULT_EDITOR_FONT;
    HWND              combo  = GetDlgItem(hwnd, IDC_FONTLIST);
    HDC               dc     = GetDC(combo);
    LOGFONT           font   = {0};
    LRESULT           selected;
    LRESULT           sizeIndex = CB_ERR;

    InitFontDrawing(hwnd, combo, dc);
    if (dc)
    {
        font.lfCharSet = DEFAULT_CHARSET;
        EnumFontFamiliesEx(dc, &font, AddFontFamily, (LPARAM)combo, 0);
        ReleaseDC(combo, dc);
    }

    selected = SendMessage(combo, CB_FINDSTRINGEXACT, -1, (LPARAM)face);
    // Keep a saved face selectable even if it is missing or was chosen in an older build.
    if (selected == CB_ERR)
        selected = SendMessage(combo, CB_ADDSTRING, 0, (LPARAM)face);
    SendMessage(combo, CB_SETCURSEL, (WPARAM)selected, 0);

    for (UINT i = 0; i < ARRAYSIZE(sizes); ++i)
    {
        TCHAR   size[4];
        LRESULT item;

        wsprintf(size, _T("%u"), sizes[i]);
        item = SendDlgItemMessage(hwnd, IDC_SIZELIST, CB_ADDSTRING, 0, (LPARAM)size);
        if (sizes[i] == (UINT)options->nFontSize)
            sizeIndex = item;
    }

    SendDlgItemMessage(hwnd, IDC_SIZELIST, CB_LIMITTEXT, 3, 0);
    if (sizeIndex >= 0)
        SendDlgItemMessage(hwnd, IDC_SIZELIST, CB_SETCURSEL, (WPARAM)sizeIndex, 0);
    else
        SetDlgItemInt(hwnd, IDC_SIZELIST, options->nFontSize, FALSE);

    CheckDlgButton(hwnd, IDC_BOLD, options->fFontBold ? BST_CHECKED : BST_UNCHECKED);
}

static BOOL ReadEditorFont(HWND hwnd, OPTIONS *options, BOOL reportError)
{
    BOOL  valid;
    UINT  size = GetDlgItemInt(hwnd, IDC_SIZELIST, &valid, FALSE);
    TCHAR face[LF_FACESIZE];

    if (!valid || size < 1 || size > 200)
    {
        if (reportError)
        {
            MessageBox(hwnd, _T("Enter a font size between 1 and 200 points."), APP_TITLE, MB_OK | MB_ICONWARNING);
            SetFocus(GetDlgItem(hwnd, IDC_SIZELIST));
        }
        return FALSE;
    }

    if (!GetDlgItemText(hwnd, IDC_FONTLIST, face, ARRAYSIZE(face)))
    {
        if (reportError)
            SetFocus(GetDlgItem(hwnd, IDC_FONTLIST));
        return FALSE;
    }

    lstrcpy(options->szFontName, face);
    options->nFontSize   = size;
    options->fFontBold   = IsDlgButtonChecked(hwnd, IDC_BOLD) == BST_CHECKED;
    return TRUE;
}

static void PreviewFont(HWND hwnd, const OPTIONS *options)
{
    HWND    preview = GetDlgItem(hwnd, IDC_PREVIEW);
    OPTIONS chosen  = *options;
    HFONT   font, previous;

    if (!preview || !ReadEditorFont(hwnd, &chosen, FALSE))
        return;

    font = CreateEditorFont(chosen.szFontName, chosen.nFontSize, chosen.fFontBold, chosen.fFontItalic, 100);
    if (!font)
        return;

    previous = (HFONT)GetProp(hwnd, PREVIEW_FONT);
    if (!SetProp(hwnd, PREVIEW_FONT, font))
    {
        DeleteObject(font);
        return;
    }

    SendMessage(preview, WM_SETFONT, (WPARAM)font, TRUE);
    if (previous)
        DeleteObject(previous);
}

static void PreviewPalette(HWND hwnd, const OPTIONS *options)
{
    LONG choice = (LONG)SendDlgItemMessage(hwnd, IDC_PALETTE, CB_GETCURSEL, 0, 0);
    LONG style  = (LONG)SendDlgItemMessage(hwnd, IDC_APPEARANCESTYLE, CB_GETCURSEL, 0, 0);

    if (choice != CB_ERR)
        ApplyEditorPalette(GetDlgItem(hwnd, IDC_PREVIEW), choice, style == CB_ERR ? options->nAppearanceStyle : style);
}

static void PreviewSpacing(HWND hwnd)
{
    BOOL valid;
    UINT spacing = GetDlgItemInt(hwnd, IDC_LINESPACING, &valid, FALSE);

    if (valid && spacing <= MAX_LINE_SPACING)
        TextView_SetLineSpacing(GetDlgItem(hwnd, IDC_PREVIEW), spacing / 2, (spacing + 1) / 2);
}

static void InitPreview(HWND hwnd, const OPTIONS *options)
{
    static const TCHAR sample[] = _T("// Editor preview\r\n")
                                  _T("const char *app = \"Neatpad\";\r\n")
                                  _T("int tab_width = 4;");
    HWND preview = GetDlgItem(hwnd, IDC_PREVIEW);

    TextView_SetEditMode(preview, MODE_READONLY);
    TextView_SetStyle(preview, TXS_HIGHLIGHTCHANGES | TXS_CHANGEMARKERS, 0);
    TextView_SetSyntaxResource(preview, IDR_SYNTAX_C);
    SendMessage(preview, EM_REPLACESEL, FALSE, (LPARAM)sample);
    SendMessage(preview, EM_SETSEL, 0, 0);
    SendMessage(preview, EM_SCROLLCARET, 0, 0);

    PreviewFont(hwnd, options);
    PreviewPalette(hwnd, options);
    PreviewSpacing(hwnd);
}

static void InitAppearancePage(HWND hwnd, const OPTIONS *options)
{
    SendDlgItemMessage(hwnd, IDC_APPEARANCESTYLE, CB_ADDSTRING, 0, (LPARAM)_T("Follow system"));
    SendDlgItemMessage(hwnd, IDC_APPEARANCESTYLE, CB_ADDSTRING, 0, (LPARAM)_T("Light"));
    SendDlgItemMessage(hwnd, IDC_APPEARANCESTYLE, CB_ADDSTRING, 0, (LPARAM)_T("Dark"));

    SendDlgItemMessage(hwnd, IDC_PALETTE, CB_ADDSTRING, 0, (LPARAM)_T("Adwaita"));
    SendDlgItemMessage(hwnd, IDC_PALETTE, CB_ADDSTRING, 0, (LPARAM)_T("Classic"));
    SendDlgItemMessage(hwnd, IDC_PALETTE, CB_ADDSTRING, 0, (LPARAM)_T("Solarized"));
    if (g_fHasCustomPalette)
        SendDlgItemMessage(hwnd, IDC_PALETTE, CB_ADDSTRING, 0, (LPARAM)_T("Custom"));

    SendDlgItemMessage(hwnd, IDC_PALETTE, CB_SETCURSEL, options->nPalette, 0);
    UpdateStyleControl(hwnd, options->nAppearanceStyle);

    InitFontControls(hwnd, options);

    SetDlgItemInt(hwnd, IDC_LINESPACING, options->nLineSpacing, FALSE);
    SendDlgItemMessage(hwnd, IDC_LINESPACING, EM_LIMITTEXT, 2, 0);
    SendDlgItemMessage(hwnd, IDC_SPIN1, UDM_SETRANGE32, 0, MAX_LINE_SPACING);
    InitPreview(hwnd, options);
}

INT_PTR CALLBACK AppearanceOptionsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    OPTIONS *options = (OPTIONS *)GetWindowLongPtr(hwnd, DWLP_USER);

    switch (msg)
    {
    case WM_INITDIALOG:
        options = (OPTIONS *)((PROPSHEETPAGE *)lParam)->lParam;
        SetWindowLongPtr(hwnd, DWLP_USER, (LONG_PTR)options);
        InitAppearancePage(hwnd, options);
        return TRUE;

    case WM_DRAWITEM:
        if (wParam == IDC_FONTLIST)
        {
            DrawFontItem(hwnd, (const DRAWITEMSTRUCT *)lParam);
            SetWindowLongPtr(hwnd, DWLP_MSGRESULT, TRUE);
            return TRUE;
        }
        break;

    case WM_NCDESTROY:
    {
        HFONT bold    = (HFONT)RemoveProp(hwnd, FONT_COMBO_BOLD_FONT);
        HFONT preview = (HFONT)RemoveProp(hwnd, PREVIEW_FONT);

        if (bold)
            DeleteObject(bold);
        if (preview)
            DeleteObject(preview);
        RemoveProp(hwnd, FONT_COMBO_TYPE_ICON);
        break;
    }

    case WM_UPDATE_PREVIEW_FONT:
        PreviewFont(hwnd, options);
        return TRUE;

    case WM_COMMAND:
    {
        WORD control = LOWORD(wParam);
        WORD event   = HIWORD(wParam);

        if (control == IDC_PALETTE && event == CBN_SELCHANGE)
        {
            LRESULT style = SendDlgItemMessage(hwnd, IDC_APPEARANCESTYLE, CB_GETCURSEL, 0, 0);

            if (style != CB_ERR)
                options->nAppearanceStyle = (LONG)style;

            UpdateStyleControl(hwnd, options->nAppearanceStyle);
            PreviewPalette(hwnd, options);
            return TRUE;
        }
        if (control == IDC_APPEARANCESTYLE && event == CBN_SELCHANGE)
        {
            PreviewPalette(hwnd, options);
            return TRUE;
        }
        if ((control == IDC_FONTLIST && event == CBN_SELCHANGE) ||
            (control == IDC_SIZELIST && (event == CBN_SELCHANGE || event == CBN_EDITCHANGE)) ||
            (control == IDC_BOLD && event == BN_CLICKED))
        {
            // An editable combo updates its text after CBN_SELCHANGE returns.
            PostMessage(hwnd, WM_UPDATE_PREVIEW_FONT, 0, 0);
            return TRUE;
        }
        if (control == IDC_LINESPACING && event == EN_CHANGE)
        {
            PreviewSpacing(hwnd);
            return TRUE;
        }
        break;
    }

    case WM_NOTIFY:
        if (((NMHDR *)lParam)->code == PSN_SETACTIVE)
            OptionsPageActivated(hwnd);

        if (((NMHDR *)lParam)->code == PSN_APPLY)
        {
            BOOL valid;
            UINT spacing = GetDlgItemInt(hwnd, IDC_LINESPACING, &valid, FALSE);
            LRESULT style = SendDlgItemMessage(hwnd, IDC_APPEARANCESTYLE, CB_GETCURSEL, 0, 0);

            if (!valid || spacing > MAX_LINE_SPACING)
            {
                MessageBox(hwnd, _T("Enter line spacing between 0 and 40 pixels."), APP_TITLE, MB_OK | MB_ICONWARNING);
                SetFocus(GetDlgItem(hwnd, IDC_LINESPACING));
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE);
                return TRUE;
            }

            if (!ReadEditorFont(hwnd, options, TRUE))
            {
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE);
                return TRUE;
            }

            options->nLineSpacing     = spacing;
            options->nPalette         = (LONG)SendDlgItemMessage(hwnd, IDC_PALETTE, CB_GETCURSEL, 0, 0);
            if (style != CB_ERR)
                options->nAppearanceStyle = (LONG)style;

            SetWindowLongPtr(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }

    return FALSE;
}
