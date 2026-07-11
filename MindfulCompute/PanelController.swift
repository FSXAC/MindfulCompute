import AppKit
import Combine
import SwiftUI

/// Borderless always-on-top panel that can still take keyboard input.
final class FloatingPanel: NSPanel {
    override var canBecomeKey: Bool { true }
}

@MainActor
final class PanelController {
    private let panel: FloatingPanel
    private let manager: SessionManager
    private var cancellables = Set<AnyCancellable>()
    private var hasPositioned = false

    init(manager: SessionManager) {
        self.manager = manager

        let panel = FloatingPanel(
            contentRect: NSRect(x: 0, y: 0, width: 400, height: 420),
            styleMask: [.borderless, .nonactivatingPanel],
            backing: .buffered,
            defer: false
        )
        // Above the dimmer overlay windows, which sit just above the Dock.
        panel.level = NSWindow.Level(rawValue: Int(CGWindowLevelForKey(.dockWindow)) + 2)
        panel.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]
        panel.isMovableByWindowBackground = true
        panel.isFloatingPanel = true
        panel.hidesOnDeactivate = false
        panel.becomesKeyOnlyIfNeeded = false
        panel.isOpaque = false
        panel.backgroundColor = .clear
        panel.hasShadow = true
        panel.isReleasedWhenClosed = false

        let host = NSHostingController(
            rootView: PanelRoot().environmentObject(manager)
        )
        panel.contentViewController = host
        self.panel = panel

        manager.$phase
            .removeDuplicates()
            .receive(on: RunLoop.main)
            .sink { [weak self] phase in
                switch phase {
                case .running: self?.hide()
                case .idle, .resting: self?.show()
                }
            }
            .store(in: &cancellables)

        manager.panelRequests
            .receive(on: RunLoop.main)
            .sink { [weak self] in self?.show() }
            .store(in: &cancellables)
    }

    func show() {
        if !hasPositioned, let screen = NSScreen.main {
            let frame = panel.frame
            let visible = screen.visibleFrame
            let origin = NSPoint(
                x: visible.midX - frame.width / 2,
                y: visible.midY - frame.height / 2 + visible.height * 0.08
            )
            panel.setFrameOrigin(origin)
            hasPositioned = true
        }
        panel.makeKeyAndOrderFront(nil)
        panel.orderFrontRegardless()
    }

    private func hide() {
        panel.orderOut(nil)
    }
}
