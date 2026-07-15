#include "LoginItem.h"
#include "Log.h"
#include <windows.h>
#include <string>

namespace {
const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kValue  = L"MindfulCompute";

std::wstring quotedExePath() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf, (n > 0 && n < MAX_PATH) ? n : 0);
    return L"\"" + p + L"\"";
}
} // namespace

bool LoginItem::isEnabled() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;
    LONG r = RegQueryValueExW(key, kValue, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}

bool LoginItem::setEnabled(bool enable) {
    HKEY key;
    // The Run key always exists on Windows, but create-if-absent is harmless.
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        Log::write(L"[login] could not open Run key");
        return false;
    }
    bool ok;
    if (enable) {
        std::wstring val = quotedExePath();
        LONG r = RegSetValueExW(key, kValue, 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(val.c_str()),
                                (DWORD)((val.size() + 1) * sizeof(wchar_t)));
        ok = (r == ERROR_SUCCESS);
        Log::write(L"[login] enable -> %ls (value=%ls)", ok ? L"OK" : L"FAIL", val.c_str());
    } else {
        LONG r = RegDeleteValueW(key, kValue);
        ok = (r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND);
        Log::write(L"[login] disable -> %ls", ok ? L"OK" : L"FAIL");
    }
    RegCloseKey(key);
    return ok;
}
