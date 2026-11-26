#include "core/storage/PathSanitizer.h"

#include <QtTest>

using namespace semplice;

// One table per row of SPEC 4.1 "Naming and path-sanitising rules".
class TstPathSanitizer : public QObject {
    Q_OBJECT

    static QString dir(const QString& s, NameKind k = NameKind::Artist) { return PathSanitizer::sanitizeDirName(s, k); }
    static bool isValidUtf16(const QString& s)
    {
        for (qsizetype i = 0; i < s.size(); ++i) {
            if (s.at(i).isHighSurrogate()) {
                if (i + 1 >= s.size() || !s.at(i + 1).isLowSurrogate())
                    return false;
                ++i;
            } else if (s.at(i).isLowSurrogate()) {
                return false;
            }
        }
        return true;
    }

private slots:
    // ---- rule: illegal characters
    void illegalCharacters_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("all nine") << QStringLiteral("a<b>c:d\"e/f\\g|h?i*j") << QStringLiteral("a_b_c_d_e_f_g_h_i_j");
        QTest::newRow("AC/DC") << QStringLiteral("AC/DC") << QStringLiteral("AC_DC");
        QTest::newRow("control character inside") << QStringLiteral("a\x01 b") << QStringLiteral("a_ b");
        QTest::newRow("tab inside") << QStringLiteral("a\tb") << QStringLiteral("a_b");
        QTest::newRow("trailing question mark is removed") << QStringLiteral("What?") << QStringLiteral("What");
        QTest::newRow("several trailing illegal") << QStringLiteral("Why??*") << QStringLiteral("Why");
        QTest::newRow("trailing newline removed") << QStringLiteral("Title\n") << QStringLiteral("Title");
        QTest::newRow("leading illegal becomes underscore") << QStringLiteral("?Mark") << QStringLiteral("_Mark");
        QTest::newRow("underscore the user typed stays") << QStringLiteral("snake_") << QStringLiteral("snake_");
    }
    void illegalCharacters()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(dir(input), expected);
    }

    // ---- rule: trailing dots and spaces
    void trailingDotsAndSpaces_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("dots") << QStringLiteral("Mr. Brightside...") << QStringLiteral("Mr. Brightside");
        QTest::newRow("spaces") << QStringLiteral("Name   ") << QStringLiteral("Name");
        QTest::newRow("mixed") << QStringLiteral("Name . . ") << QStringLiteral("Name");
        QTest::newRow("leading spaces trimmed") << QStringLiteral("   Name") << QStringLiteral("Name");
        QTest::newRow("inner dots stay") << QStringLiteral("R.E.M") << QStringLiteral("R.E.M");
    }
    void trailingDotsAndSpaces()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(dir(input), expected);
    }

    // ---- rule: reserved device names
    void reservedNames_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        for (const char* name : {"CON", "PRN", "AUX", "NUL"})
            QTest::newRow(name) << QString::fromLatin1(name) << QString::fromLatin1(name) + QLatin1Char('_');
        for (int i = 1; i <= 9; ++i) {
            QTest::newRow(qPrintable(QStringLiteral("COM%1").arg(i))) << QStringLiteral("COM%1").arg(i) << QStringLiteral("COM%1_").arg(i);
            QTest::newRow(qPrintable(QStringLiteral("LPT%1").arg(i))) << QStringLiteral("LPT%1").arg(i) << QStringLiteral("LPT%1_").arg(i);
        }
        QTest::newRow("lower case") << QStringLiteral("con") << QStringLiteral("con_");
        QTest::newRow("mixed case") << QStringLiteral("Nul") << QStringLiteral("Nul_");
        QTest::newRow("COM10 is fine") << QStringLiteral("COM10") << QStringLiteral("COM10");
        QTest::newRow("COM0 is fine") << QStringLiteral("COM0") << QStringLiteral("COM0");
        QTest::newRow("CONSOLE is fine") << QStringLiteral("CONSOLE") << QStringLiteral("CONSOLE");
        QTest::newRow("contains CON") << QStringLiteral("The CON Artists") << QStringLiteral("The CON Artists");
    }
    void reservedNames()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(dir(input), expected);
    }

    void reservedNamesWithExtension()
    {
        QCOMPARE(PathSanitizer::sanitizeFileName(QStringLiteral("CON"), QStringLiteral("txt")), QStringLiteral("CON_.txt"));
        QCOMPARE(PathSanitizer::sanitizeFileName(QStringLiteral("lpt1"), QStringLiteral("MP3")), QStringLiteral("lpt1_.mp3"));
        QCOMPARE(PathSanitizer::sanitizeFileName(QStringLiteral("NUL.tar"), QStringLiteral("gz")), QStringLiteral("NUL_.tar.gz"));
        QCOMPARE(PathSanitizer::sanitizeFileName(QStringLiteral("01 - CON"), QStringLiteral("mp3")), QStringLiteral("01 - CON.mp3"));
    }

    // ---- rule: empty result -> English fallback
    void emptyResultUsesTheFallbackOfItsKind_data()
    {
        QTest::addColumn<QString>("input");
        QTest::newRow("empty") << QString();
        QTest::newRow("spaces") << QStringLiteral("    ");
        QTest::newRow("dots") << QStringLiteral("...");
        QTest::newRow("only illegal") << QStringLiteral("???");
        QTest::newRow("slashes") << QStringLiteral("//\\\\");
        QTest::newRow("controls") << QStringLiteral("\x01\x02");
    }
    void emptyResultUsesTheFallbackOfItsKind()
    {
        QFETCH(QString, input);
        QCOMPARE(dir(input, NameKind::Artist), QStringLiteral("Unknown artist"));
        QCOMPARE(dir(input, NameKind::Album), QStringLiteral("Unknown album"));
        QCOMPARE(dir(input, NameKind::Playlist), QStringLiteral("Untitled"));
        QCOMPARE(PathSanitizer::sanitizeFileName(input, QStringLiteral("mp3")), QStringLiteral("Untitled.mp3"));
    }

    // ---- rule: length (120 UTF-16 units, code-point boundary)
    void longNamesAreCutTo120()
    {
        const QString longName(300, QLatin1Char('a'));
        QCOMPARE(dir(longName).size(), 120);
        QCOMPARE(dir(QString(120, QLatin1Char('b'))).size(), 120);  // exactly at the limit: unchanged
        QCOMPARE(dir(QString(121, QLatin1Char('b'))).size(), 120);
    }

    void truncationNeverSplitsASurrogatePair()
    {
        const QString emoji = QString::fromUtf16(u"\U0001F600");  // 2 UTF-16 units
        QCOMPARE(emoji.size(), 2);
        // 119 letters + one emoji = 121 units: the emoji does not fit and is dropped whole.
        const QString name = QString(119, QLatin1Char('a')) + emoji;
        const QString cut = dir(name);
        QVERIFY(isValidUtf16(cut));
        QCOMPARE(cut, QString(119, QLatin1Char('a')));
        // 118 letters + emoji = 120 units: fits exactly.
        const QString fits = QString(118, QLatin1Char('a')) + emoji;
        QCOMPARE(dir(fits), fits);
        // a long CJK name
        const QString cjk(200, QChar(0x6797));
        QCOMPARE(dir(cjk).size(), 120);
    }

    void fileNamesKeepTheirExtensionWhenShortened()
    {
        const QString name = PathSanitizer::sanitizeFileName(QString(300, QLatin1Char('x')), QStringLiteral("FLAC"));
        QCOMPARE(name.size(), 120);
        QVERIFY(name.endsWith(QStringLiteral(".flac")));
        QCOMPARE(PathSanitizer::sanitizeFileName(QStringLiteral("Song. "), QStringLiteral("mp3")), QStringLiteral("Song.mp3"));
        // a weird extension is reduced to letters and digits
        QCOMPARE(PathSanitizer::sanitizeFileName(QStringLiteral("a"), QStringLiteral("m/p?3")), QStringLiteral("a.mp3"));
    }

    void truncationLeavesNoTrailingSpaceOrDot()
    {
        const QString name = QString(119, QLatin1Char('a')) + QStringLiteral(" tail");
        const QString cut = dir(name);
        QVERIFY(!cut.endsWith(QLatin1Char(' ')));
        QVERIFY(cut.size() <= 120);
    }

    // ---- rule: Unicode (NFC, no transliteration)
    void unicodeIsNormalisedButNeverTransliterated()
    {
        QCOMPARE(dir(QStringLiteral("Café")), QStringLiteral("Café"));
        QCOMPARE(dir(QStringLiteral("Виктор Цой")), QStringLiteral("Виктор Цой"));
        QCOMPARE(dir(QStringLiteral("椅名林檎")), QStringLiteral("椅名林檎"));
        QCOMPARE(dir(QStringLiteral("がぎ")), QStringLiteral("がぎ"));
    }

    // ---- rule: collisions (case-insensitive, NFC, " (2)" ...)
    void collisionKeyIsCaseInsensitiveAndNormalised()
    {
        QCOMPARE(PathSanitizer::compareKey(QStringLiteral("ABBA")), PathSanitizer::compareKey(QStringLiteral("abba")));
        QCOMPARE(PathSanitizer::compareKey(QStringLiteral("Café")), PathSanitizer::compareKey(QStringLiteral("CAFÉ")));
        QCOMPARE(PathSanitizer::compareKey(QStringLiteral("Ёлка")), PathSanitizer::compareKey(QStringLiteral("ёлка")));
        QVERIFY(PathSanitizer::compareKey(QStringLiteral("a b")) != PathSanitizer::compareKey(QStringLiteral("a  b")));  // NTFS sees two names
    }

    void collisionsGetNumberedSuffixes()
    {
        QSet<QString> taken{PathSanitizer::compareKey(QStringLiteral("ABBA"))};
        auto isTaken = [&](const QString& n) { return taken.contains(PathSanitizer::compareKey(n)); };
        QCOMPARE(PathSanitizer::resolveCollision(QStringLiteral("abba"), false, isTaken), QStringLiteral("abba (2)"));
        taken.insert(PathSanitizer::compareKey(QStringLiteral("abba (2)")));
        QCOMPARE(PathSanitizer::resolveCollision(QStringLiteral("Abba"), false, isTaken), QStringLiteral("Abba (3)"));
        QCOMPARE(PathSanitizer::resolveCollision(QStringLiteral("Other"), false, isTaken), QStringLiteral("Other"));
    }

    void fileCollisionsKeepTheExtension()
    {
        QSet<QString> taken{PathSanitizer::compareKey(QStringLiteral("01 - Track.mp3"))};
        auto isTaken = [&](const QString& n) { return taken.contains(PathSanitizer::compareKey(n)); };
        QCOMPARE(PathSanitizer::resolveCollision(QStringLiteral("01 - TRACK.mp3"), true, isTaken),
                 QStringLiteral("01 - TRACK (2).mp3"));
    }

    void collisionSuffixStillFitsInTheLimit()
    {
        const QString longName = dir(QString(300, QLatin1Char('z')));
        const QString renamed = PathSanitizer::withCollisionSuffix(longName, 12, false);
        QCOMPARE(renamed.size(), 120);
        QVERIFY(renamed.endsWith(QStringLiteral(" (12)")));
        const QString file = PathSanitizer::sanitizeFileName(QString(300, QLatin1Char('z')), QStringLiteral("mp3"));
        const QString fileRenamed = PathSanitizer::withCollisionSuffix(file, 3, true);
        QCOMPARE(fileRenamed.size(), 120);
        QVERIFY(fileRenamed.endsWith(QStringLiteral(" (3).mp3")));
    }

    void differentArtistsThatSanitiseAlikeBecomeDistinctFolders()
    {
        const QString a = dir(QStringLiteral("AC/DC"));
        const QString b = dir(QStringLiteral("AC_DC"));
        QCOMPARE(a, b);  // the sanitiser alone cannot tell them apart ...
        QSet<QString> taken{PathSanitizer::compareKey(a)};
        // ... so the caller resolves the clash against what exists
        QCOMPARE(PathSanitizer::resolveCollision(b, false, [&](const QString& n) { return taken.contains(PathSanitizer::compareKey(n)); }),
                 QStringLiteral("AC_DC (2)"));
    }

    // ---- rule: full path length
    void veryLongFullPathsAreRefused()
    {
        QVERIFY(PathSanitizer::checkPathLength(QString(300, QLatin1Char('a'))).ok());  // > 260 is fine (long paths enabled)
        const auto r = PathSanitizer::checkPathLength(QString(PathSanitizer::kMaxFullPathLength + 1, QLatin1Char('a')));
        QVERIFY(!r.ok());
        QCOMPARE(r.error().code, ErrorCode::PathTooLong);
        QCOMPARE(r.error().symbol(), QStringLiteral("E_PATH_TOO_LONG"));
    }

    // ---- everything together
    void resultIsAlwaysASafeComponent_data()
    {
        QTest::addColumn<QString>("input");
        const QStringList inputs = {QStringLiteral("normal"),      QStringLiteral("CON"),
                                    QStringLiteral("a/b\\c"),      QStringLiteral("trailing."),
                                    QStringLiteral(".hidden"),     QStringLiteral(" "),
                                    QStringLiteral("***"),         QString(500, QLatin1Char('x')),
                                    QStringLiteral("Café?"), QStringLiteral("\U0001F600\U0001F600")};
        for (const QString& s : inputs)
            QTest::newRow(qPrintable(s.left(20))) << s;
    }
    void resultIsAlwaysASafeComponent()
    {
        QFETCH(QString, input);
        for (NameKind kind : {NameKind::Artist, NameKind::Album, NameKind::Playlist, NameKind::Track}) {
            const QString out = dir(input, kind);
            QVERIFY(!out.isEmpty());
            QVERIFY(out.size() <= PathSanitizer::kMaxComponentLength);
            QVERIFY(isValidUtf16(out));
            for (const QChar c : out)
                QVERIFY2(c.unicode() >= 32 && !QStringLiteral("<>:\"/\\|?*").contains(c), qPrintable(out));
            QVERIFY(!out.endsWith(QLatin1Char('.')) && !out.endsWith(QLatin1Char(' ')));
        }
    }
};

QTEST_GUILESS_MAIN(TstPathSanitizer)
#include "tst_path_sanitizer.moc"
