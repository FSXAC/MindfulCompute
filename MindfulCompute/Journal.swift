import AppKit
import Foundation

/// Appends each finished session to a Markdown journal in Application Support.
enum Journal {
    static var fileURL: URL {
        let base = FileManager.default.urls(
            for: .applicationSupportDirectory, in: .userDomainMask
        )[0].appendingPathComponent("MindfulCompute", isDirectory: true)
        try? FileManager.default.createDirectory(at: base, withIntermediateDirectories: true)
        return base.appendingPathComponent("journal.md")
    }

    static func append(
        start: Date,
        plannedMinutes: Int,
        actualMinutes: Int,
        intention: String,
        reflection: String
    ) {
        let formatter = DateFormatter()
        formatter.dateFormat = "yyyy-MM-dd HH:mm"

        var entry = "\n## \(formatter.string(from: start)) — \(actualMinutes) min"
        if actualMinutes != plannedMinutes {
            entry += " (planned \(plannedMinutes))"
        }
        entry += "\n\n**Intention:** \(intention)\n"
        if !reflection.isEmpty {
            entry += "\n**Reflection:** \(reflection)\n"
        }

        let url = fileURL
        if !FileManager.default.fileExists(atPath: url.path) {
            try? "# MindfulCompute Journal\n".write(to: url, atomically: true, encoding: .utf8)
        }
        if let handle = try? FileHandle(forWritingTo: url) {
            defer { try? handle.close() }
            _ = try? handle.seekToEnd()
            try? handle.write(contentsOf: Data(entry.utf8))
        }
    }

    static func open() {
        let url = fileURL
        if !FileManager.default.fileExists(atPath: url.path) {
            try? "# MindfulCompute Journal\n".write(to: url, atomically: true, encoding: .utf8)
        }
        NSWorkspace.shared.open(url)
    }
}
