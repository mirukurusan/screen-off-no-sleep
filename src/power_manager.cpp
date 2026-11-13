#include "power_manager.h"
#include <cstring>

namespace PowerManager {

// GUIDs
static const GUID GUID_VIDEO_SUBGROUP_LOCAL = {
    0x7516b95f, 0xf776, 0x4464,
    {0x8c, 0x53, 0x06, 0x16, 0x7f, 0x40, 0xcc, 0x99}
};

static const GUID GUID_VIDEO_IDLE_TIMEOUT_LOCAL = {
    0x3c0bc021, 0xc8a8, 0x4e07,
    {0xa9, 0x73, 0x6b, 0x14, 0xcb, 0xcb, 0x2b, 0x7e}
};

static const wchar_t* BLACKOUT_WINDOW_CLASS = L"ScreenOffNoSleepBlackout";

static HWND g_hTargetWnd = nullptr;
static HWND g_hBlackoutWnd = nullptr;
static CRITICAL_SECTION g_powerLock;
static bool g_powerLockInitialized = false;

static std::atomic<bool> g_flowActive{false};
static std::atomic<bool> g_shuttingDown{false};
static std::atomic<bool> g_lockOnScreenOff{false};

static SavedPowerState g_flowSavedState = {};
static bool g_displayOn = true;
static ULONGLONG g_lastDisplayOnTick = 0;

static bool GuidEqual(const GUID& a, const GUID& b) {
    return memcmp(&a, &b, sizeof(GUID)) == 0;
}

static LRESULT CALLBACK BlackoutWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_ERASEBKGND: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(hdc, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            return 1;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            FillRect(hdc, &ps.rcPaint, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_SETCURSOR:
            SetCursor(NULL);
            return TRUE;
        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

void ShowBlackoutOverlay() {
    if (g_hBlackoutWnd && IsWindow(g_hBlackoutWnd)) {
        return;
    }

    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vcx = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vcy = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    g_hBlackoutWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        BLACKOUT_WINDOW_CLASS,
        L"",
        WS_POPUP | WS_VISIBLE,
        vx, vy, vcx, vcy,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr
    );

    if (g_hBlackoutWnd) {
        SetLayeredWindowAttributes(g_hBlackoutWnd, 0, 255, LWA_ALPHA);
        SetWindowPos(g_hBlackoutWnd, HWND_TOPMOST, vx, vy, vcx, vcy, SWP_SHOWWINDOW | SWP_NOACTIVATE);
        UpdateWindow(g_hBlackoutWnd);
        Config::Log(L"Blackout overlay displayed");
    }
}

void DestroyBlackoutOverlay() {
    if (g_hBlackoutWnd) {
        if (IsWindow(g_hBlackoutWnd)) {
            DestroyWindow(g_hBlackoutWnd);
        }
        g_hBlackoutWnd = nullptr;
        Config::Log(L"Blackout overlay destroyed");
    }
}

static bool QueryVideoIdleTimeouts(const GUID& scheme, DWORD* acSeconds, DWORD* dcSeconds) {
    DWORD ac = 0, dc = 0;
    DWORD errAc = PowerReadACValueIndex(nullptr, &scheme, &GUID_VIDEO_SUBGROUP_LOCAL, &GUID_VIDEO_IDLE_TIMEOUT_LOCAL, &ac);
    DWORD errDc = PowerReadDCValueIndex(nullptr, &scheme, &GUID_VIDEO_SUBGROUP_LOCAL, &GUID_VIDEO_IDLE_TIMEOUT_LOCAL, &dc);

    if (errAc != ERROR_SUCCESS && errDc != ERROR_SUCCESS) {
        Config::Log(L"Failed to read current VIDEOIDLE (AC err=%lu, DC err=%lu)", errAc, errDc);
        return false;
    }

    *acSeconds = (errAc == ERROR_SUCCESS) ? ac : 0;
    *dcSeconds = (errDc == ERROR_SUCCESS) ? dc : 0;
    return true;
}

static bool WriteVideoIdleTimeouts(const GUID& scheme, DWORD acSeconds, DWORD dcSeconds, bool* acOk, bool* dcOk) {
    DWORD errAc = PowerWriteACValueIndex(nullptr, &scheme, &GUID_VIDEO_SUBGROUP_LOCAL, &GUID_VIDEO_IDLE_TIMEOUT_LOCAL, acSeconds);
    DWORD errDc = PowerWriteDCValueIndex(nullptr, &scheme, &GUID_VIDEO_SUBGROUP_LOCAL, &GUID_VIDEO_IDLE_TIMEOUT_LOCAL, dcSeconds);

    if (acOk) *acOk = (errAc == ERROR_SUCCESS);
    if (dcOk) *dcOk = (errDc == ERROR_SUCCESS);

    DWORD errAct = PowerSetActiveScheme(nullptr, &scheme);
    if (errAct != ERROR_SUCCESS) {
        Config::Log(L"PowerSetActiveScheme failed: %lu", errAct);
        return false;
    }
    return (errAc == ERROR_SUCCESS || errDc == ERROR_SUCCESS);
}

void RecoverSavedPowerState() {
    EnterCriticalSection(&g_powerLock);
    SavedPowerState state = {};
    if (Config::LoadSavedPowerState(&state)) {
        Config::Log(L"Found leftover power state from unclean shutdown; restoring AC=%lu s, DC=%lu s", state.acTimeout, state.dcTimeout);
        bool acOk = false, dcOk = false;
        if (WriteVideoIdleTimeouts(state.scheme, state.acTimeout, state.dcTimeout, &acOk, &dcOk)) {
            Config::DeleteSavedPowerState();
            Config::Log(L"Successfully recovered leftover power state");
        } else {
            Config::Log(L"Failed to restore leftover power state");
        }
    }
    LeaveCriticalSection(&g_powerLock);
}

static bool InjectOneSecondTimeout() {
    EnterCriticalSection(&g_powerLock);

    if (g_shuttingDown.load()) {
        LeaveCriticalSection(&g_powerLock);
        return false;
    }

    if (g_flowActive.load()) {
        LeaveCriticalSection(&g_powerLock);
        return true;
    }

    GUID* pActiveScheme = nullptr;
    DWORD err = PowerGetActiveScheme(nullptr, &pActiveScheme);
    if (err != ERROR_SUCCESS || !pActiveScheme) {
        Config::Log(L"PowerGetActiveScheme failed: %lu", err);
        LeaveCriticalSection(&g_powerLock);
        return false;
    }
    GUID activeScheme = *pActiveScheme;
    LocalFree(pActiveScheme);

    SavedPowerState state = {};
    bool hasExistingRecord = Config::LoadSavedPowerState(&state);

    if (hasExistingRecord && !GuidEqual(state.scheme, activeScheme)) {
        bool acOk = false, dcOk = false;
        WriteVideoIdleTimeouts(state.scheme, state.acTimeout, state.dcTimeout, &acOk, &dcOk);
        Config::DeleteSavedPowerState();
        hasExistingRecord = false;
    }

    if (!hasExistingRecord) {
        DWORD currentAc = 0, currentDc = 0;
        if (!QueryVideoIdleTimeouts(activeScheme, &currentAc, &currentDc)) {
            LeaveCriticalSection(&g_powerLock);
            return false;
        }

        state.scheme = activeScheme;
        state.acTimeout = currentAc;
        state.dcTimeout = currentDc;

        if (!Config::SaveSavedPowerState(state)) {
            Config::Log(L"Failed to record power state to registry; aborting injection");
            LeaveCriticalSection(&g_powerLock);
            return false;
        }
    }

    g_flowSavedState = state;

    bool acOk = false, dcOk = false;
    if (!WriteVideoIdleTimeouts(activeScheme, 1, 1, &acOk, &dcOk)) {
        Config::Log(L"Failed to write the 1-second display-off timeout");
        Config::DeleteSavedPowerState();
        LeaveCriticalSection(&g_powerLock);
        return false;
    }

    g_flowActive.store(true);
    LeaveCriticalSection(&g_powerLock);

    Config::Log(L"Injected VIDEOIDLE=1 s (saved AC=%lu s, DC=%lu s)", state.acTimeout, state.dcTimeout);
    return true;
}

static void RestoreSavedTimeouts() {
    EnterCriticalSection(&g_powerLock);

    if (!g_flowActive.load()) {
        LeaveCriticalSection(&g_powerLock);
        return;
    }

    SavedPowerState state = g_flowSavedState;
    g_flowActive.store(false);

    bool acOk = false, dcOk = false;
    bool success = WriteVideoIdleTimeouts(state.scheme, state.acTimeout, state.dcTimeout, &acOk, &dcOk);

    if (success) {
        Config::DeleteSavedPowerState();
        Config::Log(L"Restored VIDEOIDLE: AC=%lu s, DC=%lu s", state.acTimeout, state.dcTimeout);
    } else {
        Config::Log(L"Failed to restore VIDEOIDLE; keeping saved record for startup recovery");
    }

    LeaveCriticalSection(&g_powerLock);
}

void TriggerDisplayOff() {
    Config::Log(L"TriggerDisplayOff initiated");
    ShowBlackoutOverlay();

    if (!InjectOneSecondTimeout()) {
        Config::Log(L"Failed to inject 1-second timeout; removing overlay");
        DestroyBlackoutOverlay();
        return;
    }

    if (g_hTargetWnd) {
        SetTimer(g_hTargetWnd, WATCHDOG_TIMER_ID, WATCHDOG_TIMEOUT_MS, nullptr);
    }
}

void OnDisplayStateChange(DWORD state) {
    if (state == 0) {
        // Display turned off
        g_displayOn = false;
        Config::Log(L"Display powered off (GUID_CONSOLE_DISPLAY_STATE == 0)");

        if (g_hTargetWnd) {
            KillTimer(g_hTargetWnd, WATCHDOG_TIMER_ID);
        }

        if (g_flowActive.load()) {
            RestoreSavedTimeouts();
            DestroyBlackoutOverlay();

            if (g_lockOnScreenOff.load()) {
                Config::Log(L"Locking workstation in the dark");
                LockWorkStation();
            }
        }
    } else {
        // Display turned on (1) or dimmed (2)
        g_displayOn = true;
        g_lastDisplayOnTick = GetTickCount64();
        Config::Log(L"Display powered on/dimmed (state=%lu)", state);

        if (g_flowActive.load()) {
            Config::Log(L"Display turned back on while flow active; cleaning up");
            if (g_hTargetWnd) {
                KillTimer(g_hTargetWnd, WATCHDOG_TIMER_ID);
            }
            RestoreSavedTimeouts();
            DestroyBlackoutOverlay();
        }
    }
}

void OnWatchdogTimer() {
    Config::Log(L"Watchdog timer expired; restoring original timeouts");
    if (g_hTargetWnd) {
        KillTimer(g_hTargetWnd, WATCHDOG_TIMER_ID);
    }
    RestoreSavedTimeouts();
    DestroyBlackoutOverlay();
}

void OnResumeFromSleep() {
    Config::Log(L"System resumed from sleep");
    if (g_flowActive.load()) {
        Config::Log(L"Flow active across sleep/resume; cleaning up");
        RestoreSavedTimeouts();
        DestroyBlackoutOverlay();
    }
}

bool IsDisplayOn() {
    return g_displayOn;
}

ULONGLONG GetLastDisplayOnTick() {
    return g_lastDisplayOnTick;
}

bool IsFlowActive() {
    return g_flowActive.load();
}

void SetLockOnScreenOff(bool enable) {
    g_lockOnScreenOff.store(enable);
    Config::SaveSettingDword(Config::VAL_LOCK_ON_SCREEN_OFF, enable ? 1 : 0);
}

bool GetLockOnScreenOff() {
    return g_lockOnScreenOff.load();
}

bool Initialize(HWND hWnd) {
    g_hTargetWnd = hWnd;
    InitializeCriticalSection(&g_powerLock);
    g_powerLockInitialized = true;

    WNDCLASSEXW wc = {sizeof(WNDCLASSEXW)};
    wc.lpfnWndProc = BlackoutWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = BLACKOUT_WINDOW_CLASS;
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.hCursor = NULL;
    RegisterClassExW(&wc);

    g_lastDisplayOnTick = GetTickCount64();
    g_displayOn = true;

    RecoverSavedPowerState();
    return true;
}

void Shutdown() {
    g_shuttingDown.store(true);

    if (g_powerLockInitialized) {
        EnterCriticalSection(&g_powerLock);
        if (g_flowActive.load()) {
            Config::Log(L"Shutdown during active flow; restoring timeouts");
            SavedPowerState state = g_flowSavedState;
            g_flowActive.store(false);
            bool acOk = false, dcOk = false;
            WriteVideoIdleTimeouts(state.scheme, state.acTimeout, state.dcTimeout, &acOk, &dcOk);
            Config::DeleteSavedPowerState();
        }
        LeaveCriticalSection(&g_powerLock);
    }

    DestroyBlackoutOverlay();
    UnregisterClassW(BLACKOUT_WINDOW_CLASS, GetModuleHandle(nullptr));

    if (g_powerLockInitialized) {
        DeleteCriticalSection(&g_powerLock);
        g_powerLockInitialized = false;
    }
}

} // namespace PowerManager
