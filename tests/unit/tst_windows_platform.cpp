#include "TestAudioFiles.h"

#include "core/tags/EncodingFixer.h"
#include "core/tags/TagService.h"
#include "platform/WindowsAtomicReplace.h"
#include "platform/WindowsCodepageConverter.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace semplice;
using namespace semplice::testing;

// The real Windows implementations: true code-page tables and true atomic replace.
class TstWindowsPlatform : public QObject {
    Q_OBJECT

    platform::WindowsCodepageConverter m_conv;

    QString mojibake(const QString& good, CodePage page) const
    {
        const auto bytes = m_conv.encode(good, page);
        return bytes ? QString::fromLatin1(*bytes) : QString();
    }

private slots:
    void initTestCase()
    {
#ifndef Q_OS_WIN
        QSKIP("Windows only");
#endif
    }

    // ================================================================ converter
    void everyCodePageRoundTrips_data()
    {
        QTest::addColumn<int>("page");
        QTest::addColumn<QString>("text");
        QTest::newRow("cp1251") << int(CodePage::Cp1251) << QStringLiteral("Мумий Тролль — Земфира");
        QTest::newRow("gbk") << int(CodePage::Cp936) << QStringLiteral("周杰伦 七里香");
        QTest::newRow("shift-jis") << int(CodePage::Cp932) << QStringLiteral("椎名林檎 ありがとう カタカナ");
        QTest::newRow("euc-kr") << int(CodePage::Cp949) << QStringLiteral("방탄소년단");
        QTest::newRow("big5") << int(CodePage::Cp950) << QStringLiteral("周杰倫");
        QTest::newRow("utf-8") << int(CodePage::Utf8) << QStringLiteral("Привет 你好 \U0001F600");
    }
    void everyCodePageRoundTrips()
    {
        QFETCH(int, page);
        QFETCH(QString, text);
        const auto bytes = m_conv.encode(text, static_cast<CodePage>(page));
        QVERIFY(bytes.has_value());
        QCOMPARE(m_conv.decode(*bytes, static_cast<CodePage>(page)), std::optional<QString>(text));
    }

    void invalidInputIsReportedNotReplaced()
    {
        QVERIFY(!m_conv.decode(QByteArray("\x81"), CodePage::Cp932).has_value());     // lone lead byte
        QVERIFY(!m_conv.decode(QByteArray("\xFF\xFE\xFD"), CodePage::Utf8).has_value());
        QVERIFY(!m_conv.encode(QStringLiteral("\U0001F600"), CodePage::Cp1251).has_value());
        QVERIFY(!m_conv.encode(QStringLiteral("你"), CodePage::Cp1251).has_value());
        QCOMPARE(m_conv.decode(QByteArray(), CodePage::Cp1251), std::optional<QString>(QString()));
    }

    // ================================================================ the fixer with the REAL tables
    void realMojibakeIsRepaired_data()
    {
        QTest::addColumn<QString>("good");
        QTest::addColumn<int>("page");
        QTest::newRow("russian band") << QStringLiteral("Мумий Тролль") << int(CodePage::Cp1251);
        QTest::newRow("russian title") << QStringLiteral("Король и Шут - Лесник (Live)") << int(CodePage::Cp1251);
        QTest::newRow("ukrainian") << QStringLiteral("Океан Ельзи - Вночі") << int(CodePage::Cp1251);
        QTest::newRow("chinese") << QStringLiteral("周杰伦 - 七里香") << int(CodePage::Cp936);
        QTest::newRow("chinese 2") << QStringLiteral("王菲 我愿意") << int(CodePage::Cp936);
        QTest::newRow("japanese kana") << QStringLiteral("宇多田ヒカル") << int(CodePage::Cp932);
        QTest::newRow("japanese hiragana") << QStringLiteral("ありがとう") << int(CodePage::Cp932);
        QTest::newRow("japanese kanji only") << QStringLiteral("椎名林檎") << int(CodePage::Cp932);
    }
    void realMojibakeIsRepaired()
    {
        QFETCH(QString, good);
        QFETCH(int, page);
        const QString broken = mojibake(good, static_cast<CodePage>(page));
        QVERIFY(!broken.isEmpty() && broken != good);
        const FixResult r = EncodingFixer(m_conv).fixText(broken);
        QVERIFY2(r.changed, qPrintable(good));
        QCOMPARE(r.text, good);
        QCOMPARE(static_cast<int>(*r.page), page);
    }

    void ambiguousCjkIsFixedWithTheRightPreference()
    {
        // Big5 and EUC-KR bytes are often also valid GBK: the order decides, and the right reading is always offered.
        const QString traditional = QStringLiteral("周杰倫");
        const QString korean = QStringLiteral("방탄소년단");

        for (const auto& [good, page] : {std::pair{traditional, CodePage::Cp950}, std::pair{korean, CodePage::Cp949}}) {
            const QString broken = mojibake(good, page);
            const FixResult preferred = EncodingFixer(m_conv, {page}).fixText(broken);
            QVERIFY2(preferred.changed, qPrintable(good));
            QCOMPARE(preferred.text, good);

            const FixResult byDefault = EncodingFixer(m_conv).fixText(broken);  // may pick another reading ...
            QVERIFY2(byDefault.changed, qPrintable(good));
            bool offered = byDefault.text == good;  // ... but the correct one must be on offer
            for (const FixCandidate& c : byDefault.alternatives)
                offered = offered || c.text == good;
            QVERIFY2(offered, qPrintable(good));
        }
    }

