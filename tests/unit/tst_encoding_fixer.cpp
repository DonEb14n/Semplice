#include "FakeCodepageConverter.h"

#include "core/tags/EncodingFixer.h"

#include <QtTest>

using namespace semplice;
using namespace semplice::testing;

// Fixtures are built the way the damage happens: a known-good string is encoded in the "wrong" code
// page and the bytes are read back as ISO-8859-1. The tests then prove both directions: broken text is
// repaired, and text that is already right is left alone.
class TstEncodingFixer : public QObject {
    Q_OBJECT

    FakeCodepageConverter m_conv;
    const QString kChinese = QStringLiteral("你好世界周杰伦七里香");
    const QString kJapanese = QStringLiteral("ありがとうさくらカタカナ椎名林檎");
    const QString kTraditional = QStringLiteral("你好世界周杰倫七里香");
    const QString kKorean = QStringLiteral("안녕하세요방탄소년단");

private slots:
    void initTestCase()
    {
        // invented but structurally valid codes (see FakeCodepageConverter)
        m_conv.addTable(CodePage::Cp936, kChinese, 0xB0, 0xA1);           // GB2312 range
        m_conv.addTable(CodePage::Cp932, kJapanese, 0x82, 0x9F, 94);      // JIS lead 0x82
        m_conv.addTable(CodePage::Cp950, kTraditional, 0xA4, 0xA1, 94);   // Big5 pairs with trail >= 0xA1
        m_conv.addTable(CodePage::Cp949, kKorean, 0xC0, 0xA1);            // KS X 1001 (own range: no clash with the Chinese table)
    }

    // ================================================================ broken -> fixed
    void russianTextStoredAsCp1251IsRepaired_data()
    {
        QTest::addColumn<QString>("good");
        QTest::newRow("band") << QStringLiteral("Мумий Тролль");
        QTest::newRow("short") << QStringLiteral("Кино");
        QTest::newRow("with punctuation") << QStringLiteral("Земфира - Хочешь? (Live, 2004)");
        QTest::newRow("latin word elsewhere") << QStringLiteral("DJ Смэш");
        QTest::newRow("ё") << QStringLiteral("Ёлка");
        QTest::newRow("ukrainian") << QStringLiteral("Океан Ельзи — Вночі, ґанок і їжак, єдність");
        QTest::newRow("quotes") << QStringLiteral("«Ария» — “Герой асфальта”");
    }
    void russianTextStoredAsCp1251IsRepaired()
    {
        QFETCH(QString, good);
        const QString broken = m_conv.mojibake(good, CodePage::Cp1251);
        QVERIFY(!broken.isEmpty());
        QVERIFY(broken != good);  // the fixture really is damaged
        const EncodingFixer fixer(m_conv);
        const FixResult r = fixer.fixText(broken);
        QVERIFY2(r.changed, qPrintable(good));
        QCOMPARE(r.text, good);
        QCOMPARE(*r.page, CodePage::Cp1251);
    }

    void utf8WrittenIntoALatin1FieldIsRepaired()
    {
        const EncodingFixer fixer(m_conv);
        for (const QString& good : {QStringLiteral("Привет, мир"), QStringLiteral("Café Tacvba"), QStringLiteral("Björk"),
                                    QStringLiteral("椎名林檎"), QStringLiteral("Mötley Crüe")}) {
            const QString broken = QString::fromLatin1(good.toUtf8());  // classic "Ã©" mojibake
            const FixResult r = fixer.fixText(broken);
            QVERIFY2(r.changed, qPrintable(good));
            QCOMPARE(r.text, good);
            QCOMPARE(*r.page, CodePage::Utf8);
        }
    }

