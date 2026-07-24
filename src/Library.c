#include <windows.h>
#include <tlhelp32.h>

#define IDR_ICON_ON 1
#define IDR_ICON_OFF 2

#define IDM_AUTO 101
#define IDM_ON 102
#define IDM_OFF 103

#define WM_TRAYSTATE (WM_APP + 1)

// A private key for the manual tray override, kept separate from Steam's own
// HKCU\SOFTWARE\Valve\Steam\RunningAppID. Writing directly into Steam's key
// would fake "a game is running" to Steam itself (friends status, playtime
// tracking, etc.) whenever the override is used, in either direction.
#define OVERRIDE_SUBKEY L"SOFTWARE\\NoSteamWebHelper"
#define OVERRIDE_VALUE L"Override"
#define OVERRIDE_AUTO 0 // defer to Steam's real RunningAppID
#define OVERRIDE_ON 1   // force CEF enabled, even mid-game
#define OVERRIDE_OFF 2  // force CEF disabled, even at the main menu

static DWORD WINAPI TrayThreadProc(LPVOID lpParameter);
static DWORD WINAPI HookThreadProc(LPVOID lpParameter);
static DWORD WINAPI WatcherThreadProc(LPVOID lpParameter);

// Written only by the tray thread (WM_CREATE / WM_DESTROY), read by the
// watcher thread. A HWND is pointer-sized, so aligned reads/writes are atomic;
// volatile stops the compiler from caching a stale value across the watcher
// loop. Worst case a post races window destruction and is dropped, which is
// harmless: WM_CREATE recomputes the state itself.
static HWND volatile hTrayWnd;

// DllMainCRTStartup's hLibModule, not GetModuleHandleW(NULL): this code runs
// inside steam.exe's process, and GetModuleHandleW(NULL) would resolve to
// steam.exe's own module rather than this DLL, which is where the icon
// resources actually live. Loading resources against the wrong module
// returns NULL, leaving the tray icon blank.
static HINSTANCE hModule;

static volatile LONG gTrayThreadStarted;
static volatile LONG gWatcherThreadStarted;

static DWORD GetOverride(VOID)
{
    DWORD value = OVERRIDE_AUTO;
    RegGetValueW(HKEY_CURRENT_USER, OVERRIDE_SUBKEY, OVERRIDE_VALUE, RRF_RT_REG_DWORD, NULL, &value,
                 &((DWORD){sizeof(DWORD)}));
    return value;
}

static BOOL IsSteamAppRunning(VOID)
{
    // RunningAppID is the numeric app ID of the running game (0 when none), not
    // a boolean; any nonzero value means a game is running. We read it into a
    // DWORD and normalise to TRUE/FALSE so the "game running" contract is
    // explicit and doesn't lean on sizeof(BOOL) == sizeof(DWORD).
    DWORD appId = 0;
    RegGetValueW(HKEY_CURRENT_USER, L"SOFTWARE\\Valve\\Steam", L"RunningAppID", RRF_RT_REG_DWORD, NULL, &appId,
                 &((DWORD){sizeof(DWORD)}));
    return appId != 0;
}

static BOOL ComputeDisabled(VOID)
{
    DWORD override = GetOverride();
    if (override == OVERRIDE_ON)
        return FALSE;
    if (override == OVERRIDE_OFF)
        return TRUE;
    return IsSteamAppRunning();
}

// Loaded once and cached: LoadImageW(IMAGE_ICON) hands back a fresh,
// non-shared HICON on every call, and reloading it on each state change
// would leak one GDI handle per toggle over a multi-day Steam session.
static HICON hIconOn, hIconOff;

