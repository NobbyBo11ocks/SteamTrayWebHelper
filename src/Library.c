// README states Windows 10/11 as the only supported OS; pin the SDK target to
// match rather than relying on the toolchain's default, since that default is
// otherwise unspecified and QueryFullProcessImageNameW (Vista+) below would
// silently fail to declare on an older implied target.
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <tlhelp32.h>

#define IDR_ICON_ON 1
#define IDR_ICON_OFF 2

#define IDM_AUTO 101
#define IDM_ON 102
#define IDM_OFF 103

#define IDH_TOGGLE 1 // global hotkey id: Ctrl+Alt+L flips the forced On/Off override

#define WM_TRAYSTATE (WM_APP + 1)
#define WM_REHOTKEY (WM_APP + 2) // override key changed; re-read the hotkey binding

// Optional overrides for the toggle hotkey, stored beside the mode override.
// Absent or unusable values fall back to Ctrl+Alt+L, so a bad edit costs the
// custom binding rather than the hotkey itself.
#define HOTKEY_VALUE_MODS L"HotkeyModifiers"
#define HOTKEY_VALUE_VK L"Hotkey"
#define HOTKEY_DEFAULT_MODS (MOD_CONTROL | MOD_ALT)
#define HOTKEY_DEFAULT_VK 'L'

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

// Armed by the watcher thread when CEF auto-restores after a game exits
// (never on an explicit "On" pick - see the loop in WatcherThreadProc), and
// consumed by WinEventProc's EVENT_OBJECT_SHOW handling below, which hides
// the first Steam window that shows itself afterwards. This is what "-silent"
// suppresses at Steam's own startup; nothing plays that role for CEF coming
// back mid-session, so Steam pops its main window back to the foreground
// every time. A GetTickCount64 deadline, not just a boolean, so a show that
// was already in flight when the grace period lapses is left alone rather
// than suppressed indefinitely by a flag nothing ever clears. 64-bit reads/
// writes of an aligned variable are atomic on the x86-64 target this builds
// for, so no separate lock is needed between the two threads touching it.
static volatile ULONGLONG gSuppressShowUntilTick;

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

// The metrics the cached icons were rasterised for, so a settings change can
// tell an actual DPI move from the many unrelated WM_SETTINGCHANGEs.
static int gIconCx, gIconCy;

