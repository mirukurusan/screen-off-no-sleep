#include "config.h"
#include <cstdio>
#include <vector>
#include <initguid.h>
#include <taskschd.h>

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

static const wchar_t* AUTOSTART_TASK_NAME = L"ScreenOffNoSleep";

struct ComScope {
    HRESULT hr;
    ComScope() : hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComScope() {
        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
    }
};

bool IsAutoStartEnabled() {
    ComScope com;

    ITaskService* pService = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskService, reinterpret_cast<void**>(&pService));
    if (FAILED(hr) || !pService) {
        return false;
    }

    VARIANT varEmpty;
    VariantInit(&varEmpty);
    hr = pService->Connect(varEmpty, varEmpty, varEmpty, varEmpty);
    if (FAILED(hr)) {
        pService->Release();
        return false;
    }

    ITaskFolder* pRootFolder = nullptr;
    BSTR bstrRoot = SysAllocString(L"\\");
    hr = pService->GetFolder(bstrRoot, &pRootFolder);
    SysFreeString(bstrRoot);
    if (FAILED(hr) || !pRootFolder) {
        pService->Release();
        return false;
    }

    bool isEnabled = false;
    BSTR bstrTaskName = SysAllocString(AUTOSTART_TASK_NAME);
    IRegisteredTask* pTask = nullptr;
    hr = pRootFolder->GetTask(bstrTaskName, &pTask);
    SysFreeString(bstrTaskName);

    if (SUCCEEDED(hr) && pTask) {
        TASK_STATE state = TASK_STATE_UNKNOWN;
        if (SUCCEEDED(pTask->get_State(&state))) {
            isEnabled = (state != TASK_STATE_DISABLED);
        } else {
            VARIANT_BOOL vbEnabled = VARIANT_FALSE;
            if (SUCCEEDED(pTask->get_Enabled(&vbEnabled))) {
                isEnabled = (vbEnabled == VARIANT_TRUE);
            }
        }
        pTask->Release();
    }

    pRootFolder->Release();
    pService->Release();
    return isEnabled;
}

