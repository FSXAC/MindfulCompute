import AppKit
import Combine
import SwiftUI

extension Notification.Name {
    /// Posted when the user clicks the dimmed area; the panel pulses.
    static let panelNudge = Notification.Name("MCPanelNudge")
    /// Posted once the panel has fully faded out at session start, so the
    /// (hidden) panel content can switch to the break page ahead of time.
    static let panelDidHide = Notification.Name("MCPanelDidHide")
}

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
    private var transitionTask: Task<Void, Never>?

    /// The dim sheet is a child window of the panel: the window server moves
    /// parent and child atomically during drags, so the panel-shaped hole in
    /// the sheet can never lag. The sheet is oversized far past the screens
    /// so its own movement is invisible (uniform black everywhere else), and
    /// it is built from solid-color layers, which cost no backing memory.
    ///
    /// It is also the app's ONE dimmer: when a session begins it deepens,
    /// its hole closes over the fading panel, and it is detached to outlive
    /// the panel as the title card's backdrop — so the screen never blinks
    /// back to full brightness between the panel and the card.
    private let dimSheet: NSWindow
    private let dimContent: DimSheetView
    /// Keeps the sheet under the compositor's 16384-pixel surface limit on
    /// 2x displays (panel + 2 * margin must stay below 8192 points) while
    /// still covering any realistic drag range across displays.
    private static let sheetMargin: CGFloat = 3500

    init(manager: SessionManager) {
        self.manager = manager

        let panel = FloatingPanel(
            contentRect: NSRect(x: 0, y: 0, width: 400, height: 420),
            styleMask: [.borderless, .nonactivatingPanel],
            backing: .buffered,
            defer: false
        )
        // Above the dim sheet, which sits just above the Dock.
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

        let content = DimSheetView()
        dimContent = content
        let sheet = NSWindow(
            contentRect: .zero,
            styleMask: [.borderless],
            backing: .buffered,
            defer: false
        )
        sheet.level = panel.level
        sheet.backgroundColor = .clear
        sheet.isOpaque = false
        sheet.hasShadow = false
        sheet.ignoresMouseEvents = false   // blocking: clicks stop here
        sheet.isReleasedWhenClosed = false
        sheet.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]
        sheet.contentView = content
        dimSheet = sheet

        content.onClick = { [weak self] in self?.nudge() }

        // Panel height changes when the view switches (start <-> break);
        // resizes are app-driven, so a synchronous re-sync here is enough.
        NotificationCenter.default.addObserver(
            forName: NSWindow.didResizeNotification, object: panel, queue: nil
        ) { [weak self] _ in
            MainActor.assumeIsolated { self?.syncDimSheet() }
        }

        manager.$phase
            .removeDuplicates()
            .receive(on: RunLoop.main)
            .sink { [weak self] phase in
                switch phase {
                case .running: self?.beginSessionTransition()
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
        transitionTask?.cancel()
        transitionTask = nil
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
        syncDimSheet()
        dimSheet.ignoresMouseEvents = false

        // If the sheet is on screen without a parent, it is still carrying
        // the deep session dim (the session just ended, possibly while the
        // title card or its fade-out was underway).
        let sheetCarriesSessionDim = dimSheet.isVisible && dimSheet.parent == nil
        if dimSheet.parent == nil {
            if !sheetCarriesSessionDim { dimSheet.alphaValue = 0 }
            panel.addChildWindow(dimSheet, ordered: .below)
        }

        if sheetCarriesSessionDim {
            // Hand the dim back: the panel fades in while the hole reopens
            // and the dim eases from title-card depth to its resting level.
            panel.alphaValue = 0
            panel.makeKeyAndOrderFront(nil)
            panel.orderFrontRegardless()
            dimContent.setDim(level: DimSheetView.restLevel, holeClosed: false, duration: 0.7)
            NSAnimationContext.runAnimationGroup { context in
                context.duration = 0.7
                panel.animator().alphaValue = 1
                dimSheet.animator().alphaValue = 1   // may be mid release-fade
            }
        } else {
            panel.alphaValue = 1
            dimContent.setDim(level: DimSheetView.restLevel, holeClosed: false, duration: 0)
            panel.makeKeyAndOrderFront(nil)
            panel.orderFrontRegardless()
            NSAnimationContext.runAnimationGroup { context in
                context.duration = 2.0
                dimSheet.animator().alphaValue = 1
            }
        }
    }

    /// Session started: the panel dissolves while the dim deepens and its
    /// hole closes, then the sheet is detached to serve as the title card's
    /// backdrop. The screen darkens monotonically — no bright gap.
    private func beginSessionTransition() {
        transitionTask?.cancel()
        dimSheet.ignoresMouseEvents = true   // stop gating clicks once committed
        syncDimSheet()
        dimContent.setDim(level: DimSheetView.deepLevel, holeClosed: true, duration: 0.8)
        NSAnimationContext.runAnimationGroup { context in
            context.duration = 0.8
            panel.animator().alphaValue = 0
        }
        transitionTask = Task { [weak self] in
            try? await Task.sleep(for: .seconds(0.85))
            guard let self, !Task.isCancelled else { return }
            // Detach so the sheet outlives the panel under the title card.
            self.panel.removeChildWindow(self.dimSheet)
            self.panel.orderOut(nil)
            self.panel.alphaValue = 1
            NotificationCenter.default.post(name: .panelDidHide, object: nil)
        }
    }

    /// The title card has finished: fade the session dim away and release
    /// the sheet. Called by the overlay controller; a no-op if the session
    /// already ended (show() reclaimed the sheet).
    func releaseSessionDim() {
        guard manager.phase == .running, dimSheet.parent == nil else { return }
        transitionTask?.cancel()
        NSAnimationContext.runAnimationGroup { context in
            context.duration = 2.0
            dimSheet.animator().alphaValue = 0
        }
        transitionTask = Task { [weak self] in
            try? await Task.sleep(for: .seconds(2.1))
            guard let self, !Task.isCancelled else { return }
            self.dimSheet.orderOut(nil)
        }
    }

    private func syncDimSheet() {
        let margin = Self.sheetMargin
        dimSheet.setFrame(panel.frame.insetBy(dx: -margin, dy: -margin), display: false)
        dimContent.layoutHole(size: panel.frame.size, margin: margin)
    }

    /// A click landed on the dim: pulse the panel and give it key focus.
    private func nudge() {
        guard manager.phase != .running else { return }
        panel.makeKeyAndOrderFront(nil)
        NotificationCenter.default.post(name: .panelNudge, object: nil)
    }
}

