// The .ts files are the owner's review copy of the machine drafts: nothing may be left untranslated and every
// plural message needs the right number of forms (en 2, ru 3, zh_CN 1, ja 1).
#include <QFile>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <QtTest>

class TstTranslations : public QObject {
    Q_OBJECT

    struct Result {
        int messages = 0;
        QStringList unfinished;
        QStringList badForms;
        QStringList empty;
        QStringList garbled;   // UTF-8 text that was read as Latin-1 ("Ð¢Ñ..." instead of Cyrillic)
    };

    static Result scan(const QString& lang, int forms)
    {
        Result r;
        QFile f(QStringLiteral(SEMPLICE_SOURCE_DIR "/translations/app_%1.ts").arg(lang));
        if (!f.open(QIODevice::ReadOnly))
            return r;
        QXmlStreamReader xml(&f);
        QString source;
        bool numerus = false;
        while (!xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement())
                continue;
            if (xml.name() == u"message") {
                numerus = xml.attributes().value(QStringLiteral("numerus")) == u"yes";
                source.clear();
            } else if (xml.name() == u"source") {
                source = xml.readElementText();
            } else if (xml.name() == u"translation") {
                const QString type = xml.attributes().value(QStringLiteral("type")).toString();
                int n = 0;
                QString text;
                while (!(xml.isEndElement() && xml.name() == u"translation") && !xml.atEnd()) {
                    xml.readNext();
                    if (xml.isStartElement() && xml.name() == u"numerusform") {
                        ++n;
                        text += xml.readElementText();
                    } else if (xml.isCharacters()) {
                        text += xml.text();
                    }
                }
                if (type == u"obsolete" || type == u"vanished")
                    continue;
                ++r.messages;
                if (type == u"unfinished")
                    r.unfinished << source;
                else if (numerus && n != forms)
                    r.badForms << source;
                else if (text.trimmed().isEmpty())
                    r.empty << source;
                if (QRegularExpression(QStringLiteral("[\x{00C0}-\x{00FF}][\x{0080}-\x{00BF}]")).match(text).hasMatch())
                    r.garbled << source;
            }
        }
        return r;
    }

private slots:
    void everyLanguageIsComplete_data()
    {
        QTest::addColumn<QString>("lang");
        QTest::addColumn<int>("forms");
        QTest::newRow("en") << "en" << 2;
        QTest::newRow("ru") << "ru" << 3;
        QTest::newRow("zh_CN") << "zh_CN" << 1;
        QTest::newRow("ja") << "ja" << 1;
    }
    void everyLanguageIsComplete()
    {
        QFETCH(QString, lang);
        QFETCH(int, forms);
        const Result r = scan(lang, forms);
        QVERIFY2(r.messages > 100, "the .ts file was not read");
        QVERIFY2(r.unfinished.isEmpty(), qPrintable(QStringLiteral("untranslated: ") + r.unfinished.join(QStringLiteral(" | "))));
        QVERIFY2(r.badForms.isEmpty(), qPrintable(QStringLiteral("wrong number of plural forms: ") + r.badForms.join(QStringLiteral(" | "))));
        QVERIFY2(r.empty.isEmpty(), qPrintable(QStringLiteral("empty translation: ") + r.empty.join(QStringLiteral(" | "))));
        QVERIFY2(r.garbled.isEmpty(), qPrintable(QStringLiteral("garbled (wrong text encoding): ") + r.garbled.join(QStringLiteral(" | "))));
    }
};

QTEST_MAIN(TstTranslations)
#include "tst_translations.moc"
