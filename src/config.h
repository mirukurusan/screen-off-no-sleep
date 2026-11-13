#pragma once

#include <windows.h>
#include <string>

#pragma pack(push, 1)
struct SavedPowerState {
    GUID scheme;
    DWORD acTimeout;
    DWORD dcTimeout;
};
#pragma pack(pop)

namespace Config {

// Registry value names
constexpr const wchar_t* REG_KEY_PATH = L"Software\\ScreenOffNoSleep";
constexpr const wchar_t* VAL_LOCK_ON_SCREEN_OFF = L"LockOnScreenOff";
constexpr const wchar_t* VAL_HANDLE_LID_CLOSE = L"HandleLidClose";
constexpr const wchar_t* VAL_HANDLE_POWER_BTN = L"HandlePowerButton";
constexpr const wchar_t* VAL_TRIGGER_HOTKEY = L"TriggerHotkey";
constexpr const wchar_t* VAL_SAVED_POWER_STATE = L"SavedPowerState";

// Core Settings I/O
bool LoadAppSettings(bool* lockOnScreenOff, bool* handleLidClose, bool* handlePowerButton, std::wstring* triggerHotkey);
bool SaveSettingDword(const wchar_t* name, DWORD value);
bool SaveSettingString(const wchar_t* name, const std::wstring& value);

// Recovery Power State I/O
bool SaveSavedPowerState(const SavedPowerState& state);
bool LoadSavedPowerState(SavedPowerState* state);
void DeleteSavedPowerState();

// Windows Startup Autostart
bool IsAutoStartEnabled();
bool SetAutoStartEnabled(bool enable);

// Logging helper (OutputDebugStringW)
void Log(const wchar_t* format, ...);

} // namespace Config
