#include "core/util/Logger.h"

#include <QtTest>

using namespace semplice;

class TstLogger : public QObject {
    Q_OBJECT
private slots:
    void writesLevelAndMessage()
    {
        QTemporaryDir dir;
        Logger logger;
        QVERIFY(logger.init(dir.path()));
        logger.write(LogLevel::Info, QStringLiteral("hello"));
        logger.write(LogLevel::Error, QStringLiteral("boom"));

        QFile f(logger.filePath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(f.readAll());
        QVERIFY(text.contains(QStringLiteral("[INFO ] hello")));
        QVERIFY(text.contains(QStringLiteral("[ERROR] boom")));
    }

    void rotatesAndKeepsAtMostNFiles()
    {
        QTemporaryDir dir;
        Logger logger;
        constexpr qint64 kMax = 400;
        constexpr int kKeep = 3;
        QVERIFY(logger.init(dir.path(), kMax, kKeep));
        for (int i = 0; i < 200; ++i)
            logger.write(LogLevel::Info, QStringLiteral("line number %1 with some padding text").arg(i));

        const QStringList files = QDir(dir.path()).entryList({QStringLiteral("semplice*.log")}, QDir::Files);
        QCOMPARE(files.size(), kKeep);
        for (const QString& name : files)
            QVERIFY2(QFileInfo(dir.filePath(name)).size() <= kMax, qPrintable(name));
    }

    void macrosGoToGlobalLogger()
    {
        QTemporaryDir dir;
        Logger logger;
        QVERIFY(logger.init(dir.path()));
        Logger::setGlobal(&logger);
        SLOG_WARN(QStringLiteral("via macro"));
        Logger::setGlobal(nullptr);
        SLOG_WARN(QStringLiteral("dropped"));  // no logger installed: must not crash

        QFile f(logger.filePath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(f.readAll());
        QVERIFY(text.contains(QStringLiteral("via macro")));
        QVERIFY(!text.contains(QStringLiteral("dropped")));
    }
};

QTEST_GUILESS_MAIN(TstLogger)
#include "tst_logger.moc"
