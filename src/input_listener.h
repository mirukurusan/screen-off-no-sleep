#pragma once

#include <windows.h>
#include <string>

namespace InputListener {

constexpr int TRIGGER_HOTKEY_ID = 0x4D49;

bool Initialize(HWND hWnd);
void Shutdown();

void HandlePowerBroadcast(WPARAM wParam, LPARAM lParam);
void HandleRawInput(HRAWINPUT hRawInput);
void HandleHotkey(WPARAM wParam, LPARAM lParam);

bool ParseHotkey(const std::wstring& text, UINT* mods, UINT* vk);
bool SetTriggerHotkey(const std::wstring& hotkeyStr);
std::wstring GetTriggerHotkey();
void UnregisterTriggerHotkey();

void SetHandleLidClose(bool enable);
bool GetHandleLidClose();

void SetHandlePowerButton(bool enable);
bool GetHandlePowerButton();

} // namespace InputListener
