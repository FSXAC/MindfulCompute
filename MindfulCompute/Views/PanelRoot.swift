import SwiftUI

extension Color {
    /// Muted sage — the accent of setting out (start panel, morning-cool).
    static let sage = Color(.displayP3, red: 0.45, green: 0.54, blue: 0.42)
    /// Warm ember — the accent of winding down (break panel, lamplight-warm),
    /// so the two pages read differently at a glance.
    static let ember = Color(.displayP3, red: 0.72, green: 0.55, blue: 0.36)
}

struct PanelRoot: View {
    @EnvironmentObject private var manager: SessionManager
    @State private var nudged = false
    /// Which page the panel shows. Deliberately decoupled from the phase:
    /// at session start the start page must persist while the panel fades
    /// out, and the switch to the break page happens invisibly once the
    /// panel is hidden — so the panel can never reappear showing a stale
    /// start page for a frame.
    @State private var showBreak = false

    var body: some View {
        ZStack {
            if showBreak {
                BreakView().transition(.opacity)
            } else {
                StartView().transition(.opacity)
            }
        }
        .frame(width: 400)
        .background(PanelBackground(accent: showBreak ? .ember : .sage))
        .scaleEffect(nudged ? 1.03 : 1)
        .onAppear { showBreak = manager.phase == .resting }
        .onReceive(NotificationCenter.default.publisher(for: .panelDidHide)) { _ in
            // Panel is hidden mid-session; pre-switch to the break page
            // without animation so it's ready when the session ends.
            var snap = Transaction()
            snap.disablesAnimations = true
            withTransaction(snap) { showBreak = true }
        }
        .onChange(of: manager.phase) { _, phase in
            switch phase {
            case .idle:
                // Continue after a break: visible crossfade back to start.
                withAnimation(.easeInOut(duration: 0.35)) { showBreak = false }
            case .resting where !showBreak:
                // Session ended before the hide finished (sub-second race,
                // e.g. ended early instantly): snap, the panel is dim anyway.
                var snap = Transaction()
                snap.disablesAnimations = true
                withTransaction(snap) { showBreak = true }
            default:
                break   // .running keeps the start page while fading out
            }
        }
        .onReceive(NotificationCenter.default.publisher(for: .panelNudge)) { _ in
            withAnimation(.spring(duration: 0.22)) { nudged = true }
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.24) {
                withAnimation(.spring(duration: 0.45)) { nudged = false }
            }
        }
    }
}

private struct PanelBackground: View {
    /// Sage for the start page, ember for the break page — the wash shifts
    /// warm so the break panel can't be mistaken for the start panel.
    let accent: Color

    // Circular (not continuous) corners, matching the corner pieces of the
    // dim sheet's hole exactly — see DimSheetView.
    private var shape: RoundedRectangle {
        RoundedRectangle(cornerRadius: 26, style: .circular)
    }

    var body: some View {
        ZStack {
            VisualEffectBackdrop()
            LinearGradient(
                colors: [accent.opacity(0.10), accent.opacity(0.03)],
                startPoint: .top,
                endPoint: .bottom
            )
        }
        .clipShape(shape)
        .overlay(shape.strokeBorder(.separator.opacity(0.5), lineWidth: 1))
    }
}

/// Behind-window vibrancy so the panel reads as native glass. The dim
/// sheet keeps a hole under the panel, so the glass samples the bright
/// desktop, never the dim.
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
