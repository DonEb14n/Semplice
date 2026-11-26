#pragma once

class QGuiApplication;

namespace semplice {

// Composition root: builds the object graph by hand (no globals except the logger), runs the
// event loop and returns the process exit code.
int runApplication(QGuiApplication& app);

}  // namespace semplice
