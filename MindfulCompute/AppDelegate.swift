import AppKit

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    private var panelController: PanelController?
    private var overlayController: OverlayController?

    func applicationDidFinishLaunching(_ notification: Notification) {
        let overlay = OverlayController(manager: .shared)
        overlayController = overlay
        let controller = PanelController(manager: .shared)
        panelController = controller
        // The panel's dim sheet doubles as the title card's backdrop; the
        // overlay tells the panel controller when the card is done with it.
        overlay.onSessionDimRelease = { [weak controller] in
            controller?.releaseSessionDim()
        }
        controller.show()

        // Test hook: MINDFUL_AUTOSTART="some intention" begins a session on
        // launch with the default duration (pair with MINDFUL_SECONDS=1).
        if let auto = ProcessInfo.processInfo.environment["MINDFUL_AUTOSTART"] {
            SessionManager.shared.intention = auto
            SessionManager.shared.begin()
        }

        // Surface the intention panel every time the Mac unlocks, unless a
        // session is already underway.
        DistributedNotificationCenter.default().addObserver(
            forName: Notification.Name("com.apple.screenIsUnlocked"),
            object: nil,
            queue: .main
        ) { _ in
            Task { @MainActor in
                if SessionManager.shared.phase != .running {
                    self.panelController?.show()
                }
            }
        }
    }
}
