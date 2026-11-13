#pragma once

#include <windows.h>

namespace AboutDialog {

// Shows modal About dialog displaying version, build time, and links.
void Show(HWND hParentWnd);

} // namespace AboutDialog
