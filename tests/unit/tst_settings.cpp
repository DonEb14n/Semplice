#include "core/services/SettingsService.h"

#include <QtTest>

using namespace semplice;

class TstSettings : public QObject {
    Q_OBJECT
private:
    QString iniPath() const { return m_dir.filePath(QStringLiteral("settings.ini")); }
    QTemporaryDir m_dir;

private slots:
    void init() { QFile::remove(iniPath()); }

    void defaults()
    {
        SettingsService s(iniPath());
        QCOMPARE(s.language(), QStringLiteral("en"));
        QCOMPARE(s.userName(), QString());
        QCOMPARE(s.theme(), QStringLiteral("true-white"));
        QCOMPARE(s.volume(), 0.5);
        QVERIFY(!s.muted());
        QVERIFY(!s.windowGeometry().valid);
    }

    void roundTrip()
    {
        {
            SettingsService s(iniPath());
            s.setLanguage(QStringLiteral("ru"));
            s.setUserName(QStringLiteral("  Anna  "));
            s.setVolume(0.8);
            s.setMuted(true);
            s.setWindowGeometry({10, 20, 1280, 760, true, true});
        }
        SettingsService s(iniPath());  // a fresh object reads the file
        QCOMPARE(s.language(), QStringLiteral("ru"));
        QCOMPARE(s.userName(), QStringLiteral("Anna"));
        QCOMPARE(s.volume(), 0.8);
        QVERIFY(s.muted());
        const WindowGeometry g = s.windowGeometry();
        QVERIFY(g.valid);
        QCOMPARE(g.x, 10);
        QCOMPARE(g.y, 20);
        QCOMPARE(g.width, 1280);
        QCOMPARE(g.height, 760);
        QVERIFY(g.maximised);
    }

    void writeMissingDefaultsKeepsExistingValues()
    {
        SettingsService s(iniPath());
        s.setLanguage(QStringLiteral("ja"));
        s.writeMissingDefaults();
        QCOMPARE(s.language(), QStringLiteral("ja"));  // not overwritten

        QSettings raw(iniPath(), QSettings::IniFormat);
        QVERIFY(raw.contains(QStringLiteral("app/theme")));
        QVERIFY(raw.contains(QStringLiteral("audio/volume")));
    }

    void unknownLanguageFallsBackToEnglish()
    {
        {
            QSettings raw(iniPath(), QSettings::IniFormat);
            raw.setValue(QStringLiteral("app/language"), QStringLiteral("klingon"));
        }
        SettingsService s(iniPath());
        QCOMPARE(s.language(), QStringLiteral("en"));
        s.setLanguage(QStringLiteral("xx"));
        QCOMPARE(s.language(), QStringLiteral("en"));
    }

    void userNameIsTruncated()
    {
        SettingsService s(iniPath());
        s.setUserName(QString(80, QLatin1Char('a')));
        QCOMPARE(s.userName().size(), SettingsService::kMaxUserNameLength);
    }

    void volumeIsClamped()
    {
        SettingsService s(iniPath());
        s.setVolume(3.0);
        QCOMPARE(s.volume(), 1.0);
        s.setVolume(-1.0);
        QCOMPARE(s.volume(), 0.0);
    }
};

QTEST_GUILESS_MAIN(TstSettings)
#include "tst_settings.moc"
