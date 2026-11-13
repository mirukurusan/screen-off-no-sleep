#include "hotkey_dialog.h"
#include "input_listener.h"
#include "config.h"
#include "../res/resource.h"
#include <vector>
#include <algorithm>

namespace HotkeyDialog {

struct KeyEntry {
    const wchar_t* displayName;
    const wchar_t* stringValue;
};

static const KeyEntry kKeyEntries[] = {
    // Letters
    {L"A", L"A"}, {L"B", L"B"}, {L"C", L"C"}, {L"D", L"D"}, {L"E", L"E"},
    {L"F", L"F"}, {L"G", L"G"}, {L"H", L"H"}, {L"I", L"I"}, {L"J", L"J"},
    {L"K", L"K"}, {L"L", L"L"}, {L"M", L"M"}, {L"N", L"N"}, {L"O", L"O"},
    {L"P", L"P"}, {L"Q", L"Q"}, {L"R", L"R"}, {L"S", L"S"}, {L"T", L"T"},
    {L"U", L"U"}, {L"V", L"V"}, {L"W", L"W"}, {L"X", L"X"}, {L"Y", L"Y"},
    {L"Z", L"Z"},

    // Numbers
    {L"0", L"0"}, {L"1", L"1"}, {L"2", L"2"}, {L"3", L"3"}, {L"4", L"4"},
    {L"5", L"5"}, {L"6", L"6"}, {L"7", L"7"}, {L"8", L"8"}, {L"9", L"9"},

    // Function keys
    {L"F1", L"F1"},   {L"F2", L"F2"},   {L"F3", L"F3"},   {L"F4", L"F4"},
    {L"F5", L"F5"},   {L"F6", L"F6"},   {L"F7", L"F7"},   {L"F8", L"F8"},
    {L"F9", L"F9"},   {L"F10", L"F10"}, {L"F11", L"F11"}, {L"F12", L"F12"},

    // Common keys
    {L"Space", L"Space"},             {L"Tab", L"Tab"},
    {L"Enter", L"Enter"},             {L"Escape", L"Esc"},
    {L"Backspace", L"Backspace"},     {L"Delete", L"Delete"},
    {L"Insert", L"Insert"},           {L"Home", L"Home"},
    {L"End", L"End"},                 {L"Page Up", L"PageUp"},
    {L"Page Down", L"PageDown"},      {L"Up Arrow", L"Up"},
    {L"Down Arrow", L"Down"},         {L"Left Arrow", L"Left"},
    {L"Right Arrow", L"Right"},       {L"Pause", L"Pause"},
    {L"Print Screen", L"PrintScreen"},
    {L"Minus (-)", L"-"},             {L"Plus (+)", L"Plus"},
    {L"Comma (,)", L","},             {L"Period (.)", L"."},
    {L"Slash (/)", L"/"},             {L"Semicolon (;)", L";"},
    {L"Quote (')", L"'"},             {L"Backquote (`)", L"`"},
    {L"Left Bracket ([)", L"["},      {L"Right Bracket (])", L"]"},
    {L"Backslash (\\)", L"\\"},
};

static std::wstring BuildHotkeyString(HWND hDlg) {
    bool win = (IsDlgButtonChecked(hDlg, IDC_CHK_WIN) == BST_CHECKED);
    bool alt = (IsDlgButtonChecked(hDlg, IDC_CHK_ALT) == BST_CHECKED);
    bool ctrl = (IsDlgButtonChecked(hDlg, IDC_CHK_CTRL) == BST_CHECKED);
    bool shift = (IsDlgButtonChecked(hDlg, IDC_CHK_SHIFT) == BST_CHECKED);

    HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_KEY);
    int sel = static_cast<int>(SendMessage(hCombo, CB_GETCURSEL, 0, 0));

    std::wstring result;
    if (win) { if (!result.empty()) result += L"+"; result += L"Win"; }
    if (alt) { if (!result.empty()) result += L"+"; result += L"Alt"; }
    if (ctrl) { if (!result.empty()) result += L"+"; result += L"Ctrl"; }
    if (shift) { if (!result.empty()) result += L"+"; result += L"Shift"; }

