import AppKit
import Combine
import SwiftUI

/// Shared state for the title-card overlay shown at session start.
/// (All screen dimming — while the panel waits AND behind the title card —
/// is the panel's dim sheet; see PanelController. This overlay draws only
/// the card itself, so the two never hand off with a bright gap.)
@MainActor
final class OverlayModel: ObservableObject {
    /// Whether the title card is mounted; its visibility is driven by
    /// `cardOpacity` so fades are explicit rather than transition-dependent.
    @Published var titleCard = false
    @Published var cardOpacity: Double = 0
    var intention = ""
    var minutes = 25
    var onSkip: (() -> Void)?
}

/// One full-screen window (on the panel's screen) that renders the
/// session-start title card above the dim sheet.
@MainActor
final class OverlayController {
    private let manager: SessionManager
    private let model = OverlayModel()
    private var window: NSWindow?
    private var sequenceTask: Task<Void, Never>?
    private var cancellables = Set<AnyCancellable>()

    /// Called when the title card has faded and the session dim should
    /// fade away too. Wired to PanelController.releaseSessionDim().
    var onSessionDimRelease: (() -> Void)?

    init(manager: SessionManager) {
        self.manager = manager
        model.onSkip = { [weak self] in self?.endTitleCard() }

        manager.$phase
            .removeDuplicates()
            .receive(on: RunLoop.main)
            .sink { [weak self] phase in
                switch phase {
                case .idle, .resting: self?.dismissTitleOverlay()
                case .running: self?.runTitleSequence()
                }
            }
            .store(in: &cancellables)
    }

    private func runTitleSequence() {
        // A pending teardown from the previous session's card would otherwise
        // order this window out mid-card (fast Continue -> Begin, MINDFUL_SECONDS).
        sequenceTask?.cancel()
        model.intention = manager.trimmedIntention
        model.minutes = Int(manager.minutes)
        let window = ensureWindow()
        window.orderFrontRegardless()
        model.titleCard = true
        model.cardOpacity = 0
        window.ignoresMouseEvents = false

        let reduceMotion = NSWorkspace.shared.accessibilityDisplayShouldReduceMotion
        let cardSeconds: Double = reduceMotion ? 5 : 12
        sequenceTask = Task { [weak self] in
            // Let the mount at opacity 0 commit first, so the rise to 1
            // animates instead of coalescing into a single frame.
            try? await Task.sleep(for: .seconds(0.1))
            guard !Task.isCancelled else { return }
            self?.model.cardOpacity = 1
            try? await Task.sleep(for: .seconds(cardSeconds))
            guard !Task.isCancelled else { return }
            self?.endTitleCard()
        }
    }

    /// The card ran to its end while the session continues: fade it out and
    /// let the session dim fade with it.
    private func endTitleCard() {
        sequenceTask?.cancel()
        guard manager.phase == .running else { return }
        fadeOutCard(releaseDim: true)
    }

    /// A session stopped while the title card was up (e.g. ended early):
    /// fade the card out. The panel reclaims the dim itself here
    /// (PanelController.showDimFirst), so this must NOT release the session
    /// dim — doing so would double-fade the sheet.
    private func dismissTitleOverlay() {
        sequenceTask?.cancel()
        guard model.titleCard else { return }
        fadeOutCard(releaseDim: false)
    }

    /// Fade the card to transparent — kept mounted so `.animation(value:)` on
    /// cardOpacity actually renders the ~1s fade — then unmount it and hide
    /// the window. `releaseDim` fades the session dim away with the card; it
    /// implies the session is still running, so the teardown re-guards phase
    /// against a stop whose cancellation hasn't been delivered yet.
    private func fadeOutCard(releaseDim: Bool) {
        model.cardOpacity = 0
        window?.ignoresMouseEvents = true
        sequenceTask = Task { [weak self] in
            try? await Task.sleep(for: .seconds(1.1))   // card fades out
            guard let self, !Task.isCancelled else { return }
            if releaseDim { guard self.manager.phase == .running else { return } }
            self.model.titleCard = false
            if releaseDim { self.onSessionDimRelease?() }   // dim fades out
            self.window?.orderOut(nil)
        }
    }

    private func ensureWindow() -> NSWindow {
        // Follow the key window's screen, i.e. wherever the panel lives.
        let screen = NSScreen.main ?? NSScreen.screens[0]
        if let window {
            window.setFrame(screen.frame, display: true)
            return window
        }

        let window = NSWindow(
            contentRect: screen.frame,
            styleMask: [.borderless],
            backing: .buffered,
            defer: false
        )
        // Above the dim sheet (dock+2), below the menu bar, so the card
        // text sits on the dim while the countdown stays visible.
        window.level = NSWindow.Level(rawValue: Int(CGWindowLevelForKey(.dockWindow)) + 3)
        window.backgroundColor = .clear
        window.isOpaque = false
        window.hasShadow = false
        window.ignoresMouseEvents = true
        window.isReleasedWhenClosed = false
        window.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .stationary]
        window.contentView = NSHostingView(rootView: OverlayView(model: model))
        window.setFrame(screen.frame, display: true)
        self.window = window
        return window
    }
}

struct OverlayView: View {
    @ObservedObject var model: OverlayModel

    var body: some View {
        ZStack {
            if model.titleCard {
                TitleCardView(intention: model.intention, minutes: model.minutes)
                    .opacity(model.cardOpacity)
                    .animation(
                        .easeInOut(duration: model.cardOpacity > 0 ? 1.2 : 1.0),
                        value: model.cardOpacity
                    )
                    .onTapGesture { model.onSkip?() }
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .ignoresSafeArea()
    }
}
