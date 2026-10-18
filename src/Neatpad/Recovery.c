// Recovery and session lifetime. TextView owns the C++ persistence boundary.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <tchar.h>
#include <stdint.h>
#include <stdlib.h>
#include "../TextView/TextView.h"
#include "Neatpad.h"
#include "resource.h"

static TCHAR     store[MAX_PATH];
static HANDLE    lease = INVALID_HANDLE_VALUE;
static ULONGLONG revision, written, changed, attempted, pending;
static BOOL      warned, keepSession;

static BOOL RecoveryPath(TCHAR *out, const TCHAR *directory, const TCHAR *leaf)
{
    if (lstrlen(directory) + lstrlen(leaf) + 2 > MAX_PATH)
        return FALSE;

    lstrcpy(out, directory);
    lstrcat(out, _T("\\"));
    lstrcat(out, leaf);

    return TRUE;
}

static BOOL RecoveryRoot(TCHAR *out)
{
#ifdef RECOVERY_TESTS
    // Test builds isolate the real controller from the user's recovery stores.
    extern BOOL RecoveryTestRoot(TCHAR * directory);

    return RecoveryTestRoot(out);
#else
    TCHAR local[MAX_PATH], app[MAX_PATH];

    if (FAILED(SHGetFolderPath(0, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, 0, 0, local)))
        return FALSE;

    if (!RecoveryPath(app, local, _T("Neatpad")))
        return FALSE;

    if (!CreateDirectory(app, 0) && GetLastError() != ERROR_ALREADY_EXISTS)
        return FALSE;

    if (!RecoveryPath(out, app, _T("Recovery")))
        return FALSE;

    return CreateDirectory(out, 0) || GetLastError() == ERROR_ALREADY_EXISTS;
#endif
}

static HANDLE LockStore(const TCHAR *directory)
{
    TCHAR path[MAX_PATH];

    if (!RecoveryPath(path, directory, _T("lock")))
        return INVALID_HANDLE_VALUE;

    return CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN, 0);
}

static BOOL HasMarker(const TCHAR *directory, const TCHAR *name)
{
    TCHAR path[MAX_PATH];

    return RecoveryPath(path, directory, name) && GetFileAttributes(path) != INVALID_FILE_ATTRIBUTES;
}

static BOOL RemoveMarker(const TCHAR *name)
{
    TCHAR path[MAX_PATH];

    if (!RecoveryPath(path, store, name))
        return FALSE;

    return DeleteFile(path) || GetLastError() == ERROR_FILE_NOT_FOUND;
}

static BOOL WriteMarker(const TCHAR *name)
{
    TCHAR  path[MAX_PATH];
    HANDLE file;
    BOOL   ok;

    if (!store[0])
        return TRUE;
    if (!RecoveryPath(path, store, name))
        return FALSE;

    file = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS,
                      FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_WRITE_THROUGH, 0);

    if (file == INVALID_HANDLE_VALUE)
        return FALSE;

    ok = FlushFileBuffers(file);
    CloseHandle(file);
    return ok;
}

// Called after the view has released its backing files. Never recurse or
// follow directory reparse points; a store contains only codec-owned files.
static void RemoveStore(const TCHAR *directory)
{
    TCHAR           pattern[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATA data;
    HANDLE          find;

    if (!RecoveryPath(pattern, directory, _T("*")))
        return;
    find = FindFirstFile(pattern, &data);

    if (find != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && RecoveryPath(path, directory, data.cFileName))
                DeleteFile(path);
        } while (FindNextFile(find, &data));

        FindClose(find);
    }
    RemoveDirectory(directory);
}

static BOOL CreateStore(void)
{
    TCHAR  root[MAX_PATH], directory[MAX_PATH], name[40];
    GUID   guid;
    HANDLE file;

    if (!RecoveryRoot(root) || FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(&guid, name, ARRAYSIZE(name)))
        return FALSE;
    if (!RecoveryPath(directory, root, name) || !CreateDirectory(directory, 0))
        return FALSE;

    file = LockStore(directory);

    if (file == INVALID_HANDLE_VALUE)
    {
        RemoveDirectory(directory);
        return FALSE;
    }

    lstrcpy(store, directory);
    lease = file;

    return TRUE;
}