    void cjkTextIsRepairedByItsOwnCodePage()
    {
        const EncodingFixer fixer(m_conv);
        struct Case { QString good; CodePage page; };
        for (const Case& c : {Case{QStringLiteral("你好世界周杰伦"), CodePage::Cp936},
                              Case{QStringLiteral("ありがとうカタカナ"), CodePage::Cp932},
                              Case{QStringLiteral("椎名林檎"), CodePage::Cp932},  // Han only, still unambiguous: no GB2312 reading exists
                              Case{QStringLiteral("你好世界周杰倫"), CodePage::Cp950},
                              Case{QStringLiteral("안녕하세요방탄소년단"), CodePage::Cp949}}) {
            const QString broken = m_conv.mojibake(c.good, c.page);
            QVERIFY2(!broken.isEmpty(), qPrintable(c.good));
            const FixResult r = fixer.fixText(broken);
            QVERIFY2(r.changed, qPrintable(c.good));
            QCOMPARE(r.text, c.good);
            QCOMPARE(*r.page, c.page);
        }
    }

    void japaneseKanaBeatsTheChineseReading()
    {
        // Register the SAME bytes in two pages; the one that produces kana must win even though GBK comes first.
        FakeCodepageConverter both;
        both.addTable(CodePage::Cp936, QStringLiteral("你好世界"), 0xE0, 0xA1);
        both.addTable(CodePage::Cp932, QStringLiteral("あいうえ"), 0xE0, 0xA1);  // valid GB2312 and valid JIS pairs
        const QString broken = QString::fromLatin1(*both.encode(QStringLiteral("你好世界"), CodePage::Cp936));
        const FixResult r = EncodingFixer(both).fixText(broken);
        QVERIFY(r.changed);
        QCOMPARE(r.text, QStringLiteral("あいうえ"));
        QCOMPARE(*r.page, CodePage::Cp932);
        QCOMPARE(r.alternatives.size(), 1);  // the Chinese reading is reported as an alternative
        QCOMPARE(r.alternatives.first().text, QStringLiteral("你好世界"));
    }

    void ambiguousHanTextTakesTheFirstPageInTheConfiguredOrder()
    {
        FakeCodepageConverter both;
        both.addTable(CodePage::Cp936, QStringLiteral("你好世界"), 0xB0, 0xA1);
        both.addTable(CodePage::Cp950, QStringLiteral("爾好世界"), 0xB0, 0xA1);  // same bytes, other characters
        const QString broken = QString::fromLatin1(*both.encode(QStringLiteral("你好世界"), CodePage::Cp936));

        FixResult r = EncodingFixer(both).fixText(broken);  // default order: GBK before Big5
        QCOMPARE(r.text, QStringLiteral("你好世界"));
        QCOMPARE(*r.page, CodePage::Cp936);
        QCOMPARE(r.alternatives.size(), 1);
        QCOMPARE(r.alternatives.first().page, CodePage::Cp950);

        r = EncodingFixer(both, {CodePage::Cp950, CodePage::Cp936}).fixText(broken);  // a Traditional-Chinese user
        QCOMPARE(r.text, QStringLiteral("爾好世界"));
        QCOMPARE(*r.page, CodePage::Cp950);
    }

