#pragma once
// Minimal append-only diagnostics log for the Phase 0 spike.
// Phase 1+ can keep this for a debug channel or delete it; it is intentionally
// dependency-free and single-threaded (all callers are on the UI thread).
#include <windows.h>
#include <string>
#include <cstdio>
#include <cstdarg>
#include <mutex>

namespace Log {

inline std::wstring& path() {
    static std::wstring p;
    return p;
}
inline std::mutex& mtx() {
    static std::mutex m;
    return m;
}

inline void init(const std::wstring& file) {
    path() = file;
    FILE* f = nullptr;
    if (_wfopen_s(&f, file.c_str(), L"w, ccs=UTF-8") == 0 && f) {
        SYSTEMTIME st; GetLocalTime(&st);
        fwprintf(f, L"=== MindfulCompute Windows spike log  %04d-%02d-%02d %02d:%02d:%02d ===\n",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        fclose(f);
    }
}

inline void write(const wchar_t* fmt, ...) {
    wchar_t buf[2048];
    va_list ap; va_start(ap, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);

    std::lock_guard<std::mutex> lock(mtx());
    SYSTEMTIME st; GetLocalTime(&st);
    FILE* f = nullptr;
    if (!path().empty() && _wfopen_s(&f, path().c_str(), L"a, ccs=UTF-8") == 0 && f) {
        fwprintf(f, L"[%02d:%02d:%02d.%03d] %ls\n",
                 st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf);
        fclose(f);
    }
    OutputDebugStringW(buf);
    OutputDebugStringW(L"\n");
}

} // namespace Log