    if (sel >= 0 && sel < static_cast<int>(ARRAYSIZE(kKeyEntries))) {
        if (!result.empty()) result += L"+";
        result += kKeyEntries[sel].stringValue;
    }
    return result;
}

static void UpdatePreview(HWND hDlg) {
    std::wstring str = BuildHotkeyString(hDlg);
    std::wstring previewText = L"Preview: " + (str.empty() ? L"(Disabled / Incomplete)" : str);
    SetDlgItemTextW(hDlg, IDC_STATIC_PREVIEW, previewText.c_str());
}

static INT_PTR CALLBACK DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_INITDIALOG: {
            HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_KEY);
            for (const auto& entry : kKeyEntries) {
                SendMessageW(hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(entry.displayName));
            }

            std::wstring current = InputListener::GetTriggerHotkey();
            UINT mods = 0;
            UINT vk = 0;
            bool parsed = InputListener::ParseHotkey(current, &mods, &vk);

            CheckDlgButton(hDlg, IDC_CHK_WIN, (mods & MOD_WIN) ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hDlg, IDC_CHK_ALT, (mods & MOD_ALT) ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hDlg, IDC_CHK_CTRL, (mods & MOD_CONTROL) ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hDlg, IDC_CHK_SHIFT, (mods & MOD_SHIFT) ? BST_CHECKED : BST_UNCHECKED);

            int selectIndex = -1;
            if (parsed) {
                // Find matching entry
                for (size_t i = 0; i < ARRAYSIZE(kKeyEntries); i++) {
                    UINT testMods = 0, testVk = 0;
                    if (InputListener::ParseHotkey(kKeyEntries[i].stringValue, &testMods, &testVk) && testVk == vk) {
                        selectIndex = static_cast<int>(i);
                        break;
                    }
                }
            }

            if (selectIndex >= 0) {
                SendMessage(hCombo, CB_SETCURSEL, selectIndex, 0);
            } else {
                SendMessage(hCombo, CB_SETCURSEL, 0, 0);
            }

            UpdatePreview(hDlg);
            return TRUE;
        }

        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            WORD code = HIWORD(wParam);

            if (id == IDC_CHK_WIN || id == IDC_CHK_ALT || id == IDC_CHK_CTRL || id == IDC_CHK_SHIFT) {
                if (code == BN_CLICKED) UpdatePreview(hDlg);
            } else if (id == IDC_COMBO_KEY && (code == CBN_SELCHANGE)) {
                UpdatePreview(hDlg);
            } else if (id == IDC_BTN_DISABLE) {
                InputListener::SetTriggerHotkey(L"");
                EndDialog(hDlg, IDOK);
                return TRUE;
            } else if (id == IDOK) {
                HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_KEY);
                int sel = static_cast<int>(SendMessage(hCombo, CB_GETCURSEL, 0, 0));
                if (sel < 0) {
                    MessageBoxW(hDlg, L"Please select a key.", L"Invalid Hotkey", MB_ICONWARNING | MB_OK);
                    return TRUE;
                }

                std::wstring hotkeyStr = BuildHotkeyString(hDlg);
                UINT mods = 0;
                UINT vk = 0;
                if (!InputListener::ParseHotkey(hotkeyStr, &mods, &vk) || mods == 0) {
                    MessageBoxW(hDlg, L"Please select at least one modifier key (Win, Alt, Ctrl, Shift).",
                                L"Invalid Hotkey", MB_ICONWARNING | MB_OK);
                    return TRUE;
                }

                if (!InputListener::SetTriggerHotkey(hotkeyStr)) {
                    MessageBoxW(hDlg, L"Cannot register this hotkey combination.\nIt may already be reserved by Windows or another application.",
                                L"Hotkey Registration Failed", MB_ICONERROR | MB_OK);
                    return TRUE;
                }

                EndDialog(hDlg, IDOK);
                return TRUE;
            } else if (id == IDCANCEL) {
                EndDialog(hDlg, IDCANCEL);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

bool Show(HWND hParentWnd) {
    INT_PTR result = DialogBoxParamW(
        GetModuleHandle(nullptr),
        MAKEINTRESOURCEW(IDD_HOTKEY_DIALOG),
        hParentWnd,
        DialogProc,
        0
    );
    return (result == IDOK);
}

} // namespace HotkeyDialog
