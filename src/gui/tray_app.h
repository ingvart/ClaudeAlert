#pragma once

namespace cusage {

// Runs the desktop tray application: a polling worker thread plus an SDL tray
// icon whose menu shows live usage, with an on-demand Dear ImGui details window.
// Returns a process exit code.
int run_tray_app();

}  // namespace cusage