    void westernTextIsNeverTouchedByTheRealConverter()
    {
        const EncodingFixer fixer(m_conv);
        for (const QString& s : {QStringLiteral("Café"), QStringLiteral("Björk Guðmundsdóttir"), QStringLiteral("Ñandú"),
                                 QStringLiteral("Mötley Crüe"), QStringLiteral("Beyoncé"), QStringLiteral("Ünïcödé"),
                                 QStringLiteral("Ünïcöd"), QStringLiteral("Sigur Rós"), QStringLiteral("Plácido Domingo - Ça va"),
                                 QStringLiteral("Zoë Jenny Ångström"), QStringLiteral("niño señor"), QStringLiteral("é")})
            QVERIFY2(!fixer.fixText(s).changed, qPrintable(s));
    }

    // ================================================================ atomic replace
    void replaceFileSwapsTheContentInOneStep()
    {
        QTemporaryDir dir;
        const QString target = dir.filePath(QStringLiteral("a.bin"));
        const QString replacement = dir.filePath(QStringLiteral("a.new"));
        QFile t(target);
        QVERIFY(t.open(QIODevice::WriteOnly));
        t.write("old");
        t.close();
        QFile r(replacement);
        QVERIFY(r.open(QIODevice::WriteOnly));
        r.write("new");
        r.close();

        platform::WindowsAtomicReplace replace;
        QVERIFY(replace.replaceFile(replacement, target).ok());
        QFile check(target);
        QVERIFY(check.open(QIODevice::ReadOnly));
        QCOMPARE(check.readAll(), QByteArray("new"));
        QVERIFY(!QFileInfo::exists(replacement));            // consumed
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 1);  // no backup file left behind
    }

    void replaceFileOfAHeldOpenTargetReportsLocked()
    {
        QTemporaryDir dir;
        const QString target = dir.filePath(QStringLiteral("playing.mp3"));
        const QString replacement = dir.filePath(QStringLiteral("playing.new"));
        for (const QString& p : {target, replacement}) {
            QFile f(p);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(p == target ? "old" : "new");
        }
        QFile holder(target);
        QVERIFY(holder.open(QIODevice::ReadOnly));  // "the player"

        platform::WindowsAtomicReplace replace;
        const auto r = replace.replaceFile(replacement, target);
        QVERIFY(!r.ok());
        QCOMPARE(r.error().code, ErrorCode::FileLocked);
        holder.close();
        QFile check(target);
        QVERIFY(check.open(QIODevice::ReadOnly));
        QCOMPARE(check.readAll(), QByteArray("old"));  // untouched
        QVERIFY(QFileInfo::exists(replacement));
    }

    void tagServiceWorksWithTheWindowsReplaceForEveryFormat_data()
    {
        QTest::addColumn<int>("format");
        for (AudioFormat f : allAudioFormats())
            QTest::newRow(qPrintable(nameOf(f))) << static_cast<int>(f);
    }
    void tagServiceWorksWithTheWindowsReplaceForEveryFormat()
    {
        QFETCH(int, format);
        QTemporaryDir dir;
        const QString path = writeTinyAudio(dir.path(), QStringLiteral("t"), static_cast<AudioFormat>(format));
        platform::WindowsAtomicReplace replace;
        TagService svc(replace);
        TagWrite w;
        w.title = QStringLiteral("Ёлка 椎名林檎");
        w.artists = {QStringLiteral("A")};
        w.genres = {QStringLiteral("Rock")};
        QVERIFY(svc.write(path, w).ok());
        QCOMPARE(svc.read(path).value().title, w.title);
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).size(), 1);
    }

    void aFileHeldOpenIsReportedAsLockedAndLeftIntact()
    {
        QTemporaryDir dir;
        const QString path = writeTinyAudio(dir.path(), QStringLiteral("playing"), AudioFormat::Flac);
        QFile before(path);
        QVERIFY(before.open(QIODevice::ReadOnly));
        const QByteArray original = before.readAll();
        QFile holder(path);
        QVERIFY(holder.open(QIODevice::ReadOnly));

        platform::WindowsAtomicReplace replace;
        TagService svc(replace);
        TagWrite w;
        w.title = QStringLiteral("t");
        w.artists = {QStringLiteral("a")};
        const auto r = svc.write(path, w);
        QVERIFY(!r.ok());
        QCOMPARE(r.error().code, ErrorCode::FileLocked);
        holder.close();
        before.seek(0);
        QCOMPARE(before.readAll(), original);
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).size(), 1);  // temp copy removed
    }
};

QTEST_GUILESS_MAIN(TstWindowsPlatform)
#include "tst_windows_platform.moc"
