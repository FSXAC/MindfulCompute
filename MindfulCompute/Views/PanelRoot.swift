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

/// Opaque on purpose: the panel always floats over the dimmer, and an
/// opaque surface neither picks up the gloom (washed-out glass) nor needs
/// a cutout in the dim that would have to chase the panel during drags.
private struct PanelBackground: View {
    private var shape: RoundedRectangle {
        RoundedRectangle(cornerRadius: 26, style: .continuous)
    }

    var body: some View {
        ZStack {
            shape.fill(Color(nsColor: .windowBackgroundColor))
            LinearGradient(
                colors: [Color.sage.opacity(0.10), Color.sage.opacity(0.03)],
                startPoint: .top,
                endPoint: .bottom
            )
            .clipShape(shape)
        }
        .overlay(shape.strokeBorder(.separator.opacity(0.6), lineWidth: 1))
    }
}
