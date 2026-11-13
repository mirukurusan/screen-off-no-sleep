#include "config.h"
#include <cstdio>
#include <vector>

namespace Config {

void Log(const wchar_t* format, ...) {
    wchar_t buffer[1024];
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(buffer, _countof(buffer), _TRUNCATE, format, args);
    va_end(args);

    OutputDebugStringW(L"[ScreenOffNoSleep] ");
    OutputDebugStringW(buffer);
    OutputDebugStringW(L"\n");
}

static HKEY OpenAppRegistryKey(bool writeAccess) {
    HKEY hKey = nullptr;
    REGSAM access = writeAccess ? (KEY_READ | KEY_WRITE) : KEY_READ;
    DWORD disposition = 0;

    LSTATUS status = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        REG_KEY_PATH,
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        access,
        nullptr,
        &hKey,
        &disposition
    );

    if (status != ERROR_SUCCESS) {
        Log(L"RegCreateKeyExW failed: %ld", status);
        return nullptr;
    }
    return hKey;
}

bool LoadAppSettings(bool* lockOnScreenOff, bool* handleLidClose, bool* handlePowerButton, std::wstring* triggerHotkey) {
    HKEY hKey = OpenAppRegistryKey(false);
    if (!hKey) {
        *lockOnScreenOff = false;
        *handleLidClose = false;
        *handlePowerButton = false;
        *triggerHotkey = L"Win+Alt+L";
        return false;
    }

    auto ReadDword = [&](const wchar_t* name, bool defaultVal) -> bool {
        DWORD val = 0;
        DWORD size = sizeof(val);
        DWORD type = REG_DWORD;
        if (RegQueryValueExW(hKey, name, nullptr, &type, reinterpret_cast<LPBYTE>(&val), &size) == ERROR_SUCCESS) {
            return val != 0;
        }
        return defaultVal;
    };

    *lockOnScreenOff = ReadDword(VAL_LOCK_ON_SCREEN_OFF, false);
    *handleLidClose = ReadDword(VAL_HANDLE_LID_CLOSE, false);
    *handlePowerButton = ReadDword(VAL_HANDLE_POWER_BTN, false);

    wchar_t hotkeyBuf[128] = {0};
    DWORD hotkeySize = sizeof(hotkeyBuf);
    DWORD type = REG_SZ;
    if (RegQueryValueExW(hKey, VAL_TRIGGER_HOTKEY, nullptr, &type, reinterpret_cast<LPBYTE>(hotkeyBuf), &hotkeySize) == ERROR_SUCCESS) {
        *triggerHotkey = hotkeyBuf;
    } else {
        *triggerHotkey = L"Win+Alt+L";
    }

    RegCloseKey(hKey);
    return true;
}

bool SaveSettingDword(const wchar_t* name, DWORD value) {
    HKEY hKey = OpenAppRegistryKey(true);
    if (!hKey) return false;

    LSTATUS status = RegSetValueExW(
        hKey,
        name,
        0,
        REG_DWORD,
        reinterpret_cast<const BYTE*>(&value),
        sizeof(value)
    );
    RegCloseKey(hKey);
    return status == ERROR_SUCCESS;
}

bool SaveSettingString(const wchar_t* name, const std::wstring& value) {
    HKEY hKey = OpenAppRegistryKey(true);
    if (!hKey) return false;

    DWORD sizeInBytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    LSTATUS status = RegSetValueExW(
        hKey,
        name,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()),
        sizeInBytes
    );
    RegCloseKey(hKey);
    return status == ERROR_SUCCESS;
}

bool SaveSavedPowerState(const SavedPowerState& state) {
    HKEY hKey = OpenAppRegistryKey(true);
    if (!hKey) return false;

    LSTATUS status = RegSetValueExW(
        hKey,
        VAL_SAVED_POWER_STATE,
        0,
        REG_BINARY,
        reinterpret_cast<const BYTE*>(&state),
        sizeof(SavedPowerState)
    );
    RegCloseKey(hKey);
    return status == ERROR_SUCCESS;
}

bool LoadSavedPowerState(SavedPowerState* state) {
    HKEY hKey = OpenAppRegistryKey(false);
    if (!hKey) return false;

    DWORD size = sizeof(SavedPowerState);
    DWORD type = REG_BINARY;
    LSTATUS status = RegQueryValueExW(
        hKey,
        VAL_SAVED_POWER_STATE,
        nullptr,
        &type,
        reinterpret_cast<LPBYTE>(state),
        &size
    );
    RegCloseKey(hKey);
    return (status == ERROR_SUCCESS && size == sizeof(SavedPowerState));
}

void DeleteSavedPowerState() {
    HKEY hKey = OpenAppRegistryKey(true);
    if (hKey) {
        RegDeleteValueW(hKey, VAL_SAVED_POWER_STATE);
        RegCloseKey(hKey);
    }
}

static const wchar_t* RUN_KEY_PATH = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* RUN_VALUE_NAME = L"ScreenOffNoSleep";

bool IsAutoStartEnabled() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY_PATH, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    wchar_t pathBuf[MAX_PATH * 2] = {0};
    DWORD bufSize = sizeof(pathBuf);
    DWORD type = REG_SZ;
    LSTATUS status = RegQueryValueExW(hKey, RUN_VALUE_NAME, nullptr, &type, reinterpret_cast<LPBYTE>(pathBuf), &bufSize);
    RegCloseKey(hKey);
    return status == ERROR_SUCCESS;
}

bool SetAutoStartEnabled(bool enable) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY_PATH, 0, KEY_WRITE, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    LSTATUS status = ERROR_SUCCESS;
    if (enable) {
        wchar_t exePath[MAX_PATH] = {0};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);

        std::wstring command = L"\"";
        command += exePath;
        command += L"\"";

        DWORD sizeInBytes = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
        status = RegSetValueExW(
            hKey,
            RUN_VALUE_NAME,
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()),
            sizeInBytes
        );
    } else {
        status = RegDeleteValueW(hKey, RUN_VALUE_NAME);
    }

    RegCloseKey(hKey);
    return status == ERROR_SUCCESS;
}

} // namespace Config