static VOID EnsureTrayIconsLoaded(VOID)
{
    if (hIconOn)
        return;
    int cx = GetSystemMetrics(SM_CXSMICON), cy = GetSystemMetrics(SM_CYSMICON);
    hIconOn = LoadImageW(hModule, MAKEINTRESOURCEW(IDR_ICON_ON), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
    hIconOff = LoadImageW(hModule, MAKEINTRESOURCEW(IDR_ICON_OFF), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
}

// ---- Dark owner-drawn tray menu --------------------------------------------
// The tray popup is owner-drawn rather than themed via uxtheme's (undocumented)
// SetPreferredAppMode: that call is process-wide, and since this DLL lives
// inside steam.exe it would recolor Steam's own menus too. Owner-drawing paints
// only our menu and leaves the host process untouched.
#define DARK_BG RGB(0x2B, 0x2B, 0x2B)      // menu background
#define DARK_BG_SEL RGB(0x3D, 0x3D, 0x40)  // hovered item
#define DARK_TEXT RGB(0xF0, 0xF0, 0xF0)    // item text
#define DARK_SEP RGB(0x45, 0x45, 0x45)     // separator line
#define MENU_GUTTER 28                      // left checkmark column width (px)
#define MENU_PAD_RIGHT 18                   // right padding (px)

typedef struct
{
    const WCHAR *text;
    BOOL separator;
} DARKMENUITEM;

static const DARKMENUITEM kMiAuto = {L"Automatic", FALSE};
static const DARKMENUITEM kMiSep = {NULL, TRUE};
static const DARKMENUITEM kMiOn = {L"On", FALSE};
static const DARKMENUITEM kMiOff = {L"Off", FALSE};

static HFONT hMenuFont;      // the system menu font (SPI_GETNONCLIENTMETRICS)
static HFONT hMenuCheckFont; // Marlett, for the check glyph ('a')

static VOID EnsureMenuFonts(VOID)
{
    if (hMenuFont)
        return;

    NONCLIENTMETRICSW ncm = {.cbSize = sizeof(NONCLIENTMETRICSW)};
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICSW), &ncm, 0))
        hMenuFont = CreateFontIndirectW(&ncm.lfMenuFont);
    if (!hMenuFont)
        hMenuFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    // Marlett's 'a' is the standard menu checkmark; size it to the menu font.
    LONG h = (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICSW), &ncm, 0) &&
              ncm.lfMenuFont.lfHeight)
                 ? ncm.lfMenuFont.lfHeight
                 : -12;
    hMenuCheckFont = CreateFontW(h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH,
                                 L"Marlett");
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static NOTIFYICONDATAW nid = {.cbSize = sizeof(NOTIFYICONDATAW),
                                  .uCallbackMessage = WM_USER,
                                  .uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP};

    static UINT msgTaskbarCreated = WM_NULL;

    switch (uMsg)
    {
    case WM_CREATE:
    {
        msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
        nid.hWnd = hWnd;
        hTrayWnd = hWnd;
        EnsureTrayIconsLoaded();

        BOOL disabled = ComputeDisabled();
        nid.hIcon = disabled ? hIconOff : hIconOn;
        lstrcpyW(nid.szTip, disabled ? L"Steam WebHelper - CEF Disabled" : L"Steam WebHelper - CEF Enabled");
        Shell_NotifyIconW(NIM_ADD, &nid);
        break;
    }

    case WM_TRAYSTATE:
    {
        BOOL disabled = (BOOL)wParam;
        nid.hIcon = disabled ? hIconOff : hIconOn;
        lstrcpyW(nid.szTip, disabled ? L"Steam WebHelper - CEF Disabled" : L"Steam WebHelper - CEF Enabled");
        Shell_NotifyIconW(NIM_MODIFY, &nid);
        break;
    }

    case WM_USER:
        // WM_RBUTTONUP, not DOWN: acting on button-up matches shell convention
        // and avoids the menu opening while the button is still held.
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU)
        {
            DWORD override = GetOverride();

            EnsureMenuFonts();
            HMENU hMenu = CreatePopupMenu();
            if (!hMenu)
                break;
            AppendMenuW(hMenu, MF_OWNERDRAW | (override == OVERRIDE_AUTO ? MF_CHECKED : 0), IDM_AUTO,
                        (LPCWSTR)&kMiAuto);
            AppendMenuW(hMenu, MF_OWNERDRAW | MF_DISABLED, 0, (LPCWSTR)&kMiSep);
            AppendMenuW(hMenu, MF_OWNERDRAW | (override == OVERRIDE_ON ? MF_CHECKED : 0), IDM_ON, (LPCWSTR)&kMiOn);
            AppendMenuW(hMenu, MF_OWNERDRAW | (override == OVERRIDE_OFF ? MF_CHECKED : 0), IDM_OFF, (LPCWSTR)&kMiOff);
            SetForegroundWindow(hWnd);

            POINT pt = {0};
            GetCursorPos(&pt);
            UINT cmd = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON | TPM_RETURNCMD, pt.x,
                                      pt.y, 0, hWnd, NULL);
            DestroyMenu(hMenu);
            // Documented TrackPopupMenu quirk for notification-area menus:
            // without a posted no-op message the second right-click can fail
            // to open the menu (or it sticks open) because the window never
            // "wakes" after the first menu loop.
            PostMessageW(hWnd, WM_NULL, 0, 0);

            // cmd is 0 both when the menu is dismissed without a choice and on
            // failure; only write the override on an explicit pick so
            // dismissing the menu can no longer silently change state.
            if (cmd == IDM_AUTO || cmd == IDM_ON || cmd == IDM_OFF)
            {
                DWORD value = cmd == IDM_ON ? OVERRIDE_ON : cmd == IDM_OFF ? OVERRIDE_OFF : OVERRIDE_AUTO;
                RegSetKeyValueW(HKEY_CURRENT_USER, OVERRIDE_SUBKEY, OVERRIDE_VALUE, REG_DWORD, &value, sizeof(DWORD));
            }
        }
        break;

    case WM_MEASUREITEM:
    {
        LPMEASUREITEMSTRUCT mis = (LPMEASUREITEMSTRUCT)lParam;
        const DARKMENUITEM *it = (const DARKMENUITEM *)mis->itemData;
        if (!it)
            break;
        if (it->separator)
        {
            mis->itemHeight = 7;
            mis->itemWidth = 0;
        }
        else
        {
            EnsureMenuFonts();
            HDC hdc = GetDC(hWnd);
            HGDIOBJ old = SelectObject(hdc, hMenuFont);
            SIZE sz = {0};
            GetTextExtentPoint32W(hdc, it->text, lstrlenW(it->text), &sz);
            SelectObject(hdc, old);
            ReleaseDC(hWnd, hdc);
            mis->itemHeight = sz.cy < 18 ? 24 : sz.cy + 10;
            mis->itemWidth = MENU_GUTTER + sz.cx + MENU_PAD_RIGHT;
        }
        return TRUE;
    }

    case WM_DRAWITEM:
    {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        const DARKMENUITEM *it = (const DARKMENUITEM *)dis->itemData;
        if (!it)
            break;

        BOOL selected = (dis->itemState & ODS_SELECTED) && !it->separator;
        HBRUSH bg = CreateSolidBrush(selected ? DARK_BG_SEL : DARK_BG);
        FillRect(dis->hDC, &dis->rcItem, bg);
        DeleteObject(bg);

        if (it->separator)
        {
            HPEN pen = CreatePen(PS_SOLID, 1, DARK_SEP);
            HGDIOBJ oldPen = SelectObject(dis->hDC, pen);
            int y = (dis->rcItem.top + dis->rcItem.bottom) / 2;
            MoveToEx(dis->hDC, dis->rcItem.left + 6, y, NULL);
            LineTo(dis->hDC, dis->rcItem.right - 6, y);
            SelectObject(dis->hDC, oldPen);
            DeleteObject(pen);
        }
        else
        {
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, DARK_TEXT);

            if ((dis->itemState & ODS_CHECKED) && hMenuCheckFont)
            {
                HGDIOBJ oldF = SelectObject(dis->hDC, hMenuCheckFont);
                RECT gr = {dis->rcItem.left, dis->rcItem.top, dis->rcItem.left + MENU_GUTTER, dis->rcItem.bottom};
                DrawTextW(dis->hDC, L"a", 1, &gr, DT_CENTER | DT_VCENTER | DT_SINGLELINE); // Marlett 'a' = check
                SelectObject(dis->hDC, oldF);
            }

            HGDIOBJ oldF = SelectObject(dis->hDC, hMenuFont);
            RECT tr = dis->rcItem;
            tr.left += MENU_GUTTER;
            DrawTextW(dis->hDC, it->text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dis->hDC, oldF);
        }
        return TRUE;
    }

    case WM_DESTROY:
        // Remove the tray icon explicitly instead of leaving a ghost for the
        // shell to garbage-collect on the next mouse-over. Reached on a clean
        // Steam shutdown / restart into a new session.
        Shell_NotifyIconW(NIM_DELETE, &nid);
        hTrayWnd = NULL;
        // Release the cached menu fonts. DEFAULT_GUI_FONT is a stock object and
        // DeleteObject is a harmless no-op on it, so the fallback path is safe.
        if (hMenuFont)
        {
            DeleteObject(hMenuFont);
            hMenuFont = NULL;
        }
        if (hMenuCheckFont)
        {
            DeleteObject(hMenuCheckFont);
            hMenuCheckFont = NULL;
        }
        PostQuitMessage(0);
        break;

    default:
        // Explorer restarted (crash, or user killed it): the notification area
        // is brand new and our icon is gone, so re-add it with current state.
        if (uMsg == msgTaskbarCreated)
            Shell_NotifyIconW(NIM_ADD, &nid);
        break;
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

