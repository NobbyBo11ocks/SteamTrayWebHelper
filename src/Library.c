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

static DWORD WINAPI ThreadProc(LPVOID lpParameter);
static HWND hTrayWnd;

static DWORD GetOverride(VOID)
{
    DWORD value = OVERRIDE_AUTO;
    RegGetValueW(HKEY_CURRENT_USER, OVERRIDE_SUBKEY, OVERRIDE_VALUE, RRF_RT_REG_DWORD, NULL, &value,
                 &((DWORD){sizeof(DWORD)}));
    return value;
}

static BOOL IsSteamAppRunning(VOID)
{
    BOOL running = FALSE;
    RegGetValueW(HKEY_CURRENT_USER, L"SOFTWARE\\Valve\\Steam", L"RunningAppID", RRF_RT_REG_DWORD, NULL, &running,
                 &((DWORD){sizeof(BOOL)}));
    return running;
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
    HINSTANCE hInst = GetModuleHandleW(NULL);
    int cx = GetSystemMetrics(SM_CXSMICON), cy = GetSystemMetrics(SM_CYSMICON);
    hIconOn = LoadImageW(hInst, MAKEINTRESOURCEW(IDR_ICON_ON), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
    hIconOff = LoadImageW(hInst, MAKEINTRESOURCEW(IDR_ICON_OFF), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR);
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static NOTIFYICONDATAW _ = {.cbSize = sizeof(NOTIFYICONDATAW),
                                .uCallbackMessage = WM_USER,
                                .uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP};

    static UINT $ = WM_NULL;

    switch (uMsg)
    {
    case WM_CREATE:
    {
        $ = RegisterWindowMessageW(L"TaskbarCreated");
        hTrayWnd = _.hWnd = hWnd;
        EnsureTrayIconsLoaded();

        BOOL disabled = ComputeDisabled();
        _.hIcon = disabled ? hIconOff : hIconOn;
        lstrcpyW(_.szTip, disabled ? L"Steam WebHelper - CEF Disabled" : L"Steam WebHelper - CEF Enabled");
        Shell_NotifyIconW(NIM_ADD, &_);
        break;
    }

    case WM_TRAYSTATE:
    {
        BOOL disabled = (BOOL)wParam;
        _.hIcon = disabled ? hIconOff : hIconOn;
        lstrcpyW(_.szTip, disabled ? L"Steam WebHelper - CEF Disabled" : L"Steam WebHelper - CEF Enabled");
        Shell_NotifyIconW(NIM_MODIFY, &_);
        break;
    }

    case WM_USER:
        if (lParam == WM_RBUTTONDOWN)
        {
            DWORD override = GetOverride();

            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING | (override == OVERRIDE_AUTO ? MF_CHECKED : 0), IDM_AUTO, L"Automatic");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING | (override == OVERRIDE_ON ? MF_CHECKED : 0), IDM_ON, L"On");
            AppendMenuW(hMenu, MF_STRING | (override == OVERRIDE_OFF ? MF_CHECKED : 0), IDM_OFF, L"Off");
            SetForegroundWindow(hWnd);

            POINT _ = {};
            GetCursorPos(&_);
            UINT cmd = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON | TPM_RETURNCMD, _.x, _.y, 0,
                                       hWnd, NULL);
            DestroyMenu(hMenu);

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

    default:
        if (uMsg == $)
            Shell_NotifyIconW(NIM_ADD, &_);
        break;
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

static VOID KillWebHelperChildren(VOID)
{
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE)
        return;

    PROCESSENTRY32W pe = {.dwSize = sizeof(PROCESSENTRY32W)};
    if (Process32FirstW(hSnap, &pe))
        do
        {
            if (pe.th32ParentProcessID == GetCurrentProcessId() &&
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

static VOID CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild,
                                  DWORD dwEventThread, DWORD dwmsEventTime)
{
    WCHAR szClassName[16] = {};
    GetClassNameW(hwnd, szClassName, 16);

    if (CompareStringOrdinal(L"vguiPopupWindow", -1, szClassName, -1, FALSE) != CSTR_EQUAL ||
        GetWindowTextLengthW(hwnd) < 1)
        return;

    CloseHandle(CreateThread(NULL, 0, ThreadProc, (LPVOID)FALSE, 0, NULL));

    HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, dwEventThread);

    HKEY hSteamKey = NULL, hOverrideKey = NULL;
    // Bail out rather than arming a wait on a NULL key, which would fail
    // instantly and spin this thread at 100% CPU.
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\Valve\\Steam", REG_OPTION_NON_VOLATILE,
                       KEY_NOTIFY | KEY_QUERY_VALUE, &hSteamKey) != ERROR_SUCCESS ||
        RegCreateKeyExW(HKEY_CURRENT_USER, OVERRIDE_SUBKEY, 0, NULL, REG_OPTION_NON_VOLATILE,
                         KEY_NOTIFY | KEY_QUERY_VALUE, NULL, &hOverrideKey, NULL) != ERROR_SUCCESS)
    {
        if (hSteamKey)
            RegCloseKey(hSteamKey);
        if (hOverrideKey)
            RegCloseKey(hOverrideKey);
        CloseHandle(hThread);
        return;
    }

    // Two keys need watching now (Steam's real state and our private
    // override), so this waits on both asynchronously instead of blocking
    // synchronously on one - still zero CPU while idle, no polling.
    HANDLE hEvents[2] = {CreateEventW(NULL, FALSE, FALSE, NULL), CreateEventW(NULL, FALSE, FALSE, NULL)};
    RegNotifyChangeKeyValue(hSteamKey, FALSE, REG_NOTIFY_CHANGE_LAST_SET, hEvents[0], TRUE);
    RegNotifyChangeKeyValue(hOverrideKey, FALSE, REG_NOTIFY_CHANGE_LAST_SET, hEvents[1], TRUE);

    for (;;)
    {
        DWORD wait = WaitForMultipleObjects(2, hEvents, FALSE, INFINITE);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_OBJECT_0 + 1)
            break;

        if (wait == WAIT_OBJECT_0)
            RegNotifyChangeKeyValue(hSteamKey, FALSE, REG_NOTIFY_CHANGE_LAST_SET, hEvents[0], TRUE);
        else
            RegNotifyChangeKeyValue(hOverrideKey, FALSE, REG_NOTIFY_CHANGE_LAST_SET, hEvents[1], TRUE);

        BOOL disabled = ComputeDisabled();
        (disabled ? SuspendThread : ResumeThread)(hThread);
        if (hTrayWnd)
            PostMessageW(hTrayWnd, WM_TRAYSTATE, disabled, 0);

        if (disabled)
            KillWebHelperChildren();
    }

    CloseHandle(hEvents[0]);
    CloseHandle(hEvents[1]);
    RegCloseKey(hSteamKey);
    RegCloseKey(hOverrideKey);
    CloseHandle(hThread);
}