static void ResetCounters(void)
{
    revision = written = changed = attempted = pending = 0;
    warned = keepSession = FALSE;
}

static BOOL Checkpoint(BOOL preserveSources)
{
    TEXTVIEWRECOVERY state    = {0};
    BOOL             modified = TextView_GetModify(g_hwndTextView);

    if (!store[0] && !CreateStore())
        return FALSE;

    state.directory       = store;
    state.preserveSources = preserveSources;

    if (g_szFileTitle[0])
        lstrcpyn(state.filename, g_szFileName, MAX_PATH);

    if (!modified && !WriteMarker(_T("clean")))
        return FALSE;

    if (!TextView_SaveState(g_hwndTextView, &state))
        return FALSE;

    if (modified && !RemoveMarker(_T("clean")))
        return FALSE;

    if (!RemoveMarker(_T("session")))
        return FALSE;

    written = revision;
    warned  = FALSE;

    return TRUE;
}

void RecoveryChanged(void)
{
    if (revision == written)
        pending = GetTickCount64();

    ++revision;
    changed = GetTickCount64();
}

BOOL RecoveryBeforeSave(void)
{
    BOOL ok = Checkpoint(TRUE);

    if (!ok)
        ShowNotifyBar(NOTIFY_RECOVERY_WRITE_FAILED);

    return ok;
}

void RecoverySaved(void)
{
    if (!WriteMarker(_T("clean")))
    {
        ShowNotifyBar(NOTIFY_RECOVERY_WRITE_FAILED);
        return;
    }

    written = revision;
}

// New/Open/Revert/Discard retire the previous document after consent.
void RecoveryReset(void)
{
    if (store[0])
    {
        WriteMarker(_T("clean"));
        CloseHandle(lease);
        lease = INVALID_HANDLE_VALUE;
        RemoveStore(store);
        store[0] = 0;
    }

    ResetCounters();
}

// Always capture the final edit, even before the idle timer has fired. FALSE
// leaves the document open so WM_CLOSE can fall back to Save/Discard/Cancel.
BOOL RecoveryClose(void)
{
    keepSession = FALSE;

    if (!TextView_GetModify(g_hwndTextView))
        return WriteMarker(_T("clean"));

    if (!Checkpoint(FALSE) || !WriteMarker(_T("session")))
    {
        ShowNotifyBar(NOTIFY_RECOVERY_WRITE_FAILED);
        return FALSE;
    }

    keepSession = TRUE;
    return TRUE;
}

// After destroying the view: retain a successfully checkpointed session, or
// clean up a saved/discarded document. Release the lease only at this point.
void RecoveryShutdown(void)
{
    if (!keepSession)
    {
        RecoveryReset();
        return;
    }

    CloseHandle(lease);
    lease    = INVALID_HANDLE_VALUE;
    store[0] = 0;
    ResetCounters();
}

void RecoveryTick(void)
{
    ULONGLONG now;

    if (!g_hwndTextView || !IsWindowEnabled(g_hwndMain))
        return;

    if (!TextView_GetModify(g_hwndTextView))
    {
        if (written != revision)
            RecoverySaved();

        return;
    }

    now = GetTickCount64();

    if (revision == written || (attempted && now - attempted < (warned ? 5000 : 1000)) ||
        (now - changed < 1500 && now - pending < 10000))
        return;

    attempted = now;

    if (Checkpoint(FALSE))
        pending = now;
    else if (!warned)
    {
        ShowNotifyBar(NOTIFY_RECOVERY_WRITE_FAILED);
        warned = TRUE;
    }
}

typedef struct
{
    TCHAR    directory[MAX_PATH];
    FILETIME time;
} RECOVERYCANDIDATE;

static int __cdecl NewestFirst(const void *a, const void *b)
{
    return -CompareFileTime(&((const RECOVERYCANDIDATE *)a)->time, &((const RECOVERYCANDIDATE *)b)->time);
}