static VOID KillWebHelperChildren(VOID)
{
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE)
        return;

    DWORD selfPid = GetCurrentProcessId();
    PROCESSENTRY32W pe = {.dwSize = sizeof(PROCESSENTRY32W)};
    if (Process32FirstW(hSnap, &pe))
        do
        {
            // steamwebhelper.exe is a direct child of steam.exe (this process);
            // its own CEF renderer/GPU children die with it, so terminating the
            // parent is sufficient in practice.
            if (pe.th32ParentProcessID == selfPid &&
                CompareStringOrdinal(pe.szExeFile, -1, L"steamwebhelper.exe", -1, TRUE) == CSTR_EQUAL)
            {
                HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (hProcess)
                {
                    TerminateProcess(hProcess, EXIT_SUCCESS);
                    CloseHandle(hProcess);
                }
            }
        } while (Process32NextW(hSnap, &pe));

    CloseHandle(hSnap);
}

// Apply the desired CEF state to Steam's UI thread, tracking whether we have
// it currently suspended so the suspend count stays balanced. SuspendThread
// keeps a *count*, not a flag: without this guard two "disabled" events in a
// row would suspend twice while a single later "enabled" event resumes only
// once, leaving Steam's thread stuck suspended (a frozen client). We transition
// only on a real change and drain the resume count fully as belt-and-braces.
static VOID ApplyThreadState(HANDLE hThread, BOOL disabled, BOOL *pSuspended)
{
    if (disabled && !*pSuspended)
    {
        // Only record the suspension if it actually happened; SuspendThread
        // returns (DWORD)-1 on failure.
        if (SuspendThread(hThread) != (DWORD)-1)
            *pSuspended = TRUE;
    }
    else if (!disabled && *pSuspended)
    {
        // ResumeThread returns the *previous* suspend count: 1 means this call
        // brought it to 0 and the thread runs again; (DWORD)-1 means failure.
        // The failure case must break out explicitly - (DWORD)-1 compares
        // greater than 1 unsigned, so a bare `> 1` loop would spin at 100% CPU
        // forever if the handle ever went bad.
        for (;;)
        {
            DWORD prev = ResumeThread(hThread);
            if (prev == (DWORD)-1 || prev <= 1)
                break;
        }
        *pSuspended = FALSE;
    }
}