/// Solid-color layers forming a full dim with a rounded rectangular hole:
/// four strips around the hole, four corner pieces that carve the panel's
/// corner radius, and a hole cover that can fade in to close the hole
/// entirely (session-start transition). All pieces are opaque black inside
/// a container whose opacity is the dim level, so the level animates as a
/// single value. Solid layers never rasterize, so the huge sheet stays
/// cheap. Layers are reused across layout passes with implicit animations
/// disabled — the hole tracks panel resizes frame-for-frame with no
/// flashing edges.
private final class DimSheetView: NSView {
    var onClick: (() -> Void)?

    /// Dim while the panel waits for the user.
    static let restLevel: Float = 0.60
    /// Deeper dim behind the title card. (Reads lighter on screen than the
    /// number suggests — it composites against bright content.)
    static let deepLevel: Float = 0.82
    private static let cornerRadius: CGFloat = 26

    private let container = CALayer()
    private let strips = [CALayer(), CALayer(), CALayer(), CALayer()]
    private let corners = [CAShapeLayer(), CAShapeLayer(), CAShapeLayer(), CAShapeLayer()]
    private let holeCover = CAShapeLayer()

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        // Layer-hosting (custom layer assigned before wantsLayer), so AppKit
        // never repaints or clears the manually managed sublayers.
        layer = CALayer()
        wantsLayer = true

        let black = NSColor.black.cgColor
        for strip in strips {
            strip.backgroundColor = black
            container.addSublayer(strip)
        }
        for corner in corners {
            corner.fillColor = black
            corner.fillRule = .evenOdd
            container.addSublayer(corner)
        }
        holeCover.fillColor = black
        holeCover.opacity = 0
        container.addSublayer(holeCover)
        container.opacity = Self.restLevel
        layer?.addSublayer(container)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError() }

    override func mouseDown(with event: NSEvent) {
        onClick?()
    }

    /// Animate the dim level and whether the hole is covered. Duration 0
    /// applies instantly.
    func setDim(level: Float, holeClosed: Bool, duration: TimeInterval) {
        CATransaction.begin()
        if duration <= 0 {
            CATransaction.setDisableActions(true)
        } else {
            CATransaction.setAnimationDuration(duration)
            CATransaction.setAnimationTimingFunction(CAMediaTimingFunction(name: .easeInEaseOut))
        }
        container.opacity = level
        holeCover.opacity = holeClosed ? 1 : 0
        CATransaction.commit()
    }

    func layoutHole(size holeSize: CGSize, margin: CGFloat) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        defer { CATransaction.commit() }

        let radius = Self.cornerRadius
        let w = holeSize.width
        let h = holeSize.height
        let total = CGSize(width: w + margin * 2, height: h + margin * 2)
        container.frame = CGRect(origin: .zero, size: total)

        strips[0].frame = CGRect(x: 0, y: 0, width: total.width, height: margin)          // below
        strips[1].frame = CGRect(x: 0, y: margin + h, width: total.width, height: margin) // above
        strips[2].frame = CGRect(x: 0, y: margin, width: margin, height: h)               // left
        strips[3].frame = CGRect(x: margin + w, y: margin, width: margin, height: h)      // right

        // Corner pieces: a radius-sized square minus the quarter-disc the
        // panel's rounded corner occupies. arcCenter is in local coords.
        func corner(_ shape: CAShapeLayer, at origin: CGPoint, arcCenter: CGPoint) {
            let path = CGMutablePath()
            path.addRect(CGRect(x: 0, y: 0, width: radius, height: radius))
            path.addEllipse(in: CGRect(
                x: arcCenter.x - radius, y: arcCenter.y - radius,
                width: radius * 2, height: radius * 2
            ))
            shape.path = path
            shape.frame = CGRect(origin: origin, size: CGSize(width: radius, height: radius))
        }

        corner(corners[0], at: CGPoint(x: margin, y: margin),
               arcCenter: CGPoint(x: radius, y: radius))                                  // bottom-left
        corner(corners[1], at: CGPoint(x: margin + w - radius, y: margin),
               arcCenter: CGPoint(x: 0, y: radius))                                       // bottom-right
        corner(corners[2], at: CGPoint(x: margin, y: margin + h - radius),
               arcCenter: CGPoint(x: radius, y: 0))                                       // top-left
        corner(corners[3], at: CGPoint(x: margin + w - radius, y: margin + h - radius),
               arcCenter: CGPoint(x: 0, y: 0))                                            // top-right

        // Rounded rect exactly filling the hole, matching the panel shape.
        holeCover.frame = CGRect(x: margin, y: margin, width: w, height: h)
        holeCover.path = CGPath(
            roundedRect: CGRect(x: 0, y: 0, width: w, height: h),
            cornerWidth: radius, cornerHeight: radius, transform: nil
        )
    }
}