static VOID EnsureTrayIconsLoaded(VOID)
{
    if (hIconOn)
        return;
    int cx = GetSystemMetrics(SM_CXSMICON), cy = GetSystemMetrics(SM_CYSMICON);
    hIconOn = LoadImageW(hModule, MAKEINTRESOURCEW(IDR_ICON_ON), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
    hIconOff = LoadImageW(hModule, MAKEINTRESOURCEW(IDR_ICON_OFF), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
    gIconCx = cx;
    gIconCy = cy;
}

// ---- Dark owner-drawn tray menu --------------------------------------------
// The tray popup is owner-drawn rather than themed via uxtheme's (undocumented)
// SetPreferredAppMode: that call is process-wide, and since this DLL lives
// inside steam.exe it would recolor Steam's own menus too. Owner-drawing paints
// only our menu and leaves the host process untouched.
//
// Every colour below is copied from Steam's own client, not approximated:
// C:\Program Files (x86)\Steam\resource\styles\steam.styles, the [colors]
// block and the Menu/MenuItem/MenuItem:hover/MenuItem:selected/MenuSeparator
// rules. That file is what Steam itself reads to paint its native VGUI
// menus (including this tray icon's own right-click menu pre-CEF), so this
// is Steam's actual palette, sourced directly, not a lookalike.
#define DARK_BG_TOP RGB(56, 60, 68)         // MenuBG1 - gradient top
#define DARK_BG RGB(41, 45, 51)             // MenuBG2 - gradient bottom / flat fill past y=140
#define DARK_BG_SEL RGB(25, 55, 84)         // Focus - "background color of any selected menu or list item"
#define DARK_TEXT RGB(168, 172, 179)        // Label - normal item text
#define DARK_TEXT_HOVER RGB(255, 255, 255)  // MenuItem:hover textcolor=white
#define DARK_TEXT_CHECKED RGB(213, 217, 234) // TextHover - MenuItem:selected textcolor
#define DARK_SEP RGB(76, 84, 93)            // Divider
#define DARK_ACCENT RGB(102, 192, 244)      // Steam's link/accent blue (#66c0f4), for the checkmark glyph
#define MENU_GRADIENT_SPAN 140              // Menu.render_bg: gradient(x0,y0,x1,y0+140,...), flat below
#define MENU_CORNER_RADIUS 6                // Menu.corner_rounding=1 - VGUI doesn't expose the exact px value
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

// The popup size the rounded region was last cut for, or 0x0 for "not shaped
// yet". Reset before each TrackPopupMenu and updated by the WM_ENTERIDLE
// handler - see there for why this is tracked rather than applied every time.
// Touched only by the tray thread.
static LONG gMenuShapedW, gMenuShapedH;

// Steam's Menu.render_bg is a real per-pixel gradient over the popup's first
// MENU_GRADIENT_SPAN px, flat fill below that. WM_DRAWITEM only hands us one
// item's rect at a time, not the whole popup, but DRAWITEMSTRUCT.rcItem is
// already in menu-client coordinates (not item-relative), so using an item's
// own vertical midpoint here reproduces the same gradient across the whole
// menu - just resolved once per item instead of per pixel row, which is
// indistinguishable at this item height and avoids a msimg32 GradientFill
// dependency this build doesn't otherwise need.
static COLORREF MenuGradientAt(int y)
{
    if (y >= MENU_GRADIENT_SPAN)
        return DARK_BG;
    int r = GetRValue(DARK_BG_TOP) + (GetRValue(DARK_BG) - GetRValue(DARK_BG_TOP)) * y / MENU_GRADIENT_SPAN;
    int g = GetGValue(DARK_BG_TOP) + (GetGValue(DARK_BG) - GetGValue(DARK_BG_TOP)) * y / MENU_GRADIENT_SPAN;
    int b = GetBValue(DARK_BG_TOP) + (GetBValue(DARK_BG) - GetBValue(DARK_BG_TOP)) * y / MENU_GRADIENT_SPAN;
    return RGB(r, g, b);
}

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

// Binds the toggle hotkey from the registry, or Ctrl+Alt+L when nothing usable
// is stored. Safe to call repeatedly: RegisterHotKey does NOT replace an
// existing binding on the same window and id - both would stay live - so the
// old one is explicitly removed first.
static VOID RegisterToggleHotkey(HWND hWnd)
{
    DWORD mods = HOTKEY_DEFAULT_MODS, vk = HOTKEY_DEFAULT_VK;
    RegGetValueW(HKEY_CURRENT_USER, OVERRIDE_SUBKEY, HOTKEY_VALUE_MODS, RRF_RT_REG_DWORD, NULL, &mods,
                 &((DWORD){sizeof(DWORD)}));
    RegGetValueW(HKEY_CURRENT_USER, OVERRIDE_SUBKEY, HOTKEY_VALUE_VK, RRF_RT_REG_DWORD, NULL, &vk,
                 &((DWORD){sizeof(DWORD)}));

    // Drop anything that isn't a real modifier bit, and require at least one
    // plus a plausible virtual-key code. A combination with no modifier would
    // swallow a bare keypress system-wide, which is not something a stray
    // registry value should be able to do.
    mods &= MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN;
    if (!mods || vk == 0 || vk > 0xFF)
    {
        mods = HOTKEY_DEFAULT_MODS;
        vk = HOTKEY_DEFAULT_VK;
    }

    UnregisterHotKey(hWnd, IDH_TOGGLE);
    // MOD_NOREPEAT is forced on regardless of the stored value: holding the
    // combination down should not flood WM_HOTKEY and flip the override
    // repeatedly. Failure (another app owns the combination) is ignored, same
    // as the rest of this file's non-critical setup - the menu still works.
    RegisterHotKey(hWnd, IDH_TOGGLE, mods | MOD_NOREPEAT, vk);
}

// Drops the cached icons and fonts so the next Ensure* call rebuilds them at
// the current metrics.
static VOID InvalidateTrayVisuals(VOID)
{
    if (hIconOn)
    {
        DestroyIcon(hIconOn);
        hIconOn = NULL;
    }
    if (hIconOff)
    {
        DestroyIcon(hIconOff);
        hIconOff = NULL;
    }
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
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static NOTIFYICONDATAW nid = {.cbSize = sizeof(NOTIFYICONDATAW),
                                  .uCallbackMessage = WM_USER,
                                  .uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP};

    static UINT msgTaskbarCreated = WM_NULL;

    // Last state pushed to the tray, so a rebuild after a DPI change can put
    // the correct icon straight back without waiting for the next state change.
    static BOOL trayDisabled;

    switch (uMsg)
    {
    case WM_CREATE:
    {
        msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
        nid.hWnd = hWnd;
        hTrayWnd = hWnd;
        EnsureTrayIconsLoaded();

        BOOL disabled = ComputeDisabled();
        trayDisabled = disabled;
        nid.hIcon = disabled ? hIconOff : hIconOn;
        lstrcpyW(nid.szTip, disabled ? L"Steam WebHelper - CEF Disabled" : L"Steam WebHelper - CEF Enabled");
        Shell_NotifyIconW(NIM_ADD, &nid);

        RegisterToggleHotkey(hWnd);
        break;
    }

    case WM_TRAYSTATE:
    {
        BOOL disabled = (BOOL)wParam;
        // lParam says whether the watcher actually managed to put Steam into
        // that state. Without it the tooltip reports the *intent* - what the
        // override and RunningAppID say it should be - and so keeps claiming
        // "CEF Disabled" even when the suspend was rejected and CEF is plainly
        // still running. The icon still shows the requested mode, since that is
        // what the menu selected; the tooltip is where the disagreement goes.
        BOOL applied = (BOOL)lParam;
        trayDisabled = disabled;
        nid.hIcon = disabled ? hIconOff : hIconOn;
        lstrcpyW(nid.szTip, disabled ? L"Steam WebHelper - CEF Disabled" : L"Steam WebHelper - CEF Enabled");
        if (!applied)
            lstrcatW(nid.szTip, L" (not applied)");
        Shell_NotifyIconW(NIM_MODIFY, &nid);
        break;
    }

    case WM_REHOTKEY:
        // The override key changed, which is also where the hotkey binding
        // lives, so re-read it. Cheap enough to do unconditionally rather than
        // diffing the values, and it means a remap takes effect immediately
        // instead of at the next Steam restart.
        RegisterToggleHotkey(hWnd);
        break;

    case WM_SETTINGCHANGE:
        // Icons are rasterised once at SM_CXSMICON and the menu font is read
        // once from SPI_GETNONCLIENTMETRICS, so changing display scaling left a
        // blurred, wrongly sized tray icon until Steam restarted. WM_SETTINGCHANGE
        // fires for every system-wide setting, most of them irrelevant, so this
        // reacts to the metric that actually matters rather than guessing which
        // SPI_ codes to filter on - a no-op unless the icon size really moved.
        if (GetSystemMetrics(SM_CXSMICON) != gIconCx || GetSystemMetrics(SM_CYSMICON) != gIconCy)
        {
            InvalidateTrayVisuals();
            EnsureTrayIconsLoaded();
            nid.hIcon = trayDisabled ? hIconOff : hIconOn;
            Shell_NotifyIconW(NIM_MODIFY, &nid);
        }
        break;

    case WM_HOTKEY:
        // Ctrl+Alt+L: flip the forced override rather than just toggling CEF
        // directly, so the change persists and is visible the same way a menu
        // pick is - through the private override key, which the watcher thread
        // already reacts to (icon/tooltip update, thread suspend/resume, and
        // killing the webhelper children all happen from that single path).
        if (wParam == IDH_TOGGLE)
        {
            DWORD value = ComputeDisabled() ? OVERRIDE_ON : OVERRIDE_OFF;
            RegSetKeyValueW(HKEY_CURRENT_USER, OVERRIDE_SUBKEY, OVERRIDE_VALUE, REG_DWORD, &value, sizeof(DWORD));
        }
        break;

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
            gMenuShapedW = gMenuShapedH = 0; // re-arm the rounding in WM_ENTERIDLE
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

    case WM_ENTERIDLE:
        // Cosmetic match for steam.styles' Menu.corner_rounding=1. Fires
        // repeatedly while TrackPopupMenu's modal loop is idle-waiting;
        // #32768 is the fixed, long-stable system class every native popup
        // menu uses - there's no other way to reach the popup's own HWND,
        // since TrackPopupMenu never hands it to the owner directly. This is
        // a best-effort, non-critical touch: if another app's own native
        // menu happens to be open on the desktop at this exact instant,
        // FindWindow could round the wrong one, but this only runs for the
        // few hundred ms the tray menu itself is open.
        //
        // Cut once per popup rather than on every idle. SetWindowRgn's
        // bRedraw=TRUE invalidates the menu, and the repaint that follows drops
        // the modal loop straight back to idle, sending another WM_ENTERIDLE
        // here - so re-regioning unconditionally spins a paint/idle cycle for
        // as long as the menu stays open. Keying off the size the region was
        // last cut for (zeroed before each TrackPopupMenu) settles after one
        // pass, while still re-cutting if the popup ever reports a different
        // size later - so a region can't end up stale against its window.
        if (wParam == MSGF_MENU)
        {
            HWND hMenuWnd = FindWindowW(L"#32768", NULL);
            RECT rc;
            if (hMenuWnd && GetWindowRect(hMenuWnd, &rc))
            {
                LONG w = rc.right - rc.left, h = rc.bottom - rc.top;
                if (w != gMenuShapedW || h != gMenuShapedH)
                {
                    HRGN hRgn = CreateRoundRectRgn(0, 0, w, h, MENU_CORNER_RADIUS, MENU_CORNER_RADIUS);
                    if (hRgn && SetWindowRgn(hMenuWnd, hRgn, TRUE))
                    {
                        gMenuShapedW = w;
                        gMenuShapedH = h;
                    }
                    else if (hRgn)
                        DeleteObject(hRgn); // ownership only transfers to the window on success
                }
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
        // Steam's MenuItem/MenuItem:hover both say bgcolor=none - the highlight
        // bar comes from the generic "Focus" fill underneath, so it's a flat
        // solid, not part of the gradient. Everywhere else, the item shows
        // whatever slice of the popup-wide gradient falls at its own position.
        HBRUSH bg = CreateSolidBrush(selected ? DARK_BG_SEL
                                               : MenuGradientAt((dis->rcItem.top + dis->rcItem.bottom) / 2));
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
            BOOL checked = dis->itemState & ODS_CHECKED;
            SetBkMode(dis->hDC, TRANSPARENT);

            if (checked && hMenuCheckFont)
            {
                HGDIOBJ oldF = SelectObject(dis->hDC, hMenuCheckFont);
                SetTextColor(dis->hDC, DARK_ACCENT);
                RECT gr = {dis->rcItem.left, dis->rcItem.top, dis->rcItem.left + MENU_GUTTER, dis->rcItem.bottom};
                DrawTextW(dis->hDC, L"a", 1, &gr, DT_CENTER | DT_VCENTER | DT_SINGLELINE); // Marlett 'a' = check
                SelectObject(dis->hDC, oldF);
            }

            // Matches steam.styles: MenuItem:hover always wins with white text
            // regardless of checked state; unhovered falls back to the active
            // mode's slightly brighter TextHover shade, or plain Label text.
            SetTextColor(dis->hDC, selected ? DARK_TEXT_HOVER : checked ? DARK_TEXT_CHECKED : DARK_TEXT);
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
        UnregisterHotKey(hWnd, IDH_TOGGLE);
        hTrayWnd = NULL;
        // Release the cached icons and menu fonts. DEFAULT_GUI_FONT is a stock
        // object and DeleteObject is a harmless no-op on it, so the font
        // fallback path is safe; the icons come from LoadImage without
        // LR_SHARED, so they are ours to destroy.
        InvalidateTrayVisuals();
        PostQuitMessage(0);
        break;

    default:
        // Explorer restarted (crash, or user killed it): the notification area
        // is brand new and our icon is gone, so re-add it with current state.
        // The nonzero test matters: msgTaskbarCreated is WM_NULL (0) until
        // WM_CREATE runs, and stays 0 if RegisterWindowMessageW ever fails, so
        // without it every WM_NULL - including the one the tray menu posts to
        // itself after TrackPopupMenu - would land here instead.
        if (msgTaskbarCreated && uMsg == msgTaskbarCreated)
            Shell_NotifyIconW(NIM_ADD, &nid);
        break;
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

// Directory steam.exe itself runs from, trailing backslash kept so it can be
// used as a prefix. Populated lazily on first use rather than at DLL load:
// GetModuleFileNameW(NULL, ...) reads the host process's own image path,
// which is only meaningful once steam.exe is actually running (it always is
// by the time this DLL's hooks fire, but not necessarily at DLL_PROCESS_ATTACH).
static WCHAR gSteamDir[MAX_PATH];
static DWORD gSteamDirChars;

static BOOL EnsureSteamDir(VOID)
{
    if (gSteamDirChars)
        return TRUE;

    WCHAR path[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (len == 0 || len == MAX_PATH)
        return FALSE;

    // Manual scan for the last backslash: no CRT is linked (-nostdlib), so
    // wcsrchr/strrchr aren't available here.
    DWORD lastSlash = 0;
    BOOL found = FALSE;
    for (DWORD i = 0; i < len; i++)
        if (path[i] == L'\\')
        {
            lastSlash = i;
            found = TRUE;
        }
    if (!found)
        return FALSE;

    // Manual copy, not CopyMemory: RtlCopyMemory expands to a genuine memcpy
    // call on MinGW headers, and no CRT is linked in (-nostdlib).
    DWORD chars = lastSlash + 1; // keep the trailing backslash for prefix matching
    for (DWORD i = 0; i < chars; i++)
        gSteamDir[i] = path[i];
    // Published last, and only once the buffer above is fully written:
    // gSteamDirChars is what every reader tests before touching gSteamDir, so
    // setting it first would briefly advertise a directory that isn't there yet.
    gSteamDirChars = chars;
    return TRUE;
}

// Steam runs one steamwebhelper.exe as a direct child; a handful at most across
// client versions. The cap only bounds the array, and anything beyond it is
// simply left alone rather than overflowing it.
#define MAX_WEBHELPERS 32

// Opens a handle to every steamwebhelper.exe that is a direct child of this
// process, returning how many were collected. Split out from the terminate
// below because everything here - the toolhelp snapshot especially - allocates
// from the process heap, and this must therefore run while Steam's UI thread is
// still running. See the call site in WatcherThreadProc for why that matters.
static UINT CollectWebHelperChildren(HANDLE *out, UINT max)
{
    UINT count = 0;

    if (!EnsureSteamDir())
        return 0;

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE)
        return 0;

    DWORD selfPid = GetCurrentProcessId();
    PROCESSENTRY32W pe = {.dwSize = sizeof(PROCESSENTRY32W)};
    if (Process32FirstW(hSnap, &pe))
        do
        {
            if (count >= max)
                break;
            // steamwebhelper.exe is a direct child of steam.exe (this process);
            // its own CEF renderer/GPU children die with it, so terminating the
            // parent is sufficient in practice.
            if (pe.th32ParentProcessID == selfPid &&
                CompareStringOrdinal(pe.szExeFile, -1, L"steamwebhelper.exe", -1, TRUE) == CSTR_EQUAL)
            {
                HANDLE hProcess =
                    OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                if (hProcess)
                {
                    // Belt-and-braces: only terminate if the child's own image
                    // actually lives in Steam's install directory, not merely a
                    // same-named process that happens to be a direct child.
                    WCHAR imgPath[MAX_PATH];
                    DWORD imgPathLen = MAX_PATH;
                    if (QueryFullProcessImageNameW(hProcess, 0, imgPath, &imgPathLen) &&
                        imgPathLen > gSteamDirChars &&
                        CompareStringOrdinal(imgPath, (INT)gSteamDirChars, gSteamDir, (INT)gSteamDirChars, TRUE) ==
                            CSTR_EQUAL)
                        out[count++] = hProcess;
                    else
                        CloseHandle(hProcess);
                }
            }
        } while (Process32NextW(hSnap, &pe));

    CloseHandle(hSnap);
    return count;
}

// Kills what CollectWebHelperChildren opened. Deliberately nothing but kernel
// calls on handles that already exist: no allocation, so this half is the part
// that is safe to run while Steam's UI thread is suspended. Always call it for
// a successful collect, even on a path that decides not to kill - it owns the
// handles and closing them is not optional.
static VOID TerminateCollected(HANDLE *handles, UINT count)
{
    for (UINT i = 0; i < count; i++)
    {
        TerminateProcess(handles[i], EXIT_SUCCESS);
        CloseHandle(handles[i]);
    }
}

// Apply the desired CEF state to Steam's UI thread, tracking whether we have
// it currently suspended so the suspend count stays balanced. SuspendThread
// keeps a *count*, not a flag: without this guard two "disabled" events in a
// row would suspend twice while a single later "enabled" event resumes only
// once, leaving Steam's thread stuck suspended (a frozen client). We transition
// only on a real change and drain the resume count fully as belt-and-braces.
// Returns whether the requested state is actually in effect, so the caller can
// stop the tray reporting an override it never managed to apply.
static BOOL ApplyThreadState(HANDLE hThread, BOOL disabled, BOOL *pSuspended)
{
    if (disabled && !*pSuspended)
    {
        // Only record the suspension if it actually happened; SuspendThread
        // returns (DWORD)-1 on failure.
        if (SuspendThread(hThread) == (DWORD)-1)
            return FALSE;
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
            if (prev == (DWORD)-1)
                return FALSE;
            if (prev <= 1)
                break;
        }
        *pSuspended = FALSE;
    }
    return TRUE;
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
    //
    // Collect before suspending, terminate after. Doing the whole kill after the
    // suspend - as this did originally - takes a toolhelp snapshot while Steam's
    // UI thread is frozen, and if that thread happened to be holding the process
    // heap lock at the moment it stopped, the snapshot's own allocation waits on
    // a lock that can now never be released: Steam hangs, permanently. Collecting
    // first keeps every allocation outside the suspended window, while still
    // terminating after the suspend so Steam cannot immediately respawn what we
    // just killed.
    {
        BOOL disabled = ComputeDisabled();
        HANDLE kill[MAX_WEBHELPERS];
        UINT killCount = disabled ? CollectWebHelperChildren(kill, MAX_WEBHELPERS) : 0;
        BOOL applied = ApplyThreadState(hThread, disabled, &suspended);
        TerminateCollected(kill, killCount);
        HWND hWnd = hTrayWnd;
        if (hWnd)
            PostMessageW(hWnd, WM_TRAYSTATE, disabled, applied);
    }

    // Steam's UI thread joins the wait as a third object. A thread handle is
    // signalled when the thread terminates, so if Steam tears its UI thread down
    // - a client update, a UI restart - this wakes instead of sitting on a
    // handle that every SuspendThread will now reject, which is what used to
    // make the helper silently stop working for the rest of the session. Exiting
    // clears gWatcherThreadStarted, so the next vguiPopupWindow that Steam
    // creates starts a fresh watcher bound to the new thread.
    HANDLE waits[3] = {hEvents[0], hEvents[1], hThread};

    for (;;)
    {
        DWORD wait = WaitForMultipleObjects(3, waits, FALSE, INFINITE);
        if (wait == WAIT_OBJECT_0 + 2)
            break; // Steam's UI thread is gone; rebuild against its replacement
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
        BOOL wasSuspended = suspended;

        // Same collect-then-suspend-then-terminate ordering as the initial
        // apply above; see there for why the snapshot must not happen while
        // Steam's UI thread is frozen.
        HANDLE kill[MAX_WEBHELPERS];
        UINT killCount = disabled ? CollectWebHelperChildren(kill, MAX_WEBHELPERS) : 0;
        BOOL applied = ApplyThreadState(hThread, disabled, &suspended);
        TerminateCollected(kill, killCount);

        // Only arm the suppression window on an *automatic* restore (game
        // exited, override still Auto). An explicit "On" pick means the user
        // asked for CEF back themselves, so Steam showing its window is the
        // expected, wanted outcome there, not something to hide.
        if (wasSuspended && !disabled && GetOverride() == OVERRIDE_AUTO)
            gSuppressShowUntilTick = GetTickCount64() + 8000;

        HWND hWnd = hTrayWnd;
        if (hWnd)
        {
            PostMessageW(hWnd, WM_TRAYSTATE, disabled, applied);
            // The hotkey binding lives under the same key as the mode override,
            // so a change to it arrives on this event too. Re-reading only when
            // that key fired keeps a Steam RunningAppID change from pointlessly
            // re-registering the hotkey on every game launch and exit.
            if (wait == WAIT_OBJECT_0 + 1)
                PostMessageW(hWnd, WM_REHOTKEY, 0, 0);
        }
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
    (void)idChild;
    (void)dwmsEventTime;

    // idObject == OBJID_WINDOW means the window itself just showed, not one
    // of its child controls - EVENT_OBJECT_SHOW otherwise fires constantly
    // for every button/label inside it as the page renders.
    if (event == EVENT_OBJECT_SHOW && idObject == OBJID_WINDOW)
    {
        ULONGLONG deadline = gSuppressShowUntilTick;
        if (deadline && GetTickCount64() < deadline)
        {
            WCHAR szClassName[64] = {0};
            GetClassNameW(hwnd, szClassName, ARRAYSIZE(szClassName));
            if (CompareStringOrdinal(L"vguiPopupWindow", -1, szClassName, -1, FALSE) == CSTR_EQUAL)
            {
                // One-shot: only the first window auto-restore pops back up is
                // suppressed. Anything shown afterwards (including the user
                // manually reopening Steam a second later) is left alone.
                gSuppressShowUntilTick = 0;
                ShowWindow(hwnd, SW_HIDE);
            }
        }
        return;
    }

    if (event != EVENT_OBJECT_CREATE)
        return;

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

    // SYNCHRONIZE on top of SUSPEND_RESUME: the watcher waits on this handle
    // alongside its registry events so it notices the thread terminating, which
    // needs the handle to carry the right to be used in a wait function.
    HANDLE hSteamThread = OpenThread(THREAD_SUSPEND_RESUME | SYNCHRONIZE, FALSE, dwEventThread);
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
    // A previous attempt that got as far as registering the class but failed to
    // create its window leaves the class behind, so a retry lands on
    // ERROR_CLASS_ALREADY_EXISTS. That's the one failure worth continuing
    // through - the class is registered, which is all CreateWindowExW needs.
    BOOL ok = atom || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;

    if (ok && CreateWindowExW(WS_EX_LEFT | WS_EX_LTRREADING, wc.lpszClassName, NULL, WS_OVERLAPPED, 0, 0, 0, 0, NULL,
                              NULL, hModule, NULL))
    {
        MSG msg = {0};
        while (GetMessageW(&msg, NULL, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    else
        ok = FALSE;

    // Released whichever way we got here, so a later vguiPopupWindow event can
    // bring the tray back - the same way the watcher thread re-arms itself.
    // Without this, one early failure would latch the flag at 1 and leave the
    // Steam session with no tray icon and no way to ever get one back.
    InterlockedExchange(&gTrayThreadStarted, 0);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

// Installs the WinEvent hook and pumps messages for it. SetWinEventHook with
// WINEVENT_OUTOFCONTEXT requires the installing thread to run a message loop;
// callbacks are delivered through it.
static DWORD WINAPI HookThreadProc(LPVOID lpParameter)
{
    (void)lpParameter;

    // Range covers CREATE (existing tray/watcher bootstrap), DESTROY (ignored
    // - ends up in WinEventProc's default fallthrough), and SHOW (the auto-
    // restore suppression above). EVENT_OBJECT_HIDE is one past the end of
    // this range and is intentionally excluded - nothing here needs it.
    if (!SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW, NULL, WinEventProc, GetCurrentProcessId(), 0,
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
