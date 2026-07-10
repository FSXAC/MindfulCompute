import SwiftUI

extension Color {
    /// Muted sage — the app's single accent.
    static let sage = Color(.displayP3, red: 0.45, green: 0.54, blue: 0.42)
}

struct PanelRoot: View {
    @EnvironmentObject private var manager: SessionManager

    var body: some View {
        ZStack {
            switch manager.phase {
            case .resting:
                BreakView().transition(.opacity)
            default:
                StartView().transition(.opacity)
            }
        }
        .frame(width: 400)
        .background(PanelBackground())
        .animation(.easeInOut(duration: 0.35), value: manager.phase == .resting)
    }
}

private struct PanelBackground: View {
    private var shape: RoundedRectangle {
        RoundedRectangle(cornerRadius: 26, style: .continuous)
    }

    var body: some View {
        ZStack {
            VisualEffectBackdrop()
            LinearGradient(
                colors: [Color.sage.opacity(0.08), Color.sage.opacity(0.02)],
                startPoint: .top,
                endPoint: .bottom
            )
        }
        .clipShape(shape)
        .overlay(shape.strokeBorder(.separator.opacity(0.5), lineWidth: 1))
    }
}

/// Behind-window vibrancy so the panel reads as native glass.
private struct VisualEffectBackdrop: NSViewRepresentable {
    func makeNSView(context: Context) -> NSVisualEffectView {
        let view = NSVisualEffectView()
        view.material = .popover
        view.blendingMode = .behindWindow
        view.state = .active
        return view
    }

    func updateNSView(_ nsView: NSVisualEffectView, context: Context) {}
}
