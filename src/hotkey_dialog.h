#pragma once

#include <windows.h>
#include <string>

namespace HotkeyDialog {

// Shows modal dialog to change hotkey. Returns true if hotkey was changed.
bool Show(HWND hParentWnd);

} // namespace HotkeyDialog
