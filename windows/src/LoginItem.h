#pragma once
// "Start at login", re-expressed from MindfulCompute/LoginItem.swift.
// Backed by HKCU\Software\Microsoft\Windows\CurrentVersion\Run, value
// "MindfulCompute" = the quoted absolute exe path. Failures are non-fatal;
// the menu simply re-reads the true state next time it is built.
namespace LoginItem {
// True if the Run value is present.
bool isEnabled();
// Write (enable) or delete (disable) the Run value. Returns success.
bool setEnabled(bool enable);
}
