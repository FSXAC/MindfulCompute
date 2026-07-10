import SwiftUI

struct BreakView: View {
    @EnvironmentObject private var manager: SessionManager

    var body: some View {
        VStack(spacing: 24) {
            VStack(spacing: 14) {
                BreathingGlyph()
                Text("Time to step away")
                    .font(.system(size: 23, weight: .medium, design: .serif))
                if !manager.trimmedIntention.isEmpty {
                    Text("You set out to: “\(manager.trimmedIntention)”")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                        .multilineTextAlignment(.center)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }

            VStack(spacing: 8) {
                Text(manager.quote.text)
                    .font(.system(size: 15, design: .serif))
                    .italic()
                    .multilineTextAlignment(.center)
                    .lineSpacing(3)
                    .foregroundStyle(.primary.opacity(0.85))
                    .fixedSize(horizontal: false, vertical: true)
                if let author = manager.quote.author {
                    Text("— \(author)")
                        .font(.caption)
                        .foregroundStyle(.tertiary)
                }
            }
            .padding(.horizontal, 8)

            VStack(alignment: .leading, spacing: 8) {
                Text("Before you go — how did it go?")
                    .font(.callout)
                    .foregroundStyle(.secondary)
                TextField("A sentence is enough", text: $manager.reflection)
                    .textFieldStyle(.plain)
                    .font(.system(size: 15))
                    .padding(.horizontal, 14)
                    .padding(.vertical, 11)
                    .background(
                        RoundedRectangle(cornerRadius: 11, style: .continuous)
                            .fill(.quinary)
                    )
                    .onSubmit { manager.continueFromBreak() }
            }
            .frame(maxWidth: .infinity, alignment: .leading)

            Button {
                manager.continueFromBreak()
            } label: {
                Text("Continue")
                    .frame(maxWidth: .infinity)
            }
            .controlSize(.large)
            .buttonStyle(.borderedProminent)
            .tint(.sage)
            .keyboardShortcut(.defaultAction)
        }
        .padding(28)
    }
}

/// A small dot that swells and settles at a slow breathing pace.
struct BreathingGlyph: View {
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @State private var inhale = false

    var body: some View {
        Circle()
            .fill(Color.sage.gradient)
            .frame(width: 14, height: 14)
            .scaleEffect(inhale ? 1.3 : 0.8)
            .opacity(inhale ? 0.95 : 0.55)
            .shadow(color: .sage.opacity(0.5), radius: inhale ? 10 : 4)
            .frame(height: 24)
            .onAppear {
                guard !reduceMotion else { return }
                withAnimation(.easeInOut(duration: 4).repeatForever(autoreverses: true)) {
                    inhale = true
                }
            }
    }
}
