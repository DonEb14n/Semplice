#include "core/tags/ArtistStringSplitter.h"

#include <QtTest>

using namespace semplice;

class TstArtistSplitter : public QObject {
    Q_OBJECT

    static ArtistStringSplitter::Exists nobody()
    {
        return [](const QString&) { return false; };
    }
    static ArtistStringSplitter::Exists only(const QStringList& names)
    {
        return [names](const QString& n) { return names.contains(n, Qt::CaseInsensitive); };
    }
    static QStringList names(const QList<NamedCredit>& l)
    {
        QStringList out;
        for (const auto& c : l)
            out << c.name;
        return out;
    }

private slots:
    void aPlainNameStaysWhole()
    {
        const auto r = ArtistStringSplitter::split(QStringLiteral("Just One"), nobody());
        QCOMPARE(r.size(), 1);
        QCOMPARE(r.first().name, QStringLiteral("Just One"));
        QCOMPARE(r.first().role, CreditRole::Main);
    }

    void featSeparatorsMakeFeaturedCredits_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("feat.") << QStringLiteral("A feat. B");
        QTest::newRow("ft.") << QStringLiteral("A ft. B");
        QTest::newRow("featuring") << QStringLiteral("A featuring B");
        QTest::newRow("parens") << QStringLiteral("A (feat. B)");
        QTest::newRow("brackets") << QStringLiteral("A [ft. B]");
        QTest::newRow("upper case") << QStringLiteral("A FEAT. B");
    }
    void featSeparatorsMakeFeaturedCredits()
    {
        QFETCH(QString, text);
        const auto r = ArtistStringSplitter::split(text, nobody());
        QCOMPARE(r.size(), 2);
        QCOMPARE(r.at(0).name, QStringLiteral("A"));
        QCOMPARE(r.at(0).role, CreditRole::Main);
        QCOMPARE(r.at(1).name, QStringLiteral("B"));
        QCOMPARE(r.at(1).role, CreditRole::Feat);
    }

    void semicolonAndAmpersandSeparateMainArtists()
    {
        const auto r = ArtistStringSplitter::split(QStringLiteral("A; B & C"), nobody());
        QCOMPARE(names(r), (QStringList{QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}));
        for (const auto& c : r)
            QCOMPARE(c.role, CreditRole::Main);
    }

    void anExistingArtistIsNeverSplit()
    {
        const auto r = ArtistStringSplitter::split(QStringLiteral("Simon & Garfunkel"), only({QStringLiteral("Simon & Garfunkel")}));
        QCOMPARE(names(r), QStringList{QStringLiteral("Simon & Garfunkel")});
        const auto fresh = ArtistStringSplitter::split(QStringLiteral("Simon & Garfunkel"), nobody());
        QCOMPARE(fresh.size(), 2);
    }

    void commasSplitUnlessTheWholeNameExistsAndSlashesNever()
    {
        // owner decision after the first test round: "A, B" in a tag means two artists
        QCOMPARE(names(ArtistStringSplitter::split(QStringLiteral("Artist One, Artist Two"), nobody())),
                 (QStringList{QStringLiteral("Artist One"), QStringLiteral("Artist Two")}));
        QCOMPARE(names(ArtistStringSplitter::split(QStringLiteral("A, B & C; D"), nobody())),
                 (QStringList{QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"), QStringLiteral("D")}));
        QCOMPARE(names(ArtistStringSplitter::split(QStringLiteral("Tyler, The Creator"), only({QStringLiteral("Tyler, The Creator")}))),
                 QStringList{QStringLiteral("Tyler, The Creator")});  // an existing artist stays whole
        QCOMPARE(names(ArtistStringSplitter::split(QStringLiteral("AC/DC"), nobody())), QStringList{QStringLiteral("AC/DC")});
    }

    void multiValueFieldsAreSplitAndDeduplicated()
    {
        const auto r = ArtistStringSplitter::splitAll({QStringLiteral("A feat. B"), QStringLiteral("a"), QStringLiteral("C")}, nobody());
        QCOMPARE(names(r), (QStringList{QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}));
    }

    void emptyPartsAreDropped()
    {
        QVERIFY(ArtistStringSplitter::split(QStringLiteral("  "), nobody()).isEmpty());
        QCOMPARE(names(ArtistStringSplitter::split(QStringLiteral("A;;B; "), nobody())), (QStringList{QStringLiteral("A"), QStringLiteral("B")}));
    }
};

QTEST_GUILESS_MAIN(TstArtistSplitter)
#include "tst_artist_splitter.moc"
