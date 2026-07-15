#pragma once
// Portable core re-expressed from MindfulCompute/Journal.swift.
//
// Appends each finished session to a human-readable Markdown journal and a
// structured JSON file, both under %APPDATA%\MindfulCompute\ (roaming). The
// corruption-safety behaviour is matched exactly:
//   * sessions.json exists but won't parse -> move aside to sessions.json.corrupt
//     and start fresh, rather than clobbering the whole history.
//   * journal.md exists but can't be read -> skip the append, never overwrite.
// All output is UTF-8 (no BOM). Dates in journal.md use a fixed invariant format
// in LOCAL time (the human journal); sessions.json uses ISO-8601 in UTC ('Z'),
// matching Swift's JSONEncoder .iso8601 strategy.
#include <windows.h>
#include <string>

namespace Journal {

// Append a finished session. startUtc is the wall-clock instant the session
// began (a UTC FILETIME). plannedMinutes is the slider value; actualMinutes is
// the real wall-clock length in minutes. intention/reflection are already
// trimmed by the caller (reflection may be empty).
void append(const FILETIME& startUtc, int plannedMinutes, int actualMinutes,
            const std::wstring& intention, const std::wstring& reflection);

// "Open journal": open journal.md if it exists, else open the folder.
void open();

// %APPDATA%\MindfulCompute (created on demand). Empty on failure.
std::wstring baseDir();

} // namespace Journal
