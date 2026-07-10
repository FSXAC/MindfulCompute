import SwiftUI

/// Chapter-style full-screen card shown when a session begins: the intention
/// set between thin rules, with a slow breathing guide at the bottom.
struct TitleCardView: View {
    let intention: String
    let minutes: Int

    var body: some View {
        VStack(spacing: 0) {
            Spacer()
            VStack(spacing: 36) {
                rule
                VStack(spacing: 18) {
                    Text("THE NEXT \(minutes) MINUTES")
                        .font(.system(size: 13, weight: .semibold))
                        .tracking(5)
                        .foregroundStyle(.white.opacity(0.55))
                    Text(intention)
                        .font(.system(size: 42, weight: .medium, design: .serif))
                        .foregroundStyle(.white.opacity(0.95))
                        .multilineTextAlignment(.center)
                        .frame(maxWidth: 720)
                        .fixedSize(horizontal: false, vertical: true)
                }
                rule
            }
            .padding(.horizontal, 60)
            Spacer()
            BreathGuide()
                .padding(.bottom, 90)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .contentShape(Rectangle())
    }

    private var rule: some View {
        Rectangle()
            .fill(.white.opacity(0.28))
            .frame(width: 340, height: 1)
    }
}

/// A ring that swells and settles at a slow breathing pace, with a label
/// following the cycle.
struct BreathGuide: View {
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @State private var inhaling = false
    @State private var label = "Breathe in"

    var body: some View {
        VStack(spacing: 20) {
            ZStack {
                Circle()
                    .strokeBorder(.white.opacity(0.6), lineWidth: 1.5)
                    .frame(width: 52, height: 52)
                    .scaleEffect(inhaling ? 1.35 : 0.72)
                Circle()
                    .fill(.white.opacity(0.85))
                    .frame(width: 8, height: 8)
            }
            .frame(width: 80, height: 80)
            Text(label)
                .font(.system(size: 13, weight: .medium))
                .tracking(2)
                .foregroundStyle(.white.opacity(0.6))
        }
        .task {
            guard !reduceMotion else {
                label = "Take a slow breath"
                return
            }
            while !Task.isCancelled {
                withAnimation(.easeInOut(duration: 4)) { inhaling = true }
                label = "Breathe in"
                try? await Task.sleep(for: .seconds(4))
                guard !Task.isCancelled else { return }
                withAnimation(.easeInOut(duration: 4)) { inhaling = false }
                label = "Breathe out"
                try? await Task.sleep(for: .seconds(4))
            }
        }
    }
}
