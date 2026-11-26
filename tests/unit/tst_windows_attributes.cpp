#include "platform/WindowsFileAttributes.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace semplice;

class TstWindowsAttributes : public QObject {
    Q_OBJECT
private slots:
    void hidesAFolder()
    {
#ifndef Q_OS_WIN
        QSKIP("Windows only");
#else
        QTemporaryDir dir;
        const QString folder = dir.filePath(QStringLiteral(".library"));
        QVERIFY(QDir().mkpath(folder));
        QVERIFY(!QFileInfo(folder).isHidden());
        platform::WindowsFileAttributes attributes;
        QVERIFY(attributes.setHidden(folder).ok());
        QVERIFY(QFileInfo(folder).isHidden());
#endif
    }

    void reportsAMissingFolder()
    {
        platform::WindowsFileAttributes attributes;
#ifdef Q_OS_WIN
        QVERIFY(!attributes.setHidden(QStringLiteral("C:/definitely/not/here/xyz")).ok());
#else
        QVERIFY(attributes.setHidden(QStringLiteral("/nope")).ok());
#endif
    }
};

QTEST_GUILESS_MAIN(TstWindowsAttributes)
#include "tst_windows_attributes.moc"