static DWORD WINAPI ThreadProc(LPVOID lpParameter)
{
    if (lpParameter)
        SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE, NULL, WinEventProc, GetCurrentProcessId(), (DWORD){},
                        WINEVENT_OUTOFCONTEXT);
    else
        CreateWindowExW(
            WS_EX_LEFT | WS_EX_LTRREADING,
            (LPCWSTR)(ULONG_PTR)RegisterClassW(&((WNDCLASSW){
                .lpszClassName = L" ", .hInstance = GetModuleHandleW(NULL), .lpfnWndProc = (WNDPROC)WndProc})),
            NULL, WS_OVERLAPPED, 0, 0, 0, 0, NULL, NULL, GetModuleHandleW(NULL), NULL);

    MSG _ = {};
    while (GetMessageW(&_, NULL, (UINT){}, (UINT){}))
    {
        TranslateMessage(&_);
        DispatchMessageW(&_);
    }
    return EXIT_SUCCESS;
}

BOOL WINAPI DllMainCRTStartup(HINSTANCE hLibModule, DWORD dwReason, LPVOID lpReserved)
{
    if (dwReason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hLibModule);
        CloseHandle(CreateThread(NULL, 0, ThreadProc, (LPVOID)TRUE, 0, NULL));
    }
    return TRUE;
}
