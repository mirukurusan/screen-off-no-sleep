#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include "config.h"
#include "power_manager.h"
#include "input_listener.h"
#include "hotkey_dialog.h"
#include "../res/resource.h"

static const wchar_t* MAIN_WINDOW_CLASS = L"ScreenOffNoSleepMainWindow";
static const wchar_t* MUTEX_NAME = L"Local\\ScreenOffNoSleep_InstanceMutex";
static const wchar_t* TRIGGER_MESSAGE_NAME = L"ScreenOffNoSleep_Trigger";

static const UINT WM_TRAYICON = WM_USER + 200;

static HWND g_hWnd = nullptr;
static NOTIFYICONDATAW g_nid = {};
static UINT g_wmTaskbarCreated = 0;
static UINT g_wmTriggerMessage = 0;
static HANDLE g_hMutex = nullptr;

static void EnableHighDpiAwareness() {
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        typedef BOOL(WINAPI* PFN_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
        auto pfn = reinterpret_cast<PFN_SetProcessDpiAwarenessContext>(
            GetProcAddress(hUser32, "SetProcessDpiAwarenessContext"));
        if (pfn) {
            if (pfn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
                Config::Log(L"DPI awareness set to PerMonitorV2");
                return;
            }
            if (pfn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE)) {
                Config::Log(L"DPI awareness set to PerMonitor");
                return;
            }
        }
    }

    HMODULE hShcore = LoadLibraryW(L"shcore.dll");
    if (hShcore) {
        typedef HRESULT(WINAPI* PFN_SetProcessDpiAwareness)(int);
        auto pfn = reinterpret_cast<PFN_SetProcessDpiAwareness>(
            GetProcAddress(hShcore, "SetProcessDpiAwareness"));
        if (pfn) {
            pfn(2); // PROCESS_PER_MONITOR_DPI_AWARE
            Config::Log(L"DPI awareness set via shcore");
        }
        FreeLibrary(hShcore);
    }
}

static bool IsElevated() {
    BOOL elevated = FALSE;
    HANDLE hToken = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elevation = {};
        DWORD size = sizeof(elevation);
        if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &size)) {
            elevated = elevation.TokenIsElevated;
        }
        CloseHandle(hToken);
    }
    return elevated != FALSE;
}

static bool IsTaskbarLightTheme() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD val = 0;
        DWORD size = sizeof(val);
        DWORD type = REG_DWORD;
        if (RegQueryValueExW(hKey, L"SystemUsesLightTheme", nullptr, &type, reinterpret_cast<LPBYTE>(&val), &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return val == 1;
        }
        RegCloseKey(hKey);
    }
    return false;
}

static UINT GetTaskbarDpi(HWND hWnd) {
    UINT dpi = 0;
    HWND hTaskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (hUser) {
        typedef UINT(WINAPI* PFN_GetDpiForWindow)(HWND);
        auto pGetDpiForWindow = reinterpret_cast<PFN_GetDpiForWindow>(
            GetProcAddress(hUser, "GetDpiForWindow"));
        if (pGetDpiForWindow) {
            if (hTaskbar) {
                dpi = pGetDpiForWindow(hTaskbar);
            }
            if (!dpi && hWnd) {
                dpi = pGetDpiForWindow(hWnd);
            }
        }
        if (!dpi) {
            typedef UINT(WINAPI* PFN_GetDpiForSystem)();
            auto pGetDpiForSystem = reinterpret_cast<PFN_GetDpiForSystem>(
                GetProcAddress(hUser, "GetDpiForSystem"));
            if (pGetDpiForSystem) {
                dpi = pGetDpiForSystem();
            }
        }
    }
    if (!dpi) {
        HDC hdc = GetDC(nullptr);
        if (hdc) {
            dpi = static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSX));
            ReleaseDC(nullptr, hdc);
        }
    }
    return dpi ? dpi : 96;
}

static void GetTrayIconMetrics(HWND hWnd, int* cx, int* cy) {
    UINT dpi = GetTaskbarDpi(hWnd);
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (hUser) {
        typedef int(WINAPI* PFN_GetSystemMetricsForDpi)(int, UINT);
        auto pGetSystemMetricsForDpi = reinterpret_cast<PFN_GetSystemMetricsForDpi>(
            GetProcAddress(hUser, "GetSystemMetricsForDpi"));
        if (pGetSystemMetricsForDpi) {
            *cx = pGetSystemMetricsForDpi(SM_CXSMICON, dpi);
            *cy = pGetSystemMetricsForDpi(SM_CYSMICON, dpi);
        }
    }
    if (*cx <= 0 || *cy <= 0) {
        *cx = MulDiv(16, static_cast<int>(dpi), 96);
        *cy = MulDiv(16, static_cast<int>(dpi), 96);
    }
}

