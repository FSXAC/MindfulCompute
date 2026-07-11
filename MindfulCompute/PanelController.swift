import AppKit
import Combine
import SwiftUI

extension Notification.Name {
    /// Posted when the user clicks the dimmed area; the panel pulses.
    static let panelNudge = Notification.Name("MCPanelNudge")
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

    /// The dim sheet is a child window of the panel: the window server moves
    /// parent and child atomically during drags, so the panel-shaped hole in
    /// the sheet can never lag. The sheet is oversized far past the screens
    /// so its own movement is invisible (uniform black everywhere else), and
    /// it is built from solid-color layers, which cost no backing memory.
    private let dimSheet: NSWindow
    private let dimContent: DimSheetView
    private static let sheetMargin: CGFloat = 5000

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
        syncDimSheet()
        if dimSheet.parent == nil {
            dimSheet.alphaValue = 0
            panel.addChildWindow(dimSheet, ordered: .below)
        }
        panel.makeKeyAndOrderFront(nil)
        panel.orderFrontRegardless()
        NSAnimationContext.runAnimationGroup { context in
            context.duration = 2.0
            dimSheet.animator().alphaValue = 1
        }
    }

    private func hide() {
        panel.removeChildWindow(dimSheet)
        dimSheet.orderOut(nil)
        panel.orderOut(nil)
    }

    private func syncDimSheet() {
        let margin = Self.sheetMargin
        dimSheet.setFrame(panel.frame.insetBy(dx: -margin, dy: -margin), display: false)
        dimContent.layoutHole(size: panel.frame.size, margin: margin)
    }

    /// A click landed on the dim: pulse the panel and give it key focus.
    private func nudge() {
        panel.makeKeyAndOrderFront(nil)
        NotificationCenter.default.post(name: .panelNudge, object: nil)
    }
}

/// Solid-color layers forming a full dim with a rounded rectangular hole:
/// four strips around the hole plus four small corner pieces that carve
/// the panel's corner radius. Solid layers never rasterize, so the huge
/// sheet stays cheap.
private final class DimSheetView: NSView {
    var onClick: (() -> Void)?

    private static let dimAlpha: Float = 0.60
    private static let cornerRadius: CGFloat = 26

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        wantsLayer = true
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError() }

    override func mouseDown(with event: NSEvent) {
        onClick?()
    }

    func layoutHole(size holeSize: CGSize, margin: CGFloat) {
        guard let layer else { return }
        layer.sublayers?.forEach { $0.removeFromSuperlayer() }

        let dim = NSColor.black.cgColor
        let radius = Self.cornerRadius
        let w = holeSize.width
        let h = holeSize.height
        let total = CGSize(width: w + margin * 2, height: h + margin * 2)

        func strip(_ frame: CGRect) {
            let sublayer = CALayer()
            sublayer.backgroundColor = dim
            sublayer.opacity = Self.dimAlpha
            sublayer.frame = frame
            layer.addSublayer(sublayer)
        }

        strip(CGRect(x: 0, y: 0, width: total.width, height: margin))              // below
        strip(CGRect(x: 0, y: margin + h, width: total.width, height: margin))     // above
        strip(CGRect(x: 0, y: margin, width: margin, height: h))                   // left
        strip(CGRect(x: margin + w, y: margin, width: margin, height: h))          // right

        // Corner pieces: a radius-sized square minus the quarter-disc the
        // panel's rounded corner occupies. arcCenter is in local coords.
        func corner(at origin: CGPoint, arcCenter: CGPoint) {
            let shape = CAShapeLayer()
            let path = CGMutablePath()
            path.addRect(CGRect(x: 0, y: 0, width: radius, height: radius))
            path.addEllipse(in: CGRect(
                x: arcCenter.x - radius, y: arcCenter.y - radius,
                width: radius * 2, height: radius * 2
            ))
            shape.path = path
            shape.fillRule = .evenOdd
            shape.fillColor = dim
            shape.opacity = Self.dimAlpha
            shape.frame = CGRect(origin: origin, size: CGSize(width: radius, height: radius))
            layer.addSublayer(shape)
        }

        corner(at: CGPoint(x: margin, y: margin),
               arcCenter: CGPoint(x: radius, y: radius))                            // bottom-left
        corner(at: CGPoint(x: margin + w - radius, y: margin),
               arcCenter: CGPoint(x: 0, y: radius))                                 // bottom-right
        corner(at: CGPoint(x: margin, y: margin + h - radius),
               arcCenter: CGPoint(x: radius, y: 0))                                 // top-left
        corner(at: CGPoint(x: margin + w - radius, y: margin + h - radius),
               arcCenter: CGPoint(x: 0, y: 0))                                      // top-right
    }
}
