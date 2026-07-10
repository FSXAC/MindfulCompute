import AppKit
import Combine
import SwiftUI

/// Shared state for the full-screen overlay windows: the dimmer that focuses
/// attention on the panel, and the chapter-style title card at session start.
@MainActor
final class OverlayModel: ObservableObject {
    @Published var dimmed = false
    @Published var titleCard = false
    /// Panel frame in global screen coordinates; the dim leaves this region
    /// undimmed so the panel's glass samples bright content.
    @Published var cutout: CGRect?
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
    private let panel: NSWindow
    private var windows: [NSWindow] = []
    private var mainWindow: NSWindow?
    private var sequenceTask: Task<Void, Never>?
    private var cancellables = Set<AnyCancellable>()

    init(manager: SessionManager, panel: NSWindow) {
        self.manager = manager
        self.panel = panel
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

        // Track the panel as it is dragged or resized so the hole in the
        // dim follows it.
        for name in [NSWindow.didMoveNotification, NSWindow.didResizeNotification] {
            NotificationCenter.default.addObserver(
                forName: name, object: panel, queue: .main
            ) { [weak self] _ in
                Task { @MainActor in self?.updateCutout() }
            }
        }
    }

    private func updateCutout() {
        model.cutout = manager.phase == .running ? nil : panel.frame
    }

    private func showDim() {
        sequenceTask?.cancel()
        if model.titleCard {
            model.titleCard = false
            mainWindow?.ignoresMouseEvents = true
        }
        ensureWindows()
        for window in windows { window.orderFrontRegardless() }
        updateCutout()
        model.dimmed = true
    }

    private func runTitleSequence() {
        model.intention = manager.trimmedIntention
        model.minutes = Int(manager.minutes)
        model.cutout = nil
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
                rootView: OverlayView(model: model, isMain: isMain, screenFrame: screen.frame)
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
    let screenFrame: CGRect

    // Values look lighter on screen than they read here — the overlay
    // composites against already-bright content — so they are set by eye.
    private var dimOpacity: Double {
        if model.titleCard { return 0.82 }
        return model.dimmed ? 0.60 : 0
    }

    /// The panel's frame converted from global (bottom-left origin) screen
    /// coordinates to this window's local top-left-origin SwiftUI space.
    private var hole: CGRect? {
        guard let cutout = model.cutout, cutout.intersects(screenFrame) else { return nil }
        return CGRect(
            x: cutout.minX - screenFrame.minX,
            y: screenFrame.maxY - cutout.maxY,
            width: cutout.width,
            height: cutout.height
        )
    }

    var body: some View {
        ZStack {
            DimShape(hole: hole)
                .fill(Color.black.opacity(dimOpacity), style: FillStyle(eoFill: true))
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

/// Full-screen rectangle with a rounded hole where the panel sits; drawn
/// with even-odd fill so the hole stays undimmed.
private struct DimShape: Shape {
    var hole: CGRect?

    func path(in rect: CGRect) -> Path {
        var path = Path()
        path.addRect(rect)
        if let hole {
            // Matches the panel's corner radius so no dim peeks past it.
            path.addRoundedRect(
                in: hole,
                cornerSize: CGSize(width: 26, height: 26),
                style: .continuous
            )
        }
        return path
    }
}
