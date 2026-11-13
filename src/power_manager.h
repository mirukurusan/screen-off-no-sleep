#pragma once

#include <windows.h>
#include <powrprof.h>
#include <atomic>
#include "config.h"

namespace PowerManager {

constexpr UINT_PTR WATCHDOG_TIMER_ID = 0xD00E;
constexpr UINT WATCHDOG_TIMEOUT_MS = 3000;
constexpr ULONGLONG WAKE_GUARD_MS = 1500;

bool Initialize(HWND hWnd);
void Shutdown();

void TriggerDisplayOff();
void OnDisplayStateChange(DWORD state);
void OnWatchdogTimer();
void OnResumeFromSleep();

void RecoverSavedPowerState();

bool IsDisplayOn();
ULONGLONG GetLastDisplayOnTick();
bool IsFlowActive();

void SetLockOnScreenOff(bool enable);
bool GetLockOnScreenOff();

void ShowBlackoutOverlay();
void DestroyBlackoutOverlay();

} // namespace PowerManager
