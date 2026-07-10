import Combine
import ServiceManagement

/// Wraps SMAppService so the menu can offer a "Start at login" toggle —
/// the app can only greet an unlock if it is already running.
@MainActor
final class LoginItem: ObservableObject {
    @Published var enabled: Bool {
        didSet {
            guard oldValue != enabled else { return }
            do {
                if enabled {
                    try SMAppService.mainApp.register()
                } else {
                    try SMAppService.mainApp.unregister()
                }
            } catch {
                enabled = SMAppService.mainApp.status == .enabled
            }
        }
    }

    init() {
        enabled = SMAppService.mainApp.status == .enabled
    }
}
