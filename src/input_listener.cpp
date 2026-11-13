#include "input_listener.h"
#include "power_manager.h"
#include "config.h"
#include <hidsdi.h>
#include <hidpi.h>
#include <vector>
#include <atomic>

namespace InputListener {

static const GUID GUID_CONSOLE_DISPLAY_STATE_LOCAL = {
    0x6fe69556, 0x704a, 0x47a0,
    {0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47}
};

static const GUID GUID_LIDSWITCH_STATE_CHANGE_LOCAL = {
    0xba3e0f4d, 0xb817, 0x4094,
    {0xa2, 0xd1, 0xd5, 0x63, 0x79, 0xe6, 0xa0, 0xf3}
};

static HWND g_hWnd = nullptr;
static HPOWERNOTIFY g_displayNotify = nullptr;
static HPOWERNOTIFY g_lidNotify = nullptr;

static std::atomic<bool> g_handleLidClose{false};
static std::atomic<bool> g_handlePowerButton{false};
static std::wstring g_currentHotkey = L"Win+Alt+L";
static bool g_hotkeyRegistered = false;

static bool g_lidClosed = false;
static bool g_lidStateInitialized = false;

static wchar_t LowerAscii(wchar_t c) {
    return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c + 32) : c;
}

static bool IsAsciiAlphaNumeric(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9');
}

static std::wstring TrimHotkeyPart(const std::wstring& text) {
    size_t first = text.find_first_not_of(L" \t");
    if (first == std::wstring::npos) {
        return L"";
    }
    size_t last = text.find_last_not_of(L" \t");
    return text.substr(first, last - first + 1);
}

static bool ParseHotkeyKey(const std::wstring& part, UINT* vk) {
    if (part.empty()) return false;

    if (part.find_first_not_of(L"0123456789") == std::wstring::npos) {
        wchar_t* end = nullptr;
        unsigned long value = wcstoul(part.c_str(), &end, 10);
        if (end && *end == L'\0' && value > 0 && value <= 0xFF) {
            *vk = static_cast<UINT>(value);
            return true;
        }
        return false;
    }

    if (part.size() > 2 && part[0] == L'0' && part[1] == L'x') {
        wchar_t* end = nullptr;
        unsigned long value = wcstoul(part.c_str(), &end, 16);
        if (end && *end == L'\0' && value > 0 && value <= 0xFF) {
            *vk = static_cast<UINT>(value);
            return true;
        }
        return false;
    }

    if (part.size() == 1 && IsAsciiAlphaNumeric(part[0])) {
        *vk = static_cast<UINT>(part[0] >= L'a' ? part[0] - 32 : part[0]);
        return true;
    }

    if (part[0] == L'f' && part.size() <= 3 &&
        part.find_first_not_of(L"0123456789", 1) == std::wstring::npos) {
        wchar_t* end = nullptr;
        unsigned long number = wcstoul(part.c_str() + 1, &end, 10);
        if (end && *end == L'\0' && number >= 1 && number <= 24) {
            *vk = VK_F1 + static_cast<UINT>(number) - 1;
            return true;
        }
        return false;
    }

    struct NamedKey {
        const wchar_t* name;
        UINT vk;
    };
    static const NamedKey kNamedKeys[] = {
        {L"space", VK_SPACE},          {L"tab", VK_TAB},
        {L"enter", VK_RETURN},         {L"return", VK_RETURN},
        {L"esc", VK_ESCAPE},           {L"escape", VK_ESCAPE},
        {L"backspace", VK_BACK},       {L"insert", VK_INSERT},
        {L"ins", VK_INSERT},           {L"delete", VK_DELETE},
        {L"del", VK_DELETE},           {L"home", VK_HOME},
        {L"end", VK_END},              {L"pageup", VK_PRIOR},
        {L"pgup", VK_PRIOR},           {L"pagedown", VK_NEXT},
        {L"pgdn", VK_NEXT},            {L"up", VK_UP},
        {L"down", VK_DOWN},            {L"left", VK_LEFT},
        {L"right", VK_RIGHT},          {L"pause", VK_PAUSE},
        {L"printscreen", VK_SNAPSHOT}, {L"prtsc", VK_SNAPSHOT},
        {L"backquote", VK_OEM_3},      {L"`", VK_OEM_3},
        {L"plus", VK_OEM_PLUS},
        {L"minus", VK_OEM_MINUS},      {L"-", VK_OEM_MINUS},
        {L"comma", VK_OEM_COMMA},      {L",", VK_OEM_COMMA},
        {L"period", VK_OEM_PERIOD},    {L".", VK_OEM_PERIOD},
        {L"slash", VK_OEM_2},          {L"/", VK_OEM_2},
        {L"semicolon", VK_OEM_1},      {L";", VK_OEM_1},
        {L"quote", VK_OEM_7},          {L"'", VK_OEM_7},
        {L"leftbracket", VK_OEM_4},    {L"[", VK_OEM_4},
        {L"rightbracket", VK_OEM_6},   {L"]", VK_OEM_6},
        {L"backslash", VK_OEM_5},      {L"\\", VK_OEM_5},
    };
    for (const auto& key : kNamedKeys) {
        if (part == key.name) {
            *vk = key.vk;
            return true;
        }
    }

    return false;
}