    // ================================================================ correct text is never touched
    void correctTextIsLeftAlone_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("ascii") << QStringLiteral("Plain ASCII - Title (2020)");
        QTest::newRow("empty") << QString();
        QTest::newRow("one accent") << QStringLiteral("Café");
        QTest::newRow("name") << QStringLiteral("Björk Guðmundsdóttir");
        QTest::newRow("spanish") << QStringLiteral("Ñandú");
        QTest::newRow("german") << QStringLiteral("Mötley Crüe");
        QTest::newRow("french") << QStringLiteral("Beyoncé");
        QTest::newRow("accented mixed") << QStringLiteral("Ünïcödé");
        QTest::newRow("accented pairs") << QStringLiteral("Ünïcöd");
        QTest::newRow("sigur") << QStringLiteral("Sigur Rós");
        QTest::newRow("zoe") << QStringLiteral("Zoë Jenny Ångström");
        QTest::newRow("placido") << QStringLiteral("Plácido Domingo - Ça va");
        QTest::newRow("lowercase run") << QStringLiteral("niño señor");
        QTest::newRow("nordic") << QStringLiteral("Sigur Rós – Ágætis byrjun");  // en dash is above U+00FF: real Unicode
        QTest::newRow("real cyrillic") << QStringLiteral("Мумий Тролль");
        QTest::newRow("real chinese") << QStringLiteral("周杰伦");
        QTest::newRow("real japanese") << QStringLiteral("椎名林檎");
        QTest::newRow("single accented letter") << QStringLiteral("é");
        QTest::newRow("symbols") << QStringLiteral("© 2004 · ½ — ±");
    }
    void correctTextIsLeftAlone()
    {
        QFETCH(QString, text);
        const FixResult r = EncodingFixer(m_conv).fixText(text);
        QVERIFY2(!r.changed, qPrintable(text));
        QCOMPARE(r.text, text);
        QVERIFY(!r.page.has_value());
    }

    void textThatCannotBeDecodedIsLeftAlone()
    {
        const EncodingFixer fixer(m_conv);
        // ID3v1 cuts a field at 30 bytes: a Chinese name cut in the middle of a pair
        QByteArray cut = *m_conv.encode(QStringLiteral("你好世界周杰伦"), CodePage::Cp936);
        cut.chop(1);
        const QString broken = QString::fromLatin1(cut);
        QVERIFY(!fixer.fixText(broken).changed);
        // bytes that belong to no table at all
        QVERIFY(!fixer.fixText(QString::fromLatin1("\xB0\xA1\xB0\xA2\xE3\xE4")).changed);
        // the same Chinese bytes must not turn into Russian gibberish
        const QString chinese = m_conv.mojibake(QStringLiteral("你好世界"), CodePage::Cp936);
        const FixResult r = fixer.fixText(chinese);
        QCOMPARE(*r.page, CodePage::Cp936);
    }

    void aWrongOrderOrAnEmptyOrderNeverInventsText()
    {
        const QString broken = m_conv.mojibake(QStringLiteral("Мумий Тролль"), CodePage::Cp1251);
        QVERIFY(!EncodingFixer(m_conv, {}).fixText(broken).changed);
        QVERIFY(!EncodingFixer(m_conv, {CodePage::Cp936, CodePage::Cp950}).fixText(broken).changed);
    }

    // ================================================================ whole tag sets
    void onlyFieldsFlaggedAsLegacyAreRepaired()
    {
        TrackTags tags;
        tags.title = m_conv.mojibake(QStringLiteral("Земфира"), CodePage::Cp1251);
        tags.artists = {m_conv.mojibake(QStringLiteral("Мумий Тролль"), CodePage::Cp1251), QStringLiteral("Björk")};
        tags.album = m_conv.mojibake(QStringLiteral("Альбом"), CodePage::Cp1251);  // looks broken but NOT flagged
        tags.albumArtists = {m_conv.mojibake(QStringLiteral("Кино"), CodePage::Cp1251)};
        tags.genres = {QStringLiteral("Rock")};
        tags.legacy8bit = {tagfield::Title, tagfield::Artists, tagfield::AlbumArtists, tagfield::Genres};

        const EncodingFixer::Report report = EncodingFixer(m_conv).fix(tags);
        QCOMPARE(tags.title, QStringLiteral("Земфира"));
        QCOMPARE(tags.artists, (QStringList{QStringLiteral("Мумий Тролль"), QStringLiteral("Björk")}));  // per value
        QCOMPARE(tags.albumArtists, QStringList{QStringLiteral("Кино")});
        QCOMPARE(tags.album, m_conv.mojibake(QStringLiteral("Альбом"), CodePage::Cp1251));  // trusted: untouched
        QCOMPARE(tags.genres, QStringList{QStringLiteral("Rock")});
        QCOMPARE(report.repairedFields, (QStringList{tagfield::Title, tagfield::Artists, tagfield::AlbumArtists}));
    }

    void anUntouchedTagSetReportsNothing()
    {
        TrackTags tags;
        tags.title = QStringLiteral("Café");
        tags.artists = {QStringLiteral("Björk")};
        tags.legacy8bit = {tagfield::Title, tagfield::Artists};
        QVERIFY(EncodingFixer(m_conv).fix(tags).repairedFields.isEmpty());
        QCOMPARE(tags.title, QStringLiteral("Café"));
    }
};

QTEST_GUILESS_MAIN(TstEncodingFixer)
#include "tst_encoding_fixer.moc"