static HICON GetAppropriateTrayIcon(HWND hWnd) {
    HINSTANCE hInst = GetModuleHandle(nullptr);
    int iconId = IsTaskbarLightTheme() ? IDI_ICON_LIGHT_TASKBAR : IDI_ICON_DARK_TASKBAR;

    int cx = 0, cy = 0;
    GetTrayIconMetrics(hWnd, &cx, &cy);

    HICON hIcon = nullptr;
    HMODULE hComCtl = LoadLibraryW(L"comctl32.dll");
    if (hComCtl) {
        typedef HRESULT(WINAPI* PFN_LoadIconWithScaleDown)(HINSTANCE, PCWSTR, int, int, HICON*);
        auto pLoadIconWithScaleDown = reinterpret_cast<PFN_LoadIconWithScaleDown>(
            GetProcAddress(hComCtl, "LoadIconWithScaleDown"));
        if (pLoadIconWithScaleDown) {
            HRESULT hr = pLoadIconWithScaleDown(hInst, MAKEINTRESOURCEW(iconId), cx, cy, &hIcon);
            if (FAILED(hr)) {
                hIcon = nullptr;
            }
        }
        FreeLibrary(hComCtl);
    }

    if (!hIcon) {
        hIcon = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(iconId), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR));
    }
    if (!hIcon) {
        hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(iconId));
    }
    if (!hIcon) {
        hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    }
    return hIcon;
}

static void AddTrayIcon(HWND hWnd) {
    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = hWnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = GetAppropriateTrayIcon(hWnd);

    wcscpy_s(g_nid.szTip, L"Turn Off Display Without Sleep\nLeft-click: Turn off display\nRight-click: Menu");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    g_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
}

static void UpdateTrayIcon(HWND hWnd) {
    if (!g_nid.hWnd) {
        AddTrayIcon(hWnd);
        return;
    }

    HICON hNewIcon = GetAppropriateTrayIcon(hWnd);
    if (hNewIcon) {
        HICON hOldIcon = g_nid.hIcon;
        g_nid.hIcon = hNewIcon;
        g_nid.uFlags = NIF_ICON;
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
        if (hOldIcon && hOldIcon != hNewIcon) {
            DestroyIcon(hOldIcon);
        }
    }
}

static void RemoveTrayIcon() {
    if (g_nid.hWnd) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        if (g_nid.hIcon) {
            DestroyIcon(g_nid.hIcon);
        }
        memset(&g_nid, 0, sizeof(g_nid));
    }
}

