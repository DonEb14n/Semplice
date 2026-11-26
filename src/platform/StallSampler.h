#pragma once

#include <QString>

namespace semplice::platform {

// Diagnostic for "the window froze": a helper thread posts an event to the GUI thread now and then; when the event waits for
// more than ~100 ms, the helper asks guiStackNow() where the GUI thread is
// (call stack addresses as "module+offset", written to the log). Only with SEMPLICE_STALL_TRACE=1: stopping the GUI thread to read its stack (and loading the symbols the first time) is itself a stall of up to a second.
void startStallSampler(int thresholdMs = 120);
// Where is the GUI thread right now? (empty when the trace is off) Called by the watchdog thread while an event waits.
QString guiStackNow();

}  // namespace semplice::platform
