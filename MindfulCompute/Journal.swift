import AppKit
import Foundation

/// One finished session, as stored in sessions.json.
struct SessionRecord: Codable {
    let start: Date
    let plannedMinutes: Int
    let actualMinutes: Int
    let intention: String
    let reflection: String
}

/// Appends each finished session to a Markdown journal (for reading) and a
/// JSON file (for revisiting the data later), both in Application Support.
enum Journal {
    private static var baseURL: URL {
        let base = FileManager.default.urls(
            for: .applicationSupportDirectory, in: .userDomainMask
        )[0].appendingPathComponent("MindfulCompute", isDirectory: true)
        try? FileManager.default.createDirectory(at: base, withIntermediateDirectories: true)
        return base
    }

    static var fileURL: URL { baseURL.appendingPathComponent("journal.md") }
    static var dataURL: URL { baseURL.appendingPathComponent("sessions.json") }

    static func append(
        start: Date,
        plannedMinutes: Int,
        actualMinutes: Int,
        intention: String,
        reflection: String
    ) {
        appendRecord(SessionRecord(
            start: start,
            plannedMinutes: plannedMinutes,
            actualMinutes: actualMinutes,
            intention: intention,
            reflection: reflection
        ))

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

    private static func appendRecord(_ record: SessionRecord) {
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .iso8601
        var records = (try? Data(contentsOf: dataURL))
            .flatMap { try? decoder.decode([SessionRecord].self, from: $0) } ?? []
        records.append(record)

        let encoder = JSONEncoder()
        encoder.dateEncodingStrategy = .iso8601
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        if let data = try? encoder.encode(records) {
            try? data.write(to: dataURL, options: .atomic)
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
