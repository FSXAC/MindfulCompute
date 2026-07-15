#pragma once
// Always-on lightweight diagnostics log.
//
// Production logging for the Windows port: one timestamped file per run under
// %APPDATA%\MindfulCompute\logs\, pruned to the most recent handful of runs on
// startup. INFO by default; MINDFUL_LOG_VERBOSE=1 adds DEBUG lines. The file is
// buffered and only flushed on important events (and always by the crash
// handler, see CrashDump.h), so idle/steady CPU stays ~0 -- there is no logging
// at all while the app sits idle.
//
// The historical automation channel is preserved: MINDFUL_SPIKE_LOG=<path>
// overrides the destination with an exact file path (the agent harness relies
// on this). Log::write keeps its original name and signature so every existing
// call site is unchanged; it emits at INFO level.
#include <string>

namespace Log {

// Set up file logging (call once at startup, before anything else logs).
void init();

// INFO-level line (printf-style, wide). Original name kept for compatibility.
void write(const wchar_t* fmt, ...);

// DEBUG-level line: only emitted when MINDFUL_LOG_VERBOSE=1.
void debug(const wchar_t* fmt, ...);

// Force buffered output to disk now. Cheap; called on phase/journal events and,
// always, from the crash handler before the process dies.
void flush();

// Directory holding the run logs (for the tray "Open Logs Folder" item). Empty
// only if %APPDATA% could not be resolved.
const std::wstring& dir();

// Full path of this run's log file (the crash handler sits the .dmp beside it
// with the same timestamped stem). Empty only if no log file could be opened.
const std::wstring& file();

} // namespace Log
