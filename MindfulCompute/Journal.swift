import AppKit
import Foundation
import os

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
@MainActor
enum Journal {
    private static let logger = Logger(
        subsystem: "ca.muchen.MindfulCompute", category: "Journal"
    )

    private static let header = "# MindfulCompute Journal\n"

    /// Fixed-format output needs a fixed locale, else a user's 12/24-hour
    /// system override mangles it (Apple QA1480). Local time zone is kept —
    /// this is a human-readable journal.
    private static let entryDateFormatter: DateFormatter = {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = "yyyy-MM-dd HH:mm"
        return formatter
    }()

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

        var entry = "\n## \(entryDateFormatter.string(from: start)) — \(actualMinutes) min"
        if actualMinutes != plannedMinutes {
            entry += " (planned \(plannedMinutes))"
        }
        entry += "\n\n**Intention:** \(intention)\n"
        if !reflection.isEmpty {
            entry += "\n**Reflection:** \(reflection)\n"
        }

        let url = fileURL
        let existing: String
        if FileManager.default.fileExists(atPath: url.path) {
            do {
                existing = try String(contentsOf: url, encoding: .utf8)
            } catch {
                logger.error("journal.md exists but could not be read; skipping append to avoid clobbering it: \(error.localizedDescription, privacy: .public)")
                return
            }
        } else {
            existing = header
        }

        do {
            try (existing + entry).write(to: url, atomically: true, encoding: .utf8)
        } catch {
            logger.error("Could not write journal.md: \(error.localizedDescription, privacy: .public)")
        }
    }

    private static func appendRecord(_ record: SessionRecord) {
        var records: [SessionRecord] = []
        if FileManager.default.fileExists(atPath: dataURL.path) {
            do {
                let data = try Data(contentsOf: dataURL)
                let decoder = JSONDecoder()
                decoder.dateDecodingStrategy = .iso8601
                records = try decoder.decode([SessionRecord].self, from: data)
            } catch {
                // The file exists but won't decode: preserve it rather than
                // overwrite, then start fresh. Losing this one record beats
                // losing the whole history.
                let corruptURL = dataURL.appendingPathExtension("corrupt")
                try? FileManager.default.removeItem(at: corruptURL)
                do {
                    try FileManager.default.moveItem(at: dataURL, to: corruptURL)
                    logger.error("sessions.json could not be decoded; moved aside to sessions.json.corrupt: \(error.localizedDescription, privacy: .public)")
                    records = []
                } catch {
                    logger.error("sessions.json could not be decoded and could not be moved aside; leaving it untouched: \(error.localizedDescription, privacy: .public)")
                    return
                }
            }
        }
        records.append(record)

        let encoder = JSONEncoder()
        encoder.dateEncodingStrategy = .iso8601
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        do {
            let data = try encoder.encode(records)
            try data.write(to: dataURL, options: .atomic)
        } catch {
            logger.error("Could not write sessions.json: \(error.localizedDescription, privacy: .public)")
        }
    }

    static func open() {
        let url = fileURL
        ensureJournalFile()
        NSWorkspace.shared.open(url)
    }

    private static func ensureJournalFile() {
        let url = fileURL
        guard !FileManager.default.fileExists(atPath: url.path) else { return }
        do {
            try header.write(to: url, atomically: true, encoding: .utf8)
        } catch {
            logger.error("Could not create journal.md: \(error.localizedDescription, privacy: .public)")
        }
    }
}
