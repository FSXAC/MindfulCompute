import AppKit

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    private var panelController: PanelController?

    func applicationDidFinishLaunching(_ notification: Notification) {
        let controller = PanelController(manager: .shared)
        panelController = controller
        controller.show()

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
