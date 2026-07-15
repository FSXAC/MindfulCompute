#include "Sound.h"
#include "Log.h"
#include "resource.h"
#include <windows.h>
#include <mmsystem.h>
#include <string>

namespace {
std::wstring exeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    size_t slash = p.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? L"." : p.substr(0, slash);
}
} // namespace

void Sound::playBowl() {
    // Primary path: the bowl is embedded as a WAVE resource, so the shipped exe
    // is a single portable file. SND_NODEFAULT keeps us silent (rather than the
    // system default ding) if anything goes wrong.
    HMODULE mod = GetModuleHandleW(nullptr);
    BOOL ok = PlaySoundW(MAKEINTRESOURCEW(IDR_BOWL), mod,
                         SND_RESOURCE | SND_ASYNC | SND_NODEFAULT);
    if (ok) {
        Log::write(L"[sound] PlaySound(resource IDR_BOWL) -> TRUE");
        return;
    }
    // Trivial fallback: a wav sitting next to the exe (dev builds copy it there).
    std::wstring path = exeDir() + L"\\tibetan_bowl.wav";
    BOOL ok2 = PlaySoundW(path.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
    Log::write(L"[sound] resource miss; PlaySound(\"%ls\") -> %ls",
               path.c_str(), ok2 ? L"TRUE" : L"FALSE");
}
