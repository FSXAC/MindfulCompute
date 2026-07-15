#pragma once
// Portable core re-expressed from MindfulCompute/SessionManager.swift.
//
// The real session state machine: idle -> running -> resting -> idle. The
// countdown is anchored to WALL-CLOCK time (endDate = now + minutes*unit), so a
// lock or sleep never pauses it -- the anchor is a real system-clock instant,
// not a monotonic tick count. On finish() it snapshots the actual wall-clock
// length, plays the bowl, picks a random quote, and journals on continue (or on
// the 10-minute break auto-dismiss). The UI only ever reads ISessionState.
#include <windows.h>
#include <string>
#include <functional>
#include "Quotes.h"

enum class Phase { Idle, Running, Resting };

// The small read-only surface the UI renders from.
struct ISessionState {
    virtual ~ISessionState() = default;
    virtual Phase        phase() const = 0;
    virtual int          remainingSeconds() const = 0;
    virtual std::wstring intention() const = 0;      // trimmed for display
    virtual int          plannedMinutes() const = 0;
    virtual int          completedUnits() const = 0; // break eyebrow ("N MINUTES LATER")
    virtual const Quote& quote() const = 0;
};

// Drives idle -> running -> resting -> idle against the real wall clock.
class PageController : public ISessionState {
public:
    PageController();

    // ---- ISessionState (what the UI reads) ----
    Phase        phase() const override { return phase_; }
    int          remainingSeconds() const override;
    std::wstring intention() const override { return trimmed(intention_); }
    int          plannedMinutes() const override { return minutes_; }
    int          completedUnits() const override { return completedUnits_; }
    const Quote& quote() const override { return quote_; }

    // ---- inputs from the UI ----
    void setIntentionRaw(const std::wstring& s) { intention_ = s; }
    void setReflectionRaw(const std::wstring& s) { reflection_ = s; }
    void setMinutes(int m);
    int  minutes() const { return minutes_; }
    std::wstring reflection() const { return trimmed(reflection_); }
    bool canBegin() const { return phase_ == Phase::Idle && !intention().empty(); }

    // ---- transitions (mirror SessionManager) ----
    bool begin();               // idle + non-empty intention -> running (plays bowl)
    void endEarly();            // running -> finish()
    void continueFromBreak();   // resting -> idle (journals here)

    // Called on a steady low-frequency poll; fires finish/auto-dismiss when a
    // wall-clock deadline has passed (works across sleep, unlike naive ticks).
    void update();

    // Fired after phase_ changes (UI drives choreography off this).
    std::function<void(Phase)> onPhaseChanged;

    static std::wstring trimmed(const std::wstring& s);

private:
    void finish();              // running -> resting (snapshot, quote, bowl)
    void setPhase(Phase p);
    void startRestTimeout(unsigned long long nowMs);

    Phase        phase_ = Phase::Idle;
    std::wstring intention_;
    std::wstring reflection_;
    int          minutes_ = 25;              // 5..60, step 5, default 25
    int          completedUnits_ = 25;       // slider units of the finished session
    int          completedWallMinutes_ = 25; // real minutes of the finished session
    Quote        quote_;

    FILETIME              sessionStartFt_{};  // UTC instant the session began (journal)
    unsigned long long    startWallMs_ = 0;   // wall-clock ms at begin
    unsigned long long    endWallMs_ = 0;     // wall-clock ms deadline
    unsigned long long    breakDeadlineMs_ = 0; // break auto-dismiss (wall clock)

    // MINDFUL_SECONDS present -> the slider counts seconds, not minutes, so a
    // full cycle runs in seconds (labels still read "minutes", matching macOS).
    double secondsPerUnit_ = 60.0;
    // Dev-only: MINDFUL_BREAK_AUTODISMISS_SECONDS overrides the break timeout
    // (absolute seconds) so the auto-dismiss path is testable in seconds.
    int    breakOverrideSec_ = 0;
};
