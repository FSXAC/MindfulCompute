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

    /// Fired when the menu asks for the panel to be brought forward.
    let panelRequests = PassthroughSubject<Void, Never>()

    private var endDate: Date?
    private var sessionStart: Date?
    private var timer: Timer?

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
        sessionStart = Date()
        endDate = Date().addingTimeInterval(minutes * secondsPerUnit)
        remaining = minutes * secondsPerUnit
        phase = .running
        SoundPlayer.shared.play(.bowl)

        timer?.invalidate()
        timer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { _ in
            Task { @MainActor in SessionManager.shared.tick() }
        }
    }

    func endEarly() {
        guard phase == .running else { return }
        finish()
    }

    func continueFromBreak() {
        guard phase == .resting else { return }
        Journal.append(
            start: sessionStart ?? Date(),
            plannedMinutes: Int(minutes),
            actualMinutes: actualMinutes,
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

    private var actualMinutes: Int {
        guard let start = sessionStart else { return Int(minutes) }
        return max(1, Int((Date().timeIntervalSince(start) / 60).rounded()))
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
        }
        quote = Quotes.random()
        phase = .resting
        SoundPlayer.shared.play(.bowl)
    }
}
