import Combine
import Foundation

@MainActor
final class SessionManager: ObservableObject {
    enum Phase {
        case idle      // waiting for an intention
        case running   // session in progress, panel hidden
        case resting   // session over, break panel shown
    }

    static let shared = SessionManager()

    @Published var phase: Phase = .idle
    @Published var intention: String = ""
    @Published var minutes: Double = 25
    @Published var reflection: String = ""
    @Published var remaining: TimeInterval = 0
    @Published var quote: Quote = Quotes.random()
    /// How long the just-finished session ran, in slider units (minutes, or
    /// seconds under MINDFUL_SECONDS) — snapshotted at finish() so the break
    /// header doesn't drift while the user lingers on the break panel.
    private(set) var completedUnits: Int = 25
    /// Actual wall-clock length of the just-finished session, in minutes,
    /// snapshotted at finish() so the journal records the session — not the
    /// time the user then spent lingering on the break panel.
    private var completedWallMinutes: Int = 25

    /// Fired when the menu asks for the panel to be brought forward.
    let panelRequests = PassthroughSubject<Void, Never>()

    private var endDate: Date?
    private var sessionStart: Date?
    private var timer: Timer?
    /// Auto-dismisses the break panel after a while (see startRestTimeout).
    private var dismissTask: Task<Void, Never>?

    /// With MINDFUL_SECONDS=1 in the environment, the slider counts seconds
    /// instead of minutes — for trying the full cycle without waiting.
    private let secondsPerUnit: Double =
        ProcessInfo.processInfo.environment["MINDFUL_SECONDS"] != nil ? 1 : 60

    var trimmedIntention: String {
        intention.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    var remainingLabel: String {
        let total = max(0, Int(remaining.rounded()))
        return String(format: "%d:%02d", total / 60, total % 60)
    }

    func begin() {
        guard phase == .idle, !trimmedIntention.isEmpty else { return }
        dismissTask?.cancel()
        dismissTask = nil
        sessionStart = Date()
        endDate = Date().addingTimeInterval(minutes * secondsPerUnit)
        remaining = minutes * secondsPerUnit
        phase = .running
        SoundPlayer.shared.play(.bowl)

        timer?.invalidate()
        let timer = Timer(timeInterval: 1, repeats: true) { _ in
            Task { @MainActor in SessionManager.shared.tick() }
        }
        // Loose tolerance lets the OS coalesce the 1 Hz wakeups; .common mode
        // keeps the countdown firing while the menu-bar menu is open.
        timer.tolerance = 0.3
        RunLoop.main.add(timer, forMode: .common)
        self.timer = timer
    }

    func endEarly() {
        guard phase == .running else { return }
        finish()
    }

    func continueFromBreak() {
        guard phase == .resting else { return }
        dismissTask?.cancel()
        dismissTask = nil
        Journal.append(
            start: sessionStart ?? Date(),
            plannedMinutes: Int(minutes),
            actualMinutes: completedWallMinutes,
            intention: trimmedIntention,
            reflection: reflection.trimmingCharacters(in: .whitespacesAndNewlines)
        )
        intention = ""
        reflection = ""
        sessionStart = nil
        phase = .idle
    }

    func requestPanel() {
        panelRequests.send()
    }

    private func tick() {
        guard phase == .running, let endDate else { return }
        // Anchored to wall-clock time so the countdown stays honest across
        // sleep and screen locks.
        remaining = max(0, endDate.timeIntervalSinceNow)
        if remaining <= 0 { finish() }
    }

    private func finish() {
        timer?.invalidate()
        timer = nil
        remaining = 0
        if let start = sessionStart {
            completedUnits = max(1, Int((Date().timeIntervalSince(start) / secondsPerUnit).rounded()))
            completedWallMinutes = max(1, Int((Date().timeIntervalSince(start) / 60).rounded()))
        }
        quote = Quotes.random()
        phase = .resting
        SoundPlayer.shared.play(.bowl)
        startRestTimeout()
    }

    /// After the break has sat untouched for ten minutes, journal it as if the
    /// user had clicked Continue and cycle back to the start panel.
    private func startRestTimeout() {
        dismissTask?.cancel()
        let seconds = 10 * secondsPerUnit
        dismissTask = Task { [weak self] in
            // Task.sleep's continuous clock counts time the Mac spends asleep,
            // so a break that outlasted a lock or sleep is already over on wake.
            try? await Task.sleep(for: .seconds(seconds))
            guard let self, !Task.isCancelled, self.phase == .resting else { return }
            self.continueFromBreak()
        }
    }
}