bool ParseHotkey(const std::wstring& text, UINT* mods, UINT* vk) {
    *mods = 0;
    *vk = 0;

    std::wstring lowered;
    lowered.reserve(text.size());
    for (wchar_t c : text) {
        lowered.push_back(LowerAscii(c));
    }

    bool gotKey = false;
    size_t start = 0;
    while (start <= lowered.size()) {
        size_t plus = lowered.find(L'+', start);
        std::wstring part = TrimHotkeyPart(lowered.substr(
            start, plus == std::wstring::npos ? std::wstring::npos : plus - start));
        bool last = (plus == std::wstring::npos);

        if (!part.empty()) {
            if (part == L"ctrl" || part == L"control") {
                *mods |= MOD_CONTROL;
            } else if (part == L"alt") {
                *mods |= MOD_ALT;
            } else if (part == L"shift") {
                *mods |= MOD_SHIFT;
            } else if (part == L"win" || part == L"windows") {
                *mods |= MOD_WIN;
            } else if (last && !gotKey) {
                gotKey = ParseHotkeyKey(part, vk);
                if (!gotKey) return false;
            } else {
                return false;
            }
        }

        if (last) break;
        start = plus + 1;
    }

    return gotKey && *vk != 0;
}

void UnregisterTriggerHotkey() {
    if (g_hWnd && g_hotkeyRegistered) {
        UnregisterHotKey(g_hWnd, TRIGGER_HOTKEY_ID);
        g_hotkeyRegistered = false;
    }
}

bool SetTriggerHotkey(const std::wstring& hotkeyStr) {
    UnregisterTriggerHotkey();
    g_currentHotkey = hotkeyStr;
    Config::SaveSettingString(Config::VAL_TRIGGER_HOTKEY, hotkeyStr);

    if (hotkeyStr.empty()) {
        Config::Log(L"Hotkey disabled");
        return true;
    }

    UINT mods = 0;
    UINT vk = 0;
    if (!ParseHotkey(hotkeyStr, &mods, &vk)) {
        Config::Log(L"Failed to parse hotkey '%ls'", hotkeyStr.c_str());
        return false;
    }

    if (!RegisterHotKey(g_hWnd, TRIGGER_HOTKEY_ID, mods | MOD_NOREPEAT, vk)) {
        DWORD err = GetLastError();
        Config::Log(L"RegisterHotKey failed for '%ls': %lu", hotkeyStr.c_str(), err);
        return false;
    }

    g_hotkeyRegistered = true;
    Config::Log(L"Trigger hotkey registered: %ls (mods=0x%X, vk=0x%02X)", hotkeyStr.c_str(), mods, vk);
    return true;
}

std::wstring GetTriggerHotkey() {
    return g_currentHotkey;
}

void SetHandleLidClose(bool enable) {
    g_handleLidClose.store(enable);
    Config::SaveSettingDword(Config::VAL_HANDLE_LID_CLOSE, enable ? 1 : 0);
}

bool GetHandleLidClose() {
    return g_handleLidClose.load();
}

void SetHandlePowerButton(bool enable) {
    g_handlePowerButton.store(enable);
    Config::SaveSettingDword(Config::VAL_HANDLE_POWER_BTN, enable ? 1 : 0);
}

bool GetHandlePowerButton() {
    return g_handlePowerButton.load();
}

static void RegisterPowerButtonRawInput() {
    struct CollectionDesc {
        USHORT page;
        USHORT usage;
    };
    static const CollectionDesc kCollections[] = {
        {0x01, 0x80}, // System Control
        {0x01, 0x0D}, // Portable Device Control (Surface Power Button)
        {0x01, 0x81}, // System Power Down
        {0x01, 0x82}, // System Sleep
        {0x0C, 0x01}, // Consumer Control
    };

    for (const auto& item : kCollections) {
        RAWINPUTDEVICE rid = {};
        rid.usUsagePage = item.page;
        rid.usUsage = item.usage;
        rid.dwFlags = RIDEV_INPUTSINK;
        rid.hwndTarget = g_hWnd;
        RegisterRawInputDevices(&rid, 1, sizeof(rid));
    }
}

