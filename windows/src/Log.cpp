#include "Log.h"
#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <vector>
#include <algorithm>

namespace {

// Keep ~5 most recent run logs; older ones are pruned on startup. A "run" is one
// process launch = one log file (a crash also drops a matching .dmp, left alone
// since crashes are rare and the dump is the evidence).
constexpr size_t kMaxRuns = 5;

std::mutex   g_mtx;               // callers are all on the UI thread, but the
                                  // crash handler and PlaySound internals mean a
                                  // lock is cheap insurance against interleaving.
FILE*        g_file = nullptr;    // held open for the whole run, buffered.
std::wstring g_dir;               // logs directory (for "Open Logs Folder").
std::wstring g_path;              // this run's log file.
bool         g_verbose = false;   // MINDFUL_LOG_VERBOSE -> DEBUG lines emitted.

std::wstring appDataRoot() {
    PWSTR raw = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &raw)) && raw)
        out = raw;
    if (raw) CoTaskMemFree(raw);
    return out;
}

std::wstring envStr(const wchar_t* name) {
    wchar_t buf[1024];
    DWORD n = GetEnvironmentVariableW(name, buf, 1024);
    return (n > 0 && n < 1024) ? std::wstring(buf, n) : std::wstring();
}

std::wstring stamp() {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t b[32];
    swprintf_s(b, L"%04d%02d%02d-%02d%02d%02d",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return b;
}

// Delete all but the newest (kMaxRuns-1) run-*.log so this run makes kMaxRuns.
// The timestamped filenames sort chronologically, so a lexical sort is enough.
void pruneOldRuns(const std::wstring& d) {
    std::vector<std::wstring> logs;
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((d + L"\\run-*.log").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                logs.push_back(fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (logs.size() < kMaxRuns) return;                 // room already
    std::sort(logs.begin(), logs.end());
    size_t keep = kMaxRuns - 1;                          // leave space for this run
    for (size_t i = 0; i + keep < logs.size(); ++i)
        DeleteFileW((d + L"\\" + logs[i]).c_str());
}

void emit(const wchar_t* level, const wchar_t* buf) {
    std::lock_guard<std::mutex> lock(g_mtx);
    SYSTEMTIME st; GetLocalTime(&st);
    if (g_file) {
        fwprintf(g_file, L"[%02d:%02d:%02d.%03d] %-5ls %ls\n",
                 st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, level, buf);
    }
    OutputDebugStringW(buf);
    OutputDebugStringW(L"\n");
}

void formatEmit(const wchar_t* level, const wchar_t* fmt, va_list ap) {
    wchar_t buf[2048];
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, ap);
    emit(level, buf);
}

} // namespace

namespace Log {

void init() {
    std::wstring v = envStr(L"MINDFUL_LOG_VERBOSE");
    g_verbose = (!v.empty() && v != L"0");

    // Automation override kept from the Phase 0 spike: an exact file path.
    std::wstring override = envStr(L"MINDFUL_SPIKE_LOG");
    if (!override.empty()) {
        g_path = override;
        size_t slash = g_path.find_last_of(L"\\/");
        g_dir = (slash == std::wstring::npos) ? L"." : g_path.substr(0, slash);
    } else {
        std::wstring root = appDataRoot();
        if (!root.empty()) {
            std::wstring appDir = root + L"\\MindfulCompute";
            CreateDirectoryW(appDir.c_str(), nullptr);          // ok if it exists
            g_dir = appDir + L"\\logs";
            CreateDirectoryW(g_dir.c_str(), nullptr);
            pruneOldRuns(g_dir);
            g_path = g_dir + L"\\run-" + stamp() + L".log";
        }
    }

    if (!g_path.empty())
        _wfopen_s(&g_file, g_path.c_str(), L"w, ccs=UTF-8");

    if (g_file) {
        SYSTEMTIME st; GetLocalTime(&st);
        fwprintf(g_file,
                 L"=== MindfulCompute log  %04d-%02d-%02d %02d:%02d:%02d  level=%ls ===\n",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                 g_verbose ? L"DEBUG" : L"INFO");
        fflush(g_file);
    }
}

void write(const wchar_t* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    formatEmit(L"INFO", fmt, ap);
    va_end(ap);
}

void debug(const wchar_t* fmt, ...) {
    if (!g_verbose) return;
    va_list ap; va_start(ap, fmt);
    formatEmit(L"DEBUG", fmt, ap);
    va_end(ap);
}

void flush() {
    std::lock_guard<std::mutex> lock(g_mtx);
    if (g_file) fflush(g_file);
}

const std::wstring& dir()  { return g_dir; }
const std::wstring& file() { return g_path; }

} // namespace Log
