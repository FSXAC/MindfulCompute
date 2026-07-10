import SwiftUI

enum AppVersion {
    /// "1.1 (260710.1305)" — the build number is stamped at compile time,
    /// so this immediately shows whether a running copy is stale.
    static let display: String = {
        let info = Bundle.main.infoDictionary
        let version = info?["CFBundleShortVersionString"] as? String ?? "?"
        let build = info?["CFBundleVersion"] as? String ?? "?"
        return "\(version) (\(build))"
    }()
}

@main
struct MindfulComputeApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate

    var body: some Scene {
        MenuBarExtra {
            MenuContent()
                .environmentObject(SessionManager.shared)
        } label: {
            MenuBarLabel()
        }
    }
}

struct MenuBarLabel: View {
    @ObservedObject private var manager = SessionManager.shared

    var body: some View {
        if manager.phase == .running {
            Text(manager.remainingLabel)
                .monospacedDigit()
        } else {
            Image(systemName: "leaf")
        }
    }
}

struct MenuContent: View {
    @EnvironmentObject private var manager: SessionManager
    @StateObject private var loginItem = LoginItem()

    var body: some View {
        switch manager.phase {
        case .running:
            Text("“\(manager.intention)”")
            Text("\(manager.remainingLabel) remaining")
            Divider()
            Button("End session early") { manager.endEarly() }
        case .idle:
            Button("Bring intention panel to front") { manager.requestPanel() }
        case .resting:
            Button("Bring break panel to front") { manager.requestPanel() }
        }
        Divider()
        Button("Open journal") { Journal.open() }
        Toggle("Start at login", isOn: $loginItem.enabled)
        Divider()
        Text("MindfulCompute \(AppVersion.display)")
        Button("Quit MindfulCompute") { NSApp.terminate(nil) }
    }
}