static void UnregisterPowerButtonRawInput() {
    static const USHORT kPages[] = {0x01, 0x01, 0x01, 0x01, 0x0C};
    static const USHORT kUsages[] = {0x80, 0x0D, 0x81, 0x82, 0x01};

    for (size_t i = 0; i < ARRAYSIZE(kPages); i++) {
        RAWINPUTDEVICE rid = {};
        rid.usUsagePage = kPages[i];
        rid.usUsage = kUsages[i];
        rid.dwFlags = RIDEV_REMOVE;
        rid.hwndTarget = nullptr;
        RegisterRawInputDevices(&rid, 1, sizeof(rid));
    }
}

static bool ParseHidPowerReport(const RAWINPUT* raw) {
    if (!raw || raw->header.dwType != RIM_TYPEHID) return false;

    UINT prepDataSize = 0;
    if (GetRawInputDeviceInfoW(raw->header.hDevice, RIDI_PREPARSEDDATA, nullptr, &prepDataSize) != 0 || prepDataSize == 0) {
        return false;
    }

    std::vector<BYTE> prepData(prepDataSize);
    if (GetRawInputDeviceInfoW(raw->header.hDevice, RIDI_PREPARSEDDATA, prepData.data(), &prepDataSize) == static_cast<UINT>(-1)) {
        return false;
    }

    auto pPreparsed = reinterpret_cast<PHIDP_PREPARSED_DATA>(prepData.data());
    HIDP_CAPS caps = {};
    if (HidP_GetCaps(pPreparsed, &caps) != HIDP_STATUS_SUCCESS) {
        return false;
    }

    PCHAR reportData = const_cast<PCHAR>(reinterpret_cast<const char*>(raw->data.hid.bRawData));

    USAGE usagesGeneric[32];
    ULONG countGeneric = ARRAYSIZE(usagesGeneric);
    if (HidP_GetUsages(HidP_Input, 0x01, 0, usagesGeneric, &countGeneric, pPreparsed,
                       reportData, raw->data.hid.dwSizeHid) != HIDP_STATUS_SUCCESS) {
        countGeneric = 0;
    }

    USAGE usagesConsumer[32];
    ULONG countConsumer = ARRAYSIZE(usagesConsumer);
    if (HidP_GetUsages(HidP_Input, 0x0C, 0, usagesConsumer, &countConsumer, pPreparsed,
                       reportData, raw->data.hid.dwSizeHid) != HIDP_STATUS_SUCCESS) {
        countConsumer = 0;
    }

    USAGE usagesButton[32];
    ULONG countButton = ARRAYSIZE(usagesButton);
    if (HidP_GetUsages(HidP_Input, 0x09, 0, usagesButton, &countButton, pPreparsed,
                       reportData, raw->data.hid.dwSizeHid) != HIDP_STATUS_SUCCESS) {
        countButton = 0;
    }

    // 1. Generic Desktop Power Down (0x81) or Sleep (0x82)
    for (ULONG i = 0; i < countGeneric; i++) {
        if (usagesGeneric[i] == 0x81 || usagesGeneric[i] == 0x82) return true;
    }

    // 2. Consumer Power (0x30) or Sleep (0x32)
    for (ULONG i = 0; i < countConsumer; i++) {
        if (usagesConsumer[i] == 0x30 || usagesConsumer[i] == 0x32) return true;
    }

    // 3. Portable Device Control (UsagePage 0x01, Usage 0x0D): Surface Power Button is Button 1
    if (caps.UsagePage == 0x01 && caps.Usage == 0x0D) {
        for (ULONG i = 0; i < countButton; i++) {
            if (usagesButton[i] == 1) return true;
        }
    }

    // 4. Direct TLC match if collection itself is Power Down / Sleep and payload is non-zero
    DWORD byteCount = raw->data.hid.dwSizeHid * raw->data.hid.dwCount;
    if (caps.UsagePage == 0x01 && (caps.Usage == 0x81 || caps.Usage == 0x82)) {
        if (byteCount > 0 && raw->data.hid.bRawData[0] != 0) return true;
    }

    return false;
}