static void ShowTrayContextMenu(HWND hWnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    // 1. Turn Off Display Now (Bold)
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_TRIGGER_NOW, L"Turn Off Display Now");
    SetMenuDefaultItem(hMenu, ID_TRAY_TRIGGER_NOW, FALSE);

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // 2. Settings toggles
    UINT lockFlags = MF_STRING | (PowerManager::GetLockOnScreenOff() ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(hMenu, lockFlags, ID_TRAY_LOCK_ON_OFF, L"Lock Workstation After Display Off");

    UINT lidFlags = MF_STRING | (InputListener::GetHandleLidClose() ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(hMenu, lidFlags, ID_TRAY_HANDLE_LID, L"Turn Off Display on Lid Close");

    UINT pwrFlags = MF_STRING | (InputListener::GetHandlePowerButton() ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(hMenu, pwrFlags, ID_TRAY_HANDLE_POWER_BTN, L"Turn Off Display on Power Button (HID)");

    UINT autoFlags = MF_STRING | (Config::IsAutoStartEnabled() ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(hMenu, autoFlags, ID_TRAY_AUTOSTART, L"Start with Windows");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // 3. Hotkey configuration
    std::wstring currentHotkey = InputListener::GetTriggerHotkey();
    std::wstring hotkeyMenuText = L"Change Hotkey... (Current: " + (currentHotkey.empty() ? L"Disabled" : currentHotkey) + L")";
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_CHANGE_HOTKEY, hotkeyMenuText.c_str());

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // 4. Exit
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");

    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_RIGHTALIGN, pt.x, pt.y, 0, hWnd, nullptr);
    PostMessageW(hWnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

static LRESULT CALLBACK MainWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == g_wmTaskbarCreated) {
        Config::Log(L"Explorer restarted; recreating tray icon");
        AddTrayIcon(hWnd);
        return 0;
    }

    if (uMsg == g_wmTriggerMessage) {
        Config::Log(L"External trigger message received");
        PowerManager::TriggerDisplayOff();
        return 0;
    }

    switch (uMsg) {
        case WM_CREATE:
            return 0;

        case WM_TRAYICON: {
            UINT event = LOWORD(lParam);
            if (event == WM_LBUTTONUP || event == WM_LBUTTONDBLCLK) {
                PowerManager::TriggerDisplayOff();
            } else if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) {
                ShowTrayContextMenu(hWnd);
            }
            return 0;
        }

        case WM_DPICHANGED: {
            Config::Log(L"WM_DPICHANGED; updating tray icon for new DPI");
            UpdateTrayIcon(hWnd);
            return 0;
        }

        case WM_SETTINGCHANGE: {
            if (lParam && wcscmp(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0) {
                Config::Log(L"Theme changed; updating tray icon for taskbar");
                UpdateTrayIcon(hWnd);
            }
            return 0;
        }

        case WM_THEMECHANGED: {
            Config::Log(L"Theme changed; updating tray icon");
            UpdateTrayIcon(hWnd);
            return 0;
        }

        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            switch (id) {
                case ID_TRAY_TRIGGER_NOW:
                    PowerManager::TriggerDisplayOff();
                    break;
                case ID_TRAY_LOCK_ON_OFF:
                    PowerManager::SetLockOnScreenOff(!PowerManager::GetLockOnScreenOff());
                    break;
                case ID_TRAY_HANDLE_LID:
                    InputListener::SetHandleLidClose(!InputListener::GetHandleLidClose());
                    break;
                case ID_TRAY_HANDLE_POWER_BTN:
                    InputListener::SetHandlePowerButton(!InputListener::GetHandlePowerButton());
                    break;
                case ID_TRAY_AUTOSTART:
                    Config::SetAutoStartEnabled(!Config::IsAutoStartEnabled());
                    break;
                case ID_TRAY_CHANGE_HOTKEY:
                    HotkeyDialog::Show(hWnd);
                    break;
                case ID_TRAY_EXIT:
                    DestroyWindow(hWnd);
                    break;
            }
            return 0;
        }

        case WM_HOTKEY:
            InputListener::HandleHotkey(wParam, lParam);
            return 0;

        case WM_INPUT:
            InputListener::HandleRawInput(reinterpret_cast<HRAWINPUT>(lParam));
            return DefWindowProcW(hWnd, uMsg, wParam, lParam);

        case WM_POWERBROADCAST:
            InputListener::HandlePowerBroadcast(wParam, lParam);
            return TRUE;

        case WM_TIMER:
            if (wParam == PowerManager::WATCHDOG_TIMER_ID) {
                PowerManager::OnWatchdogTimer();
            }
            return 0;

        case WM_DESTROY:
            RemoveTrayIcon();
            InputListener::Shutdown();
            PowerManager::Shutdown();
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, PWSTR /*lpCmdLine*/, int /*nCmdShow*/) {
    // 1. Enable High-DPI Awareness (PerMonitorV2 + GDI Scaling)
    EnableHighDpiAwareness();

    // 2. Single Instance Check
    g_wmTriggerMessage = RegisterWindowMessageW(TRIGGER_MESSAGE_NAME);

    g_hMutex = CreateMutexW(nullptr, TRUE, MUTEX_NAME);
    if (!g_hMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (g_wmTriggerMessage) {
            PostMessageW(HWND_BROADCAST, g_wmTriggerMessage, 0, 0);
        }
        if (g_hMutex) {
            CloseHandle(g_hMutex);
        }
        return 0;
    }

    // 3. Administrator Elevation Check
    if (!IsElevated()) {
        MessageBoxW(
            nullptr,
            L"Screen-Off-No-Sleep requires administrator privileges to manage power schemes and input sinks.\nPlease run as Administrator.",
            L"Administrator Privileges Required",
            MB_ICONERROR | MB_OK
        );
        CloseHandle(g_hMutex);
        return 1;
    }

    // 4. Initialize Common Controls
    INITCOMMONCONTROLSEX icex = {sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icex);

    // 5. Register TaskbarCreated message
    g_wmTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    // 6. Register Main Hidden Window Class
    WNDCLASSEXW wc = {sizeof(WNDCLASSEXW)};
    wc.lpfnWndProc = MainWindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = MAIN_WINDOW_CLASS;
    if (!RegisterClassExW(&wc)) {
        CloseHandle(g_hMutex);
        return 1;
    }

    // 7. Create Main Hidden Window (Top-level window to receive broadcasts)
    g_hWnd = CreateWindowExW(
        0,
        MAIN_WINDOW_CLASS,
        L"ScreenOffNoSleep",
        WS_OVERLAPPED,
        0, 0, 0, 0,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!g_hWnd) {
        CloseHandle(g_hMutex);
        return 1;
    }

    // 8. Load Settings from Registry
    bool lockOnScreenOff = false;
    bool handleLidClose = false;
    bool handlePowerButton = false;
    std::wstring triggerHotkey = L"Win+Alt+L";
    Config::LoadAppSettings(&lockOnScreenOff, &handleLidClose, &handlePowerButton, &triggerHotkey);

    // 9. Initialize Subsystems
    PowerManager::Initialize(g_hWnd);
    PowerManager::SetLockOnScreenOff(lockOnScreenOff);

    InputListener::Initialize(g_hWnd);
    InputListener::SetHandleLidClose(handleLidClose);
    InputListener::SetHandlePowerButton(handlePowerButton);
    InputListener::SetTriggerHotkey(triggerHotkey);

    // 10. Display Tray Icon
    AddTrayIcon(g_hWnd);

    Config::Log(L"ScreenOffNoSleep standalone application started successfully");

    // 11. Message Loop
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // 12. Cleanup
    UnregisterClassW(MAIN_WINDOW_CLASS, hInstance);
    if (g_hMutex) {
        ReleaseMutex(g_hMutex);
        CloseHandle(g_hMutex);
        g_hMutex = nullptr;
    }

    return static_cast<int>(msg.wParam);
}
