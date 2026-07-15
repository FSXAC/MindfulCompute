#include "CrashDump.h"
#include "Log.h"
#include <windows.h>
#include <dbghelp.h>      // MINIDUMP_* types only; dbghelp is NOT linked (see below)
#include <intrin.h>       // _ReturnAddress
#include <cstdlib>        // _set_purecall_handler, _set_invalid_parameter_handler
#include <csignal>
#include <string>

namespace {

// dbghelp.dll is resolved at crash time via LoadLibrary/GetProcAddress, so the
// normal run never loads or links it -- the happy path stays dependency-free.
typedef BOOL(WINAPI* MiniDumpWriteDump_t)(
    HANDLE hProcess, DWORD ProcessId, HANDLE hFile, MINIDUMP_TYPE DumpType,
    PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
    PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
    PMINIDUMP_CALLBACK_INFORMATION CallbackParam);

// Guard against re-entrancy: e.g. purecall/invalid-parameter fall through to
// abort(), which would otherwise fire our SIGABRT handler a second time.
LONG g_inCrash = 0;

// The .dmp shares the log's timestamped stem (run-YYYYMMDD-HHMMSS.dmp) so a dump
// and its log line up at a glance.
std::wstring dumpPath() {
    std::wstring log = Log::file();
    if (!log.empty()) {
        size_t dot = log.find_last_of(L'.');
        std::wstring stem = (dot == std::wstring::npos) ? log : log.substr(0, dot);
        return stem + L".dmp";
    }
    return L"MindfulCompute.dmp";
}

void logFault(EXCEPTION_POINTERS* ep) {
    if (!ep || !ep->ExceptionRecord) return;
    const EXCEPTION_RECORD* er = ep->ExceptionRecord;
    void* addr = er->ExceptionAddress;

    wchar_t modName[MAX_PATH] = L"?";
    uintptr_t off = 0;
    HMODULE mod = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)addr, &mod) && mod) {
        wchar_t full[MAX_PATH];
        if (GetModuleFileNameW(mod, full, MAX_PATH)) {
            std::wstring m(full);
            size_t slash = m.find_last_of(L"\\/");
            wcscpy_s(modName, (slash == std::wstring::npos) ? m.c_str()
                                                            : m.substr(slash + 1).c_str());
        }
        off = (uintptr_t)addr - (uintptr_t)mod;
    }
    Log::write(L"[crash] code=0x%08X addr=0x%p  %ls+0x%IX",
               (unsigned)er->ExceptionCode, addr, modName, off);

    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
        ULONG_PTR kind = er->ExceptionInformation[0];   // 0 read, 1 write, 8 execute
        ULONG_PTR data = er->ExceptionInformation[1];   // faulting data address
        Log::write(L"[crash] access violation %ls address 0x%p",
                   kind == 1 ? L"writing" : kind == 8 ? L"executing" : L"reading",
                   (void*)data);
    }
}

void writeMiniDump(EXCEPTION_POINTERS* ep) {
    HMODULE dbg = LoadLibraryW(L"dbghelp.dll");
    if (!dbg) { Log::write(L"[crash] dbghelp.dll load failed err=%lu", GetLastError()); return; }
    auto MDWD = (MiniDumpWriteDump_t)GetProcAddress(dbg, "MiniDumpWriteDump");
    if (!MDWD) { Log::write(L"[crash] MiniDumpWriteDump missing"); return; }

    std::wstring path = dumpPath();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        Log::write(L"[crash] cannot create dump %ls err=%lu", path.c_str(), GetLastError());
        return;
    }
    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId          = GetCurrentThreadId();
    mei.ExceptionPointers = ep;
    mei.ClientPointers    = FALSE;

    // Normal call stacks + the memory they point at + thread info: enough to see
    // the faulting stack and nearby state without ballooning the dump.
    MINIDUMP_TYPE type = (MINIDUMP_TYPE)(MiniDumpNormal |
                                         MiniDumpWithIndirectlyReferencedMemory |
                                         MiniDumpWithThreadInfo);
    BOOL ok = MDWD(GetCurrentProcess(), GetCurrentProcessId(), f, type,
                   ep ? &mei : nullptr, nullptr, nullptr);
    CloseHandle(f);
    Log::write(L"[crash] minidump %ls -> %ls", ok ? L"written" : L"FAILED", path.c_str());
}

// Full path for a real structured exception (the important one: e.g. the null
// deref while typing).
LONG WINAPI onUnhandled(EXCEPTION_POINTERS* ep) {
    if (InterlockedExchange(&g_inCrash, 1)) return EXCEPTION_EXECUTE_HANDLER;
    Log::write(L"[crash] ==== UNHANDLED EXCEPTION ====");
    logFault(ep);
    writeMiniDump(ep);
    Log::flush();
    return EXCEPTION_EXECUTE_HANDLER;   // let the process die
}

// CRT fatal paths (abort/purecall/invalid-parameter) hand us no
// EXCEPTION_POINTERS, so synthesize one from the current context/return address
// to still get a useful faulting stack into the dump.
void fatalNoException(const wchar_t* reason) {
    if (InterlockedExchange(&g_inCrash, 1)) return;
    Log::write(L"[crash] ==== FATAL: %ls ====", reason);
    CONTEXT ctx{};
    RtlCaptureContext(&ctx);
    EXCEPTION_RECORD er{};
    er.ExceptionCode    = 0xE0000001;               // app-defined "fatal CRT path"
    er.ExceptionAddress = _ReturnAddress();
    EXCEPTION_POINTERS ep{ &er, &ctx };
    writeMiniDump(&ep);
    Log::flush();
    // Die now rather than returning into the CRT (which would call abort() and
    // re-enter). The log is already flushed and the dump is written.
    TerminateProcess(GetCurrentProcess(), er.ExceptionCode);
}

void __cdecl onAbort(int) { fatalNoException(L"abort()"); }
void __cdecl onPurecall() { fatalNoException(L"pure virtual call"); }
void __cdecl onInvalidParameter(const wchar_t* expr, const wchar_t* func,
                                const wchar_t* file, unsigned line, uintptr_t) {
    Log::write(L"[crash] invalid CRT parameter: expr=%ls func=%ls file=%ls line=%u",
               expr ? expr : L"?", func ? func : L"?", file ? file : L"?", line);
    fatalNoException(L"invalid CRT parameter");
}

} // namespace

namespace CrashDump {

void install() {
    SetUnhandledExceptionFilter(onUnhandled);
    _set_purecall_handler(onPurecall);
    _set_invalid_parameter_handler(onInvalidParameter);
    signal(SIGABRT, onAbort);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT); // no dialog/WER popup
    Log::write(L"[crash] handlers installed (unhandled / abort / purecall / invalid-param)");
}

} // namespace CrashDump
