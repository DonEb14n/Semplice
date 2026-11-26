#pragma once

#include <QFile>
#include <QMutex>
#include <QString>

namespace semplice {

enum class LogLevel { Debug, Info, Warning, Error };

// Rotating text log. Thread-safe. Files: semplice.log (current), semplice.1.log (newest old) ...
class Logger {
public:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // Creates `dir` if needed. `keepFiles` counts the current file too.
    bool init(const QString& dir, qint64 maxBytes = 2 * 1024 * 1024, int keepFiles = 5);
    void write(LogLevel level, const QString& message);
    QString filePath() const;

    // Process-wide logger used by the SLOG_* macros and by the Qt message handler.
    static void setGlobal(Logger* logger);
    static Logger* global();
    // Routes qDebug/qWarning/... into the global logger (and still to the previous handler).
    static void installQtMessageHandler();

private:
    void rotateLocked();
    QString numberedPath(int index) const;

    mutable QMutex m_mutex;
    QFile m_file;
    QString m_dir;
    qint64 m_maxBytes = 0;
    int m_keepFiles = 0;
};

void logMessage(LogLevel level, const QString& message);

}  // namespace semplice

#define SLOG_DEBUG(msg) ::semplice::logMessage(::semplice::LogLevel::Debug, (msg))
#define SLOG_INFO(msg) ::semplice::logMessage(::semplice::LogLevel::Info, (msg))
#define SLOG_WARN(msg) ::semplice::logMessage(::semplice::LogLevel::Warning, (msg))
#define SLOG_ERROR(msg) ::semplice::logMessage(::semplice::LogLevel::Error, (msg))

#include <QElapsedTimer>

namespace semplice {

// Logs a warning when the scope that owns it took longer than `limitMs` (a diagnostic for "the window froze").
class SlowScope {
public:
    explicit SlowScope(const char* name, int limitMs = 30) : m_name(name), m_limit(limitMs) { m_timer.start(); }
    ~SlowScope()
    {
        if (m_timer.elapsed() > m_limit)
            logMessage(LogLevel::Warning, QStringLiteral("slow: %1 took %2 ms").arg(QString::fromLatin1(m_name)).arg(m_timer.elapsed()));
    }

private:
    const char* m_name;
    int m_limit;
    QElapsedTimer m_timer;
};

}  // namespace semplice

#define SEMPLICE_SLOW_SCOPE(name) ::semplice::SlowScope semplice_slow_scope_(name)