// Runs on its own thread (not inside the WinEvent callback - see
// WinEventProc). Owns the Steam UI thread handle passed as its parameter and
// closes it on exit. Watches Steam's RunningAppID and our override value via
// registry change notifications - zero CPU while idle, no polling - and
// applies the resulting CEF state.
static DWORD WINAPI WatcherThreadProc(LPVOID lpParameter)
{
    HANDLE hThread = (HANDLE)lpParameter;
    HKEY hSteamKey = NULL, hOverrideKey = NULL;
    HANDLE hEvents[2] = {NULL, NULL};
    BOOL suspended = FALSE;

    // Bail out rather than arming a wait on a NULL key, which would fail
    // instantly and spin this thread at 100% CPU.
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\Valve\\Steam", 0, KEY_NOTIFY | KEY_QUERY_VALUE, &hSteamKey) !=
            ERROR_SUCCESS ||
        RegCreateKeyExW(HKEY_CURRENT_USER, OVERRIDE_SUBKEY, 0, NULL, REG_OPTION_NON_VOLATILE,
                        KEY_NOTIFY | KEY_QUERY_VALUE, NULL, &hOverrideKey, NULL) != ERROR_SUCCESS)
        goto cleanup;

    // Two keys need watching (Steam's real state and our private override), so
    // this waits on both asynchronously instead of blocking synchronously on
    // one. Every handle and every arm is checked: an unchecked NULL event or a
    // failed RegNotifyChangeKeyValue would leave a wait that either errors in
    // a tight loop or sleeps forever while the helper silently stops working.
    hEvents[0] = CreateEventW(NULL, FALSE, FALSE, NULL);
    hEvents[1] = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!hEvents[0] || !hEvents[1])
        goto cleanup;
    if (RegNotifyChangeKeyValue(hSteamKey, FALSE, REG_NOTIFY_CHANGE_LAST_SET, hEvents[0], TRUE) != ERROR_SUCCESS ||
        RegNotifyChangeKeyValue(hOverrideKey, FALSE, REG_NOTIFY_CHANGE_LAST_SET, hEvents[1], TRUE) != ERROR_SUCCESS)
        goto cleanup;

    // Apply the current state once up front. A game already running - or one
    // that launches in the small gap between the tray window being created and
    // the notifications being armed - would otherwise not take effect until the
    // *next* change, leaving steamwebhelper.exe alive for a cycle.
    {
        BOOL disabled = ComputeDisabled();
        ApplyThreadState(hThread, disabled, &suspended);
        HWND hWnd = hTrayWnd;
        if (hWnd)
            PostMessageW(hWnd, WM_TRAYSTATE, disabled, 0);
        if (disabled)
            KillWebHelperChildren();
    }

    for (;;)
    {
        DWORD wait = WaitForMultipleObjects(2, hEvents, FALSE, INFINITE);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_OBJECT_0 + 1)
            break;

        // Notifications are one-shot: re-arm the key that fired before acting
        // on it, so a change landing while we work still wakes the next wait.
        // If re-arming ever fails, stop cleanly instead of waiting forever on
        // an event that can no longer be signalled.
        if (RegNotifyChangeKeyValue(wait == WAIT_OBJECT_0 ? hSteamKey : hOverrideKey, FALSE,
                                    REG_NOTIFY_CHANGE_LAST_SET, hEvents[wait - WAIT_OBJECT_0], TRUE) != ERROR_SUCCESS)
            break;

        BOOL disabled = ComputeDisabled();
        ApplyThreadState(hThread, disabled, &suspended);
        HWND hWnd = hTrayWnd;
        if (hWnd)
            PostMessageW(hWnd, WM_TRAYSTATE, disabled, 0);

        if (disabled)
            KillWebHelperChildren();
    }

