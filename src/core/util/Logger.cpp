#include "core/util/Logger.h"

#include "core/storage/OwnFiles.h"

#include <QDateTime>
#include <QDir>
#include <QMutexLocker>

#include <atomic>

namespace semplice {

namespace {

std::atomic<Logger*> g_logger{nullptr};
QtMessageHandler g_previousHandler = nullptr;

const char* levelText(LogLevel level)
{
    switch (level) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO ";
    case LogLevel::Warning: return "WARN ";
    case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

void qtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    LogLevel level = LogLevel::Info;
    switch (type) {
    case QtDebugMsg: level = LogLevel::Debug; break;
    case QtInfoMsg: level = LogLevel::Info; break;
    case QtWarningMsg: level = LogLevel::Warning; break;
    case QtCriticalMsg:
    case QtFatalMsg: level = LogLevel::Error; break;
    }
    logMessage(level, message);
    if (g_previousHandler)
        g_previousHandler(type, context, message);
}

}  // namespace

Logger::~Logger()
{
    if (g_logger.load() == this)
        g_logger.store(nullptr);
}

bool Logger::init(const QString& dir, qint64 maxBytes, int keepFiles)
{
    QMutexLocker lock(&m_mutex);
    m_dir = dir;
    m_maxBytes = maxBytes;
    m_keepFiles = keepFiles < 1 ? 1 : keepFiles;
    if (!QDir().mkpath(dir))
        return false;
    if (m_file.isOpen())
        m_file.close();
    m_file.setFileName(numberedPath(0));
    return m_file.open(QIODevice::WriteOnly | QIODevice::Append);
}

QString Logger::numberedPath(int index) const
{
    const QString name = index == 0 ? QStringLiteral("semplice.log") : QStringLiteral("semplice.%1.log").arg(index);
    return QDir(m_dir).filePath(name);
}

QString Logger::filePath() const
{
    QMutexLocker lock(&m_mutex);
    return numberedPath(0);
}

void Logger::rotateLocked()
{
    m_file.close();
    // Drop the oldest, shift the rest up: .(n-2) -> .(n-1), ..., .1 -> .2, current -> .1
    (void)ownfiles::removeFile(numberedPath(m_keepFiles - 1));
    for (int i = m_keepFiles - 2; i >= 1; --i)
        (void)QFile::rename(numberedPath(i), numberedPath(i + 1));
    if (m_keepFiles > 1)
        (void)QFile::rename(numberedPath(0), numberedPath(1));
    m_file.setFileName(numberedPath(0));
    (void)m_file.open(QIODevice::WriteOnly | QIODevice::Append);
}

void Logger::write(LogLevel level, const QString& message)
{
    const QByteArray line = QStringLiteral("%1 [%2] %3\n")
                                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
                                     QString::fromLatin1(levelText(level)), message)
                                .toUtf8();

    QMutexLocker lock(&m_mutex);
    if (!m_file.isOpen())
        return;
    if (m_file.size() > 0 && m_file.size() + line.size() > m_maxBytes)
        rotateLocked();
    if (m_file.isOpen()) {
        m_file.write(line);
        m_file.flush();
    }
}

void Logger::setGlobal(Logger* logger)
{
    g_logger.store(logger);
}

Logger* Logger::global()
{
    return g_logger.load();
}

void Logger::installQtMessageHandler()
{
    g_previousHandler = qInstallMessageHandler(qtMessageHandler);
}

void logMessage(LogLevel level, const QString& message)
{
    if (Logger* logger = g_logger.load())
        logger->write(level, message);
}

}  // namespace semplice
