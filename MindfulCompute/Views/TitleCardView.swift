import SwiftUI

/// Chapter-style full-screen card shown when a session begins: the intention
/// set between thin rules, with a slow breathing guide beneath it.
struct TitleCardView: View {
    let intention: String
    let minutes: Int

    var body: some View {
        // The intention and the breathing guide center together as one
        // group, so the guide sits just below the middle of the screen
        // rather than down by the Dock.
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
            BreathGuide()
                .padding(.top, 72)
            Spacer()
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
        VStack(spacing: 24) {
            ZStack {
                // Soft halo so the movement reads from across the room.
                Circle()
                    .fill(.white.opacity(0.22))
                    .frame(width: 72, height: 72)
                    .scaleEffect(inhaling ? 1.55 : 0.65)
                    .blur(radius: 14)
                Circle()
                    .strokeBorder(.white.opacity(0.9), lineWidth: 2.5)
                    .frame(width: 72, height: 72)
                    .scaleEffect(inhaling ? 1.4 : 0.6)
                Circle()
                    .fill(.white.opacity(0.9))
                    .frame(width: 10, height: 10)
                    .scaleEffect(inhaling ? 1.25 : 0.75)
            }
            .frame(width: 130, height: 130)
            Text(label)
                .font(.system(size: 15, weight: .medium))
                .tracking(2.5)
                .foregroundStyle(.white.opacity(0.85))
                .contentTransition(.opacity)
                .animation(.easeInOut(duration: 0.5), value: label)
        }
        .task {
            guard !reduceMotion else {
                label = "Take a slow breath"
                return
            }
            // One box-breathing cycle, matched to the 12-second card:
            // 4s in, 4s hold, 4s out.
            withAnimation(.easeInOut(duration: 4)) { inhaling = true }
            label = "Breathe in"
            try? await Task.sleep(for: .seconds(4))
            guard !Task.isCancelled else { return }
            label = "Hold"
            try? await Task.sleep(for: .seconds(4))
            guard !Task.isCancelled else { return }
            withAnimation(.easeInOut(duration: 4)) { inhaling = false }
            label = "Breathe out"
        }
    }
}