BOOL RecoveryRestore(HWND hwnd)
{
    TCHAR                     root[MAX_PATH], pattern[MAX_PATH], directory[MAX_PATH], path[MAX_PATH];
    RECOVERYCANDIDATE        *items = NULL;
    size_t                    count = 0, capacity = 0, i;
    WIN32_FIND_DATA           data;
    WIN32_FILE_ATTRIBUTE_DATA manifest;
    HANDLE                    find, file;
    BOOL                      failed = FALSE, originalUnavailable = FALSE, restored = FALSE;

    if (!RecoveryRoot(root) || !RecoveryPath(pattern, root, _T("*")))
        return FALSE;

    find = FindFirstFile(pattern, &data);

    if (find == INVALID_HANDLE_VALUE)
        return FALSE;

    do
    {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || data.cFileName[0] != _T('{'))
            continue;

        if (!RecoveryPath(directory, root, data.cFileName) || HasMarker(directory, _T("clean")) ||
            !RecoveryPath(path, directory, _T("state.bin")) ||
            !GetFileAttributesEx(path, GetFileExInfoStandard, &manifest))
            continue;

        if (count == capacity)
        {
            size_t             next = capacity ? capacity * 2 : 8;
            RECOVERYCANDIDATE *grown;

            if (next <= capacity || next > SIZE_MAX / sizeof(*items))
            {
                failed = TRUE;
                break;
            }

            grown = items ? HeapReAlloc(GetProcessHeap(), 0, items, next * sizeof(*items))
                          : HeapAlloc(GetProcessHeap(), 0, next * sizeof(*items));

            if (!grown)
            {
                failed = TRUE;
                break;
            }

            items    = grown;
            capacity = next;
        }

        lstrcpy(items[count].directory, directory);
        items[count++].time = manifest.ftLastWriteTime;
    } while (FindNextFile(find, &data));

    FindClose(find);

    if (count > 1)
        qsort(items, count, sizeof(*items), NewestFirst);

    for (i = 0; i < count; ++i)
    {
        TEXTVIEWRECOVERY state = {0};
        BOOL             session;

        file = LockStore(items[i].directory);

        if (file == INVALID_HANDLE_VALUE)
            continue;

        if (HasMarker(items[i].directory, _T("clean")))
        {
            CloseHandle(file);
            continue;
        }

        session         = HasMarker(items[i].directory, _T("session"));
        state.directory = items[i].directory;

        if (TextView_LoadState(g_hwndTextView, &state))
        {
            lstrcpy(store, items[i].directory);
            lease = file;
            RemoveMarker(_T("session"));
            written   = revision;
            attempted = GetTickCount64();
            ShowRecoveredFile(hwnd, state.filename);

            if (session)
                ShowNotifyBar(state.filename[0] ? NOTIFY_SESSION_RESTORED : NOTIFY_SESSION_RESTORED_UNTITLED);

            restored = TRUE;
            break;
        }

        CloseHandle(file);
        failed              = TRUE;
        originalUnavailable = originalUnavailable || state.originalUnavailable;
    }

    if (items)
        HeapFree(GetProcessHeap(), 0, items);

    if (!restored && failed)
        ShowNotifyBar(originalUnavailable ? NOTIFY_RECOVERY_ORIGINAL_CHANGED : NOTIFY_RECOVERY_READ_FAILED);

    return restored;
}

void RecoveryRevert(HWND hwnd)
{
    if (!g_szFileTitle[0])
    {
        SendMessage(hwnd, WM_COMMAND, IDM_FILE_NEW, 0);
        return;
    }

    if (GetFileAttributes(g_szFileName) == INVALID_FILE_ATTRIBUTES)
    {
        MessageBox(hwnd, _T("The saved file is unavailable. Your recovered changes are still open."), APP_TITLE,
                   MB_OK | MB_ICONINFORMATION);
        return;
    }

    NeatpadReloadFile(hwnd);
}