cleanup:
    // Never leave Steam's thread suspended if we ever stop watching.
    ApplyThreadState(hThread, FALSE, &suspended);

    if (hEvents[0])
        CloseHandle(hEvents[0]);
    if (hEvents[1])
        CloseHandle(hEvents[1]);
    if (hSteamKey)
        RegCloseKey(hSteamKey);
    if (hOverrideKey)
        RegCloseKey(hOverrideKey);
    CloseHandle(hThread);
    // Released last: from this point a future vguiPopupWindow event may start
    // a fresh watcher, so all shared work above must already be finished.
    InterlockedExchange(&gWatcherThreadStarted, 0);
    return EXIT_SUCCESS;
}

static VOID CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild,
                                  DWORD dwEventThread, DWORD dwmsEventTime)
{
    (void)hWinEventHook;
    (void)event;
    (void)idObject;
    (void)idChild;
    (void)dwmsEventTime;

    // Room to spare so a class name that merely shares a 15-char prefix with
    // "vguiPopupWindow" can't be truncated into a false match.
    WCHAR szClassName[64] = {0};
    GetClassNameW(hwnd, szClassName, ARRAYSIZE(szClassName));

    if (CompareStringOrdinal(L"vguiPopupWindow", -1, szClassName, -1, FALSE) != CSTR_EQUAL ||
        GetWindowTextLengthW(hwnd) < 1)
        return;

    if (InterlockedCompareExchange(&gTrayThreadStarted, 1, 0) == 0)
    {
        HANDLE hTrayThread = CreateThread(NULL, 0, TrayThreadProc, NULL, 0, NULL);
        if (hTrayThread)
            CloseHandle(hTrayThread);
        else
            InterlockedExchange(&gTrayThreadStarted, 0);
    }

    if (InterlockedCompareExchange(&gWatcherThreadStarted, 1, 0) != 0)
        return;

    HANDLE hSteamThread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, dwEventThread);
    if (!hSteamThread)
    {
        InterlockedExchange(&gWatcherThreadStarted, 0);
        return;
    }

    // The watch loop runs on its own thread rather than inline here: this
    // callback executes on the hook thread's message loop, and blocking it in
    // an infinite wait would stall that pump - no further WinEvents would ever
    // be delivered, so the watcher could never be restarted after a failure.
    HANDLE hWatcher = CreateThread(NULL, 0, WatcherThreadProc, hSteamThread, 0, NULL);
    if (hWatcher)
        CloseHandle(hWatcher);
    else
    {
        CloseHandle(hSteamThread);
        InterlockedExchange(&gWatcherThreadStarted, 0);
    }
}

