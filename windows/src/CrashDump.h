#pragma once
// Crash capture: turn an otherwise-silent crash into a self-diagnosing bug
// report. install() wires up SetUnhandledExceptionFilter plus the CRT
// abort/purecall/invalid-parameter handlers so every fatal path funnels through
// one place that:
//   1. logs the exception code + faulting address + module offset,
//   2. writes a minidump (.dmp) next to this run's log file,
//   3. flushes the log, then lets the process die.
//
// dbghelp.dll is LoadLibrary'd only at crash time, so the happy path carries no
// dependency on it. Call install() right after Log::init().
namespace CrashDump {
void install();
}
