import SwiftUI

struct StartView: View {
    @EnvironmentObject private var manager: SessionManager
    @FocusState private var intentionFocused: Bool

    private var greeting: String {
        switch Calendar.current.component(.hour, from: Date()) {
        case 5..<12: "Good morning"
        case 12..<17: "Good afternoon"
        default: "Good evening"
        }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 24) {
            VStack(alignment: .leading, spacing: 8) {
                Text(greeting)
                    .font(.footnote)
                    .foregroundStyle(.secondary)
                Text("What are you here to do?")
                    .font(.system(size: 23, weight: .medium, design: .serif))
            }

            TextField("One task, in a few words", text: $manager.intention)
                .textFieldStyle(.plain)
                .font(.system(size: 15))
                .padding(.horizontal, 14)
                .padding(.vertical, 11)
                .background(
                    RoundedRectangle(cornerRadius: 11, style: .continuous)
                        .fill(.quinary)
                )
                .overlay(
                    RoundedRectangle(cornerRadius: 11, style: .continuous)
                        .strokeBorder(
                            intentionFocused ? Color.sage.opacity(0.6) : Color.clear,
                            lineWidth: 1
                        )
                )
                .focused($intentionFocused)
                .onSubmit { manager.begin() }

            VStack(alignment: .leading, spacing: 10) {
                HStack(spacing: 5) {
                    Text("For")
                    Text("\(Int(manager.minutes)) minutes")
                        .fontWeight(.semibold)
                        .monospacedDigit()
                        .contentTransition(.numericText())
                        .animation(.snappy, value: manager.minutes)
                }
                .font(.callout)
                Slider(value: $manager.minutes, in: 5...60, step: 5)
                    .tint(.sage)
                HStack {
                    Text("5 min")
                    Spacer()
                    Text("1 hour")
                }
                .font(.caption2)
                .foregroundStyle(.tertiary)
            }

            Button {
                manager.begin()
            } label: {
                Text("Begin")
                    .frame(maxWidth: .infinity)
            }
            .controlSize(.large)
            .buttonStyle(.borderedProminent)
            .tint(.sage)
            .keyboardShortcut(.defaultAction)
            .disabled(manager.trimmedIntention.isEmpty)

            Text("v\(AppVersion.display)")
                .font(.caption2)
                .foregroundStyle(.quaternary)
                .frame(maxWidth: .infinity, alignment: .trailing)
        }
        .padding(28)
        .padding(.bottom, -12)
        .onAppear {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) {
                intentionFocused = true
            }
        }
    }
}