void HandleRawInput(HRAWINPUT hRawInput) {
    if (!g_handlePowerButton.load()) return;

    UINT size = 0;
    GetRawInputData(hRawInput, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
    if (size == 0) return;

    std::vector<BYTE> buffer(size);
    if (GetRawInputData(hRawInput, RID_INPUT, buffer.data(), &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1)) {
        return;
    }

    const auto* raw = reinterpret_cast<const RAWINPUT*>(buffer.data());
    if (raw->header.dwType == RIM_TYPEHID && ParseHidPowerReport(raw)) {
        Config::Log(L"Power button HID report detected");

        if (!PowerManager::IsDisplayOn()) {
            Config::Log(L"Power button while display off: wake gesture, ignored");
            return;
        }

        ULONGLONG now = GetTickCount64();
        if (PowerManager::GetLastDisplayOnTick() != 0 &&
            (now - PowerManager::GetLastDisplayOnTick()) < PowerManager::WAKE_GUARD_MS) {
            Config::Log(L"Power button within display-on guard window: wake gesture, ignored");
            return;
        }

        PowerManager::TriggerDisplayOff();
    }
}

void HandlePowerBroadcast(WPARAM wParam, LPARAM lParam) {
    if (wParam == PBT_APMRESUMESUSPEND || wParam == PBT_APMRESUMEAUTOMATIC) {
        PowerManager::OnResumeFromSleep();
        return;
    }

    if (wParam == PBT_POWERSETTINGCHANGE && lParam != 0) {
        const auto* pbs = reinterpret_cast<const POWERBROADCAST_SETTING*>(lParam);

        if (memcmp(&pbs->PowerSetting, &GUID_CONSOLE_DISPLAY_STATE_LOCAL, sizeof(GUID)) == 0 &&
            pbs->DataLength >= sizeof(DWORD)) {
            DWORD state = *reinterpret_cast<const DWORD*>(pbs->Data);
            PowerManager::OnDisplayStateChange(state);
            return;
        }

        if (memcmp(&pbs->PowerSetting, &GUID_LIDSWITCH_STATE_CHANGE_LOCAL, sizeof(GUID)) == 0 &&
            pbs->DataLength >= sizeof(DWORD)) {
            DWORD lidState = *reinterpret_cast<const DWORD*>(pbs->Data);
            bool isClosed = (lidState == 0);
            Config::Log(L"Lid switch state: %lu (%ls)", lidState, isClosed ? L"closed" : L"open");

            if (!g_lidStateInitialized) {
                g_lidStateInitialized = true;
                g_lidClosed = isClosed;
                return;
            }

            bool wasClosed = g_lidClosed;
            g_lidClosed = isClosed;

            if (isClosed && !wasClosed) {
                if (g_handleLidClose.load()) {
                    Config::Log(L"Lid closed with HandleLidClose enabled; triggering display off");
                    if (PowerManager::IsDisplayOn()) {
                        PowerManager::TriggerDisplayOff();
                    }
                }
            }
            return;
        }
    }
}

void HandleHotkey(WPARAM wParam, LPARAM /*lParam*/) {
    if (wParam == static_cast<WPARAM>(TRIGGER_HOTKEY_ID)) {
        if (!PowerManager::IsDisplayOn()) {
            Config::Log(L"Hotkey pressed while display off; ignored");
            return;
        }

        ULONGLONG now = GetTickCount64();
        if (PowerManager::GetLastDisplayOnTick() != 0 &&
            (now - PowerManager::GetLastDisplayOnTick()) < PowerManager::WAKE_GUARD_MS) {
            Config::Log(L"Hotkey pressed within display-on guard window; ignored");
            return;
        }

        Config::Log(L"Trigger hotkey pressed; initiating display off");
        PowerManager::TriggerDisplayOff();
    }
}

bool Initialize(HWND hWnd) {
    g_hWnd = hWnd;

    g_displayNotify = RegisterPowerSettingNotification(
        g_hWnd, &GUID_CONSOLE_DISPLAY_STATE_LOCAL, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (!g_displayNotify) {
        Config::Log(L"RegisterPowerSettingNotification (Display) failed: %lu", GetLastError());
    }

    g_lidNotify = RegisterPowerSettingNotification(
        g_hWnd, &GUID_LIDSWITCH_STATE_CHANGE_LOCAL, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (!g_lidNotify) {
        Config::Log(L"RegisterPowerSettingNotification (Lid) failed: %lu", GetLastError());
    }

    RegisterPowerButtonRawInput();
    return true;
}

void Shutdown() {
    UnregisterTriggerHotkey();
    UnregisterPowerButtonRawInput();

    if (g_lidNotify) {
        UnregisterPowerSettingNotification(g_lidNotify);
        g_lidNotify = nullptr;
    }
    if (g_displayNotify) {
        UnregisterPowerSettingNotification(g_displayNotify);
        g_displayNotify = nullptr;
    }
    g_hWnd = nullptr;
}

} // namespace InputListener
