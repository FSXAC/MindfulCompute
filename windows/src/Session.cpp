#include "Session.h"
#include "Journal.h"
#include "Sound.h"
#include "Log.h"
#include <ctime>
#include <cmath>
#include <algorithm>

namespace {
// Wall-clock milliseconds since the Unix epoch, from the system clock (moves
// forward across sleep/lock, unlike GetTickCount64).
unsigned long long nowWallMs() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    // FILETIME is 100 ns ticks since 1601; only differences matter here.
    return u.QuadPart / 10000ULL;
}
bool envPresent(const wchar_t* name) {
    return GetEnvironmentVariableW(name, nullptr, 0) != 0;
}
int envInt(const wchar_t* name, int fallback) {
    wchar_t buf[64];
    DWORD n = GetEnvironmentVariableW(name, buf, 64);
    if (n == 0 || n >= 64) return fallback;
    int v = _wtoi(buf);
    return v > 0 ? v : fallback;
}
const wchar_t* phaseName(Phase p) {
    switch (p) {
    case Phase::Idle:    return L"Idle";
    case Phase::Running: return L"Running";
    case Phase::Resting: return L"Resting";
    }
    return L"?";
}
} // namespace

PageController::PageController() {
    srand(static_cast<unsigned>(time(nullptr)));
    quote_ = Quotes::random();

    if (envPresent(L"MINDFUL_SECONDS")) {
        secondsPerUnit_ = 1.0;
        Log::write(L"[session] MINDFUL_SECONDS set: slider counts seconds (labels still say minutes)");
    }
    if (envPresent(L"MINDFUL_BREAK_AUTODISMISS_SECONDS")) {
        breakOverrideSec_ = envInt(L"MINDFUL_BREAK_AUTODISMISS_SECONDS", 0);
        Log::write(L"[session] MINDFUL_BREAK_AUTODISMISS_SECONDS = %d s (dev override)", breakOverrideSec_);
    }
}

std::wstring PageController::trimmed(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

void PageController::setMinutes(int m) {
    if (m < 5) m = 5;
    if (m > 60) m = 60;
    m = ((m + 2) / 5) * 5;        // snap to nearest 5
    if (m < 5) m = 5; if (m > 60) m = 60;
    minutes_ = m;
}

int PageController::remainingSeconds() const {
    if (phase_ != Phase::Running || endWallMs_ == 0) return 0;
    unsigned long long now = nowWallMs();
    if (now >= endWallMs_) return 0;
    return static_cast<int>((endWallMs_ - now + 999) / 1000);
}

void PageController::setPhase(Phase p) {
    if (phase_ == p) return;
    Phase prev = phase_;
    phase_ = p;
    // Sessions have no pause state by design (the countdown is wall-clock
    // anchored) -- the lifecycle is just these three transitions.
    Log::write(L"[session] phase %ls -> %ls", phaseName(prev), phaseName(p));
    Log::flush();   // lifecycle transitions are important events: land them on disk
    if (onPhaseChanged) onPhaseChanged(p);
}

bool PageController::begin() {
    if (!canBegin()) return false;
    GetSystemTimeAsFileTime(&sessionStartFt_);
    startWallMs_ = nowWallMs();
    double lengthSec = minutes_ * secondsPerUnit_;
    endWallMs_ = startWallMs_ + static_cast<unsigned long long>(std::llround(lengthSec * 1000.0));
    Log::write(L"[session] begin: intention=\"%ls\" minutes=%d length=%.0fs (wall-clock anchored)",
               intention().c_str(), minutes_, lengthSec);
    setPhase(Phase::Running);
    Sound::playBowl();
    return true;
}

void PageController::endEarly() {
    if (phase_ != Phase::Running) return;
    finish();
}

void PageController::finish() {
    unsigned long long now = nowWallMs();
    double elapsedSec = (now - startWallMs_) / 1000.0;
    // Snapshot the real length: slider units for the break header, wall-clock
    // minutes for the journal, so lingering on the break panel doesn't drift it.
    completedUnits_ = std::max(1, (int)std::llround(elapsedSec / secondsPerUnit_));
    completedWallMinutes_ = std::max(1, (int)std::llround(elapsedSec / 60.0));
    quote_ = Quotes::random();
    startRestTimeout(now);
    setPhase(Phase::Resting);
    Sound::playBowl();
}

void PageController::startRestTimeout(unsigned long long nowMs) {
    // Matches Swift's 10 * secondsPerUnit; the dev override (absolute seconds)
    // wins when present so the auto-dismiss can be exercised quickly.
    double seconds = breakOverrideSec_ > 0 ? (double)breakOverrideSec_ : 10.0 * secondsPerUnit_;
    breakDeadlineMs_ = nowMs + static_cast<unsigned long long>(std::llround(seconds * 1000.0));
    Log::write(L"[session] break auto-dismiss in %.0fs", seconds);
}

void PageController::continueFromBreak() {
    if (phase_ != Phase::Resting) return;
    Log::write(L"[session] continue: reflection=\"%ls\"", reflection().c_str());
    Journal::append(sessionStartFt_, minutes_, completedWallMinutes_,
                    intention(), reflection());
    intention_.clear();
    reflection_.clear();
    breakDeadlineMs_ = 0;
    setPhase(Phase::Idle);
}

void PageController::update() {
    unsigned long long now = nowWallMs();
    if (phase_ == Phase::Running && endWallMs_ != 0 && now >= endWallMs_) {
        Log::write(L"[session] session elapsed (wall-clock) -> finish");
        finish();
    } else if (phase_ == Phase::Resting && breakDeadlineMs_ != 0 && now >= breakDeadlineMs_) {
        Log::write(L"[session] break auto-dismiss (untouched) -> continue (empty reflection)");
        continueFromBreak();
    }
}
