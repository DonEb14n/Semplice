#include "core/util/Normalizer.h"

#include <QtTest>

using namespace semplice;

class TstNormalizer : public QObject {
    Q_OBJECT
private slots:
    void identityKeyIsCaseSpacingAndNormalisationInsensitive()
    {
        QCOMPARE(Normalizer::identityKey(QStringLiteral("  The   Beatles ")), QStringLiteral("the beatles"));
        QCOMPARE(Normalizer::identityKey(QStringLiteral("ABBA")), Normalizer::identityKey(QStringLiteral("abba")));
        // precomposed vs decomposed
        QCOMPARE(Normalizer::identityKey(QStringLiteral("Café")), Normalizer::identityKey(QStringLiteral("Café")));
        // Cyrillic and Greek case folding
        QCOMPARE(Normalizer::identityKey(QStringLiteral("Ёлка")), Normalizer::identityKey(QStringLiteral("ёлка")));
        QCOMPARE(Normalizer::identityKey(QStringLiteral("ΣΟΦΙΑ")), Normalizer::identityKey(QStringLiteral("σοφια")));
        // letters outside ASCII fold as well
        QCOMPARE(Normalizer::identityKey(QStringLiteral("STRAßE")), Normalizer::identityKey(QStringLiteral("straße")));
        // NBSP counts as a space
        QCOMPARE(Normalizer::identityKey(QStringLiteral("a b")), QStringLiteral("a b"));
    }

    void identityKeyKeepsDiacriticsAndDistinctNames()
    {
        QVERIFY(Normalizer::identityKey(QStringLiteral("Beyoncé")) != Normalizer::identityKey(QStringLiteral("Beyonce")));
        QVERIFY(Normalizer::identityKey(QStringLiteral("й")) != Normalizer::identityKey(QStringLiteral("и")));
        QVERIFY(Normalizer::identityKey(QStringLiteral("が")) != Normalizer::identityKey(QStringLiteral("か")));
    }

    void searchKeyStripsDiacriticsOnly()
    {
        QCOMPARE(Normalizer::searchKey(QStringLiteral("Beyoncé")), QStringLiteral("beyonce"));
        QCOMPARE(Normalizer::searchKey(QStringLiteral("Motörhead")), QStringLiteral("motorhead"));
        QCOMPARE(Normalizer::searchKey(QStringLiteral("Ёлка")), QStringLiteral("елка"));  // Ёлка -> елка
        // Japanese voiced marks are part of the letter: が stays が, 椎名林檎 stays as is.
        QCOMPARE(Normalizer::searchKey(QStringLiteral("が")), QStringLiteral("が"));
        QCOMPARE(Normalizer::searchKey(QStringLiteral("椅名林檎")), QStringLiteral("椅名林檎"));
    }

    void displayTrimsAndComposes()
    {
        QCOMPARE(Normalizer::display(QStringLiteral("  Café ")), QStringLiteral("Café"));
    }
};

QTEST_GUILESS_MAIN(TstNormalizer)
#include "tst_normalizer.moc"
