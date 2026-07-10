import AppKit
import Combine
import SwiftUI

/// Shared state for the full-screen overlay windows: the dimmer that focuses
/// attention on the panel, and the chapter-style title card at session start.
@MainActor
final class OverlayModel: ObservableObject {
    @Published var dimmed = false
    @Published var titleCard = false
    var intention = ""
    var minutes = 25
    var onSkip: (() -> Void)?
}

/// One click-through window per screen. The dimmer fades in whenever the
/// intention or break panel is up, and only leaves once a session starts —
/// after the title card has faded in and out.
@MainActor
final class OverlayController {
    private let manager: SessionManager
    private let model = OverlayModel()
    private var windows: [NSWindow] = []
    private var mainWindow: NSWindow?
    private var sequenceTask: Task<Void, Never>?
    private var cancellables = Set<AnyCancellable>()

    init(manager: SessionManager) {
        self.manager = manager
        model.onSkip = { [weak self] in self?.endTitleCard() }

        manager.$phase
            .removeDuplicates()
            .receive(on: RunLoop.main)
            .sink { [weak self] phase in
                switch phase {
                case .idle, .resting: self?.showDim()
                case .running: self?.runTitleSequence()
                }
            }
            .store(in: &cancellables)
    }

    private func showDim() {
        sequenceTask?.cancel()
        if model.titleCard {
            model.titleCard = false
            mainWindow?.ignoresMouseEvents = true
        }
        ensureWindows()
        for window in windows { window.orderFrontRegardless() }
        model.dimmed = true
    }

    private func runTitleSequence() {
        model.intention = manager.trimmedIntention
        model.minutes = Int(manager.minutes)
        ensureWindows()
        for window in windows { window.orderFrontRegardless() }
        model.dimmed = true
        model.titleCard = true
        mainWindow?.ignoresMouseEvents = false

        let reduceMotion = NSWorkspace.shared.accessibilityDisplayShouldReduceMotion
        let cardSeconds: Double = reduceMotion ? 5 : 10.5
        sequenceTask = Task { [weak self] in
            try? await Task.sleep(for: .seconds(cardSeconds))
            guard !Task.isCancelled else { return }
            self?.endTitleCard()
        }
    }

    private func endTitleCard() {
        sequenceTask?.cancel()
        guard manager.phase == .running else { return }
        model.titleCard = false
        mainWindow?.ignoresMouseEvents = true
        sequenceTask = Task { [weak self] in
            try? await Task.sleep(for: .seconds(1.0))   // card fades out
            guard let self, !Task.isCancelled, self.manager.phase == .running else { return }
            self.model.dimmed = false
            try? await Task.sleep(for: .seconds(2.2))   // dim fades out
            guard !Task.isCancelled, self.manager.phase == .running else { return }
            for window in self.windows { window.orderOut(nil) }
        }
    }

    private func ensureWindows() {
        let screens = NSScreen.screens
        guard windows.count != screens.count else {
            for (window, screen) in zip(windows, screens) {
                window.setFrame(screen.frame, display: true)
            }
            return
        }

        for window in windows { window.orderOut(nil) }
        windows = []
        mainWindow = nil

        for (index, screen) in screens.enumerated() {
            let isMain = index == 0
            let window = NSWindow(
                contentRect: screen.frame,
                styleMask: [.borderless],
                backing: .buffered,
                defer: false
            )
            // Above the Dock so the whole desktop dims, but below the menu
            // bar so the countdown stays visible.
            window.level = NSWindow.Level(rawValue: Int(CGWindowLevelForKey(.dockWindow)) + 1)
            window.backgroundColor = .clear
            window.isOpaque = false
            window.hasShadow = false
            window.ignoresMouseEvents = true
            window.isReleasedWhenClosed = false
            window.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .stationary]
            window.contentView = NSHostingView(
                rootView: OverlayView(model: model, isMain: isMain)
            )
            window.setFrame(screen.frame, display: true)
            windows.append(window)
            if isMain { mainWindow = window }
        }
    }
}

struct OverlayView: View {
    @ObservedObject var model: OverlayModel
    let isMain: Bool

    // Values look lighter on screen than they read here — the overlay
    // composites against already-bright content — so they are set by eye.
    private var dimOpacity: Double {
        if model.titleCard { return 0.82 }
        return model.dimmed ? 0.60 : 0
    }

    var body: some View {
        ZStack {
            Color.black.opacity(dimOpacity)
                .animation(.easeInOut(duration: model.titleCard ? 1.2 : 2.0), value: dimOpacity)
            if isMain, model.titleCard {
                TitleCardView(intention: model.intention, minutes: model.minutes)
                    .onTapGesture { model.onSkip?() }
            }
        }
        .animation(.easeInOut(duration: 1.2), value: model.titleCard)
        .ignoresSafeArea()
    }
}