// Hosts the tray icon window and its message loop.
static DWORD WINAPI TrayThreadProc(LPVOID lpParameter)
{
    (void)lpParameter;

    WNDCLASSW wc = {.lpszClassName = L"NoSteamWebHelperTray", .hInstance = hModule, .lpfnWndProc = WndProc};
    ATOM atom = RegisterClassW(&wc);
    if (!atom)
        return EXIT_FAILURE;
    if (!CreateWindowExW(WS_EX_LEFT | WS_EX_LTRREADING, (LPCWSTR)(ULONG_PTR)atom, NULL, WS_OVERLAPPED, 0, 0, 0, 0,
                         NULL, NULL, hModule, NULL))
        return EXIT_FAILURE;

    MSG msg = {0};
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return EXIT_SUCCESS;
}

// Installs the WinEvent hook and pumps messages for it. SetWinEventHook with
// WINEVENT_OUTOFCONTEXT requires the installing thread to run a message loop;
// callbacks are delivered through it.
static DWORD WINAPI HookThreadProc(LPVOID lpParameter)
{
    (void)lpParameter;

    if (!SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE, NULL, WinEventProc, GetCurrentProcessId(), 0,
                         WINEVENT_OUTOFCONTEXT))
        return EXIT_FAILURE;

    MSG msg = {0};
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return EXIT_SUCCESS;
}

BOOL WINAPI DllMainCRTStartup(HINSTANCE hLibModule, DWORD dwReason, LPVOID lpReserved)
{
    (void)lpReserved;

    if (dwReason == DLL_PROCESS_ATTACH)
    {
        hModule = hLibModule;
        DisableThreadLibraryCalls(hLibModule);
        // CreateThread is safe here (the new thread only starts running after
        // the loader lock is released); the thread itself does no loading.
        HANDLE hThread = CreateThread(NULL, 0, HookThreadProc, NULL, 0, NULL);
        if (hThread)
            CloseHandle(hThread);
    }
    return TRUE;
}
