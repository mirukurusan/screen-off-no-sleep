#include "about_dialog.h"
#include "../res/resource.h"
#include <commctrl.h>
#include <shellapi.h>
#include <string>

#define WIDEN2(x) L##x
#define WIDEN(x) WIDEN2(x)

namespace AboutDialog {

static HFONT g_hTitleFont = nullptr;

static INT_PTR CALLBACK DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_INITDIALOG: {
            HWND hTitle = GetDlgItem(hDlg, IDC_ABOUT_APP_TITLE);
            if (hTitle) {
                HFONT hDefaultFont = reinterpret_cast<HFONT>(SendMessage(hTitle, WM_GETFONT, 0, 0));
                LOGFONTW lf = {};
                if (hDefaultFont && GetObjectW(hDefaultFont, sizeof(LOGFONTW), &lf)) {
                    lf.lfWeight = FW_BOLD;
                    g_hTitleFont = CreateFontIndirectW(&lf);
                    if (g_hTitleFont) {
                        SendMessage(hTitle, WM_SETFONT, reinterpret_cast<WPARAM>(g_hTitleFont), TRUE);
                    }
                }
            }

            std::wstring verText = L"Version: ";
            verText += WIDEN(APP_VERSION_STR);
            SetDlgItemTextW(hDlg, IDC_ABOUT_VERSION, verText.c_str());

            std::wstring buildText = L"Build Time: ";
            buildText += WIDEN(__DATE__);
            buildText += L" ";
            buildText += WIDEN(__TIME__);
            SetDlgItemTextW(hDlg, IDC_ABOUT_BUILD_TIME, buildText.c_str());

            SetDlgItemTextW(hDlg, IDC_ABOUT_SYSLINK_AUTHOR,
                            L"Author: <a href=\"https://github.com/mirukurusan\">mirukurusan</a>");
            SetDlgItemTextW(hDlg, IDC_ABOUT_SYSLINK_REPO,
                            L"GitHub: <a href=\"https://github.com/mirukurusan/screen-off-no-sleep\">screen-off-no-sleep</a>");

            return TRUE;
        }

        case WM_NOTIFY: {
            LPNMHDR pnmh = reinterpret_cast<LPNMHDR>(lParam);
            if (pnmh->code == NM_CLICK || pnmh->code == NM_RETURN) {
                PNMLINK pNMLink = reinterpret_cast<PNMLINK>(lParam);
                if (pNMLink->item.szUrl[0] != L'\0') {
                    ShellExecuteW(hDlg, L"open", pNMLink->item.szUrl, nullptr, nullptr, SW_SHOWNORMAL);
                    return TRUE;
                }
            }
            break;
        }

        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            if (id == IDOK || id == IDCANCEL) {
                EndDialog(hDlg, id);
                return TRUE;
            }
            break;
        }

        case WM_DESTROY: {
            if (g_hTitleFont) {
                DeleteObject(g_hTitleFont);
                g_hTitleFont = nullptr;
            }
            break;
        }
    }
    return FALSE;
}

void Show(HWND hParentWnd) {
    DialogBoxParamW(
        GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDD_ABOUT_DIALOG),
        hParentWnd,
        DialogProc,
        0
    );
}

} // namespace AboutDialog