bool SetAutoStartEnabled(bool enable) {
    ComScope com;

    ITaskService* pService = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskService, reinterpret_cast<void**>(&pService));
    if (FAILED(hr) || !pService) {
        Log(L"CoCreateInstance(CLSID_TaskScheduler) failed: 0x%08X", hr);
        return false;
    }

    VARIANT varEmpty;
    VariantInit(&varEmpty);
    hr = pService->Connect(varEmpty, varEmpty, varEmpty, varEmpty);
    if (FAILED(hr)) {
        Log(L"ITaskService::Connect failed: 0x%08X", hr);
        pService->Release();
        return false;
    }

    ITaskFolder* pRootFolder = nullptr;
    BSTR bstrRoot = SysAllocString(L"\\");
    hr = pService->GetFolder(bstrRoot, &pRootFolder);
    SysFreeString(bstrRoot);
    if (FAILED(hr) || !pRootFolder) {
        Log(L"ITaskService::GetFolder failed: 0x%08X", hr);
        pService->Release();
        return false;
    }

    BSTR bstrTaskName = SysAllocString(AUTOSTART_TASK_NAME);
    bool success = false;

    if (!enable) {
        hr = pRootFolder->DeleteTask(bstrTaskName, 0);
        success = SUCCEEDED(hr) || (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
        if (!success) {
            Log(L"ITaskFolder::DeleteTask failed: 0x%08X", hr);
        }
    } else {
        ITaskDefinition* pTask = nullptr;
        hr = pService->NewTask(0, &pTask);
        if (SUCCEEDED(hr) && pTask) {
            IRegistrationInfo* pRegInfo = nullptr;
            if (SUCCEEDED(pTask->get_RegistrationInfo(&pRegInfo)) && pRegInfo) {
                BSTR bstrAuthor = SysAllocString(L"ScreenOffNoSleep");
                pRegInfo->put_Author(bstrAuthor);
                SysFreeString(bstrAuthor);

                BSTR bstrDesc = SysAllocString(L"Turn Off Display Without Sleep - Auto Start");
                pRegInfo->put_Description(bstrDesc);
                SysFreeString(bstrDesc);

                pRegInfo->Release();
            }

            IPrincipal* pPrincipal = nullptr;
            if (SUCCEEDED(pTask->get_Principal(&pPrincipal)) && pPrincipal) {
                pPrincipal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
                pPrincipal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);
                pPrincipal->Release();
            }

            ITaskSettings* pSettings = nullptr;
            if (SUCCEEDED(pTask->get_Settings(&pSettings)) && pSettings) {
                pSettings->put_StartWhenAvailable(VARIANT_TRUE);
                pSettings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
                pSettings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
                pSettings->put_AllowHardTerminate(VARIANT_TRUE);
                pSettings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW);
                pSettings->put_Priority(7);

                BSTR bstrTimeLimit = SysAllocString(L"PT0S");
                pSettings->put_ExecutionTimeLimit(bstrTimeLimit);
                SysFreeString(bstrTimeLimit);

                pSettings->Release();
            }

            ITriggerCollection* pTriggers = nullptr;
            if (SUCCEEDED(pTask->get_Triggers(&pTriggers)) && pTriggers) {
                ITrigger* pTrigger = nullptr;
                if (SUCCEEDED(pTriggers->Create(TASK_TRIGGER_LOGON, &pTrigger)) && pTrigger) {
                    ILogonTrigger* pLogonTrigger = nullptr;
                    if (SUCCEEDED(pTrigger->QueryInterface(IID_ILogonTrigger, reinterpret_cast<void**>(&pLogonTrigger))) && pLogonTrigger) {
                        pLogonTrigger->put_Enabled(VARIANT_TRUE);
                        pLogonTrigger->Release();
                    }
                    pTrigger->Release();
                }
                pTriggers->Release();
            }

            IActionCollection* pActions = nullptr;
            if (SUCCEEDED(pTask->get_Actions(&pActions)) && pActions) {
                IAction* pAction = nullptr;
                if (SUCCEEDED(pActions->Create(TASK_ACTION_EXEC, &pAction)) && pAction) {
                    IExecAction* pExecAction = nullptr;
                    if (SUCCEEDED(pAction->QueryInterface(IID_IExecAction, reinterpret_cast<void**>(&pExecAction))) && pExecAction) {
                        wchar_t exePath[MAX_PATH] = {0};
                        GetModuleFileNameW(nullptr, exePath, MAX_PATH);

                        std::wstring workingDir = exePath;
                        size_t lastSlash = workingDir.find_last_of(L"\\/");
                        if (lastSlash != std::wstring::npos) {
                            workingDir = workingDir.substr(0, lastSlash);
                        }

                        BSTR bstrPath = SysAllocString(exePath);
                        BSTR bstrDir = SysAllocString(workingDir.c_str());

                        pExecAction->put_Path(bstrPath);
                        pExecAction->put_WorkingDirectory(bstrDir);

                        SysFreeString(bstrPath);
                        SysFreeString(bstrDir);

                        pExecAction->Release();
                    }
                    pAction->Release();
                }
                pActions->Release();
            }

            IRegisteredTask* pRegisteredTask = nullptr;
            hr = pRootFolder->RegisterTaskDefinition(
                bstrTaskName,
                pTask,
                TASK_CREATE_OR_UPDATE,
                varEmpty,
                varEmpty,
                TASK_LOGON_INTERACTIVE_TOKEN,
                varEmpty,
                &pRegisteredTask
            );

            if (SUCCEEDED(hr)) {
                success = true;
                if (pRegisteredTask) {
                    pRegisteredTask->Release();
                }
            } else {
                Log(L"ITaskFolder::RegisterTaskDefinition failed: 0x%08X", hr);
            }

            pTask->Release();
        } else {
            Log(L"ITaskService::NewTask failed: 0x%08X", hr);
        }
    }

    HKEY hRunKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hRunKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hRunKey, AUTOSTART_TASK_NAME);
        RegCloseKey(hRunKey);
    }

    SysFreeString(bstrTaskName);
    pRootFolder->Release();
    pService->Release();

    return success;
}

} // namespace Config
