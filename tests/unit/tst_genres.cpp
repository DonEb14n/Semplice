#include "core/tags/GenreMap.h"
#include "core/tags/GenreVocabulary.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace semplice;

class TstGenres : public QObject {
    Q_OBJECT

    static QByteArray baseJson() { return readFile(QString::fromLatin1(GenreVocabulary::kResourcePath)); }
    static QByteArray readFile(const QString& path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }
    // Applies a change to the shipped file and returns the result of loading it.
    static Result<GenreVocabulary> loadModified(const std::function<void(QJsonObject&)>& change)
    {
        QJsonObject root = QJsonDocument::fromJson(baseJson()).object();
        change(root);
        return GenreVocabulary::fromJson(QJsonDocument(root).toJson());
    }
    static QJsonObject genre(QJsonObject& root, const QString& key)
    {
        for (const QJsonValue& v : root.value(QStringLiteral("genres")).toArray()) {
            if (v.toObject().value(QStringLiteral("key")).toString() == key)
                return v.toObject();
        }
        return {};
    }
    static void replaceGenre(QJsonObject& root, const QString& key, const QJsonObject& updated)
    {
        QJsonArray list = root.value(QStringLiteral("genres")).toArray();
        for (int i = 0; i < list.size(); ++i) {
            if (list[i].toObject().value(QStringLiteral("key")).toString() == key)
                list[i] = updated;
        }
        root.insert(QStringLiteral("genres"), list);
    }

private slots:
    // ================================================================ the shipped file
    void languagesAndMoodsAreTagsButNotGenres()
    {
        const auto v = GenreVocabulary::load();
        QVERIFY(v.ok());
        const GenreVocabulary& vocab = v.value();
        QVERIFY(vocab.languageKeys().contains(QStringLiteral("lang:en")));
        QVERIFY(vocab.moodKeys().contains(QStringLiteral("mood:sad")));
        QVERIFY(vocab.toTagVocabulary().firstUnknown({QStringLiteral("lang:ru"), QStringLiteral("mood:cheerful"), QStringLiteral("rock")}).isEmpty());
        QVERIFY(!vocab.keys().contains(QStringLiteral("lang:en")));   // the genre list stays genres only
        QCOMPARE(vocab.name(QStringLiteral("lang:en"), QStringLiteral("ru")), QStringLiteral("Английский"));
        QCOMPARE(vocab.name(QStringLiteral("mood:sad"), QStringLiteral("en")), QStringLiteral("Sad"));
        QVERIFY(!GenreVocabulary::isGenreKey(QStringLiteral("mood:sad")));
        QVERIFY(GenreVocabulary::isGenreKey(QStringLiteral("rock")));
    }

    void theShippedVocabularyLoadsAndIsValid()
    {
        const auto v = GenreVocabulary::load();
        QVERIFY2(v.ok(), v.ok() ? "" : qPrintable(v.error().devMessage));
        const int n = static_cast<int>(v.value().genres().size());
        QVERIFY2(n >= 60 && n <= 80, qPrintable(QString::number(n)));  // plan: ~60-80 genres
        QCOMPARE(v.value().id3v1().size(), qsizetype(192));
    }

    void everyGenreHasNamesInAllFourLanguages()
    {
        const auto v = GenreVocabulary::load().value();
        for (const GenreInfo& g : v.genres()) {
            for (const QString& lang : GenreVocabulary::languages())
                QVERIFY2(!v.name(g.key, lang).isEmpty() && g.names.contains(lang), qPrintable(g.key + " " + lang));
        }
        QCOMPARE(v.name(QStringLiteral("hip-hop"), QStringLiteral("ru")), QStringLiteral("Хип-хоп"));
        QCOMPARE(v.name(QStringLiteral("hip-hop"), QStringLiteral("xx")), QStringLiteral("Hip-Hop"));  // unknown language -> English
        QCOMPARE(v.name(QStringLiteral("no-such-genre"), QStringLiteral("en")), QStringLiteral("no-such-genre"));
    }

    void relationsAreSymmetricAndSane()
    {
        const auto v = GenreVocabulary::load().value();
        int related = 0;
        int opposite = 0;
        for (const GenreInfo& g : v.genres()) {
            for (const QString& r : g.related) {
                QVERIFY2(v.related(r).contains(g.key), qPrintable(g.key + " -> " + r));
                QVERIFY(!g.opposite.contains(r));
                QVERIFY(r != g.key);
                ++related;
            }
            for (const QString& o : g.opposite) {
                QVERIFY2(v.opposite(o).contains(g.key), qPrintable(g.key + " -> " + o));
                ++opposite;
            }
        }
        QVERIFY(related > 80);   // the recommender has something to work with
        QVERIFY(opposite > 20);
        QVERIFY(v.related(QStringLiteral("rock")).contains(QStringLiteral("hard-rock")));
        QVERIFY(v.opposite(QStringLiteral("classical")).contains(QStringLiteral("death-metal")));
    }

    void keysAreStableSlugsAndUsableAsTheTagVocabulary()
    {
        const auto v = GenreVocabulary::load().value();
        const TagVocabulary tags = v.toTagVocabulary();
        QVERIFY(tags.contains(QStringLiteral("rock")));
        QVERIFY(tags.contains(QStringLiteral("hip-hop")));
        QVERIFY(!tags.contains(QStringLiteral("Rock")));  // keys are lower-case slugs
        QCOMPARE(tags.firstUnknown({QStringLiteral("rock"), QStringLiteral("vaporwave")}), QStringLiteral("vaporwave"));
    }

    // ================================================================ the validator itself
    void theValidatorRejectsBrokenFiles()
    {
        // asymmetric related
        auto r = loadModified([](QJsonObject& root) {
            QJsonObject g = genre(root, QStringLiteral("rock"));
            QJsonArray rel = g.value(QStringLiteral("related")).toArray();
            rel.append(QStringLiteral("opera"));  // opera does not list rock
            g.insert(QStringLiteral("related"), rel);
            replaceGenre(root, QStringLiteral("rock"), g);
        });
        QVERIFY(!r.ok());
        QVERIFY2(r.error().devMessage.contains(QStringLiteral("symmetric")), qPrintable(r.error().devMessage));

        // unknown related key
        r = loadModified([](QJsonObject& root) {
            QJsonObject g = genre(root, QStringLiteral("rock"));
            g.insert(QStringLiteral("related"), QJsonArray{QStringLiteral("no-such-genre")});
            replaceGenre(root, QStringLiteral("rock"), g);
        });
        QVERIFY(!r.ok());

        // missing translation
        r = loadModified([](QJsonObject& root) {
            QJsonObject g = genre(root, QStringLiteral("jazz"));
            QJsonObject names = g.value(QStringLiteral("names")).toObject();
            names.remove(QStringLiteral("ja"));
            g.insert(QStringLiteral("names"), names);
            replaceGenre(root, QStringLiteral("jazz"), g);
        });
        QVERIFY(!r.ok());
        QVERIFY(r.error().devMessage.contains(QStringLiteral("ja")));

        // an alias that already means another genre
        r = loadModified([](QJsonObject& root) {
            QJsonObject g = genre(root, QStringLiteral("jazz"));
            QJsonArray aliases = g.value(QStringLiteral("aliases")).toArray();
            aliases.append(QStringLiteral("Hip Hop"));
            g.insert(QStringLiteral("aliases"), aliases);
            replaceGenre(root, QStringLiteral("jazz"), g);
        });
        QVERIFY(!r.ok());
        QVERIFY(r.error().devMessage.contains(QStringLiteral("claimed")));

        // bad key
        r = loadModified([](QJsonObject& root) {
            QJsonObject g = genre(root, QStringLiteral("jazz"));
            g.insert(QStringLiteral("key"), QStringLiteral("Jazz Music"));
            replaceGenre(root, QStringLiteral("jazz"), g);
        });
        QVERIFY(!r.ok());

        // duplicate key
        r = loadModified([](QJsonObject& root) {
            QJsonArray list = root.value(QStringLiteral("genres")).toArray();
            list.append(list.at(0));
            root.insert(QStringLiteral("genres"), list);
        });
        QVERIFY(!r.ok());

        // related and opposite at once
        r = loadModified([](QJsonObject& root) {
            for (const QString& key : {QStringLiteral("rock"), QStringLiteral("hard-rock")}) {
                QJsonObject g = genre(root, key);
                g.insert(QStringLiteral("opposite"), QJsonArray{key == QLatin1String("rock") ? QStringLiteral("hard-rock") : QStringLiteral("rock")});
                replaceGenre(root, key, g);
            }
        });
        QVERIFY(!r.ok());

        // ID3v1 table of the wrong size / unknown key
        r = loadModified([](QJsonObject& root) {
            QJsonArray t = root.value(QStringLiteral("id3v1")).toArray();
            t.removeLast();
            root.insert(QStringLiteral("id3v1"), t);
        });
        QVERIFY(!r.ok());
        r = loadModified([](QJsonObject& root) {
            QJsonArray t = root.value(QStringLiteral("id3v1")).toArray();
            QJsonObject e = t.at(7).toObject();
            e.insert(QStringLiteral("key"), QStringLiteral("nope"));
            t[7] = e;
            root.insert(QStringLiteral("id3v1"), t);
        });
        QVERIFY(!r.ok());

        QVERIFY(!GenreVocabulary::fromJson("not json").ok());
        QVERIFY(!GenreVocabulary::load(QStringLiteral("C:/definitely/not/here.json")).ok());
    }

    // ================================================================ GenreMap
    void normaliseIgnoresCaseDiacriticsAndPunctuation()
    {
        QCOMPARE(GenreMap::normalise(QStringLiteral("Hip-Hop/Rap")), QStringLiteral("hip hop rap"));
        QCOMPARE(GenreMap::normalise(QStringLiteral("  R&B ")), QStringLiteral("r and b"));
        QCOMPARE(GenreMap::normalise(QStringLiteral("Électro")), QStringLiteral("electro"));
        QCOMPARE(GenreMap::normalise(QStringLiteral("摇滚")), QStringLiteral("摇滚"));
        QCOMPARE(GenreMap::normalise(QStringLiteral("!!!")), QString());
    }

    void namesAliasesAndSpellingsResolve_data()
    {
        QTest::addColumn<QString>("raw");
        QTest::addColumn<QStringList>("keys");
        auto row = [](const char* raw, QStringList keys) { QTest::newRow(raw) << QString::fromUtf8(raw) << keys; };
        row("Rock", {"rock"});
        row("ROCK", {"rock"});
        row("  rock  ", {"rock"});
        row("Hip-Hop", {"hip-hop"});
        row("Hip Hop", {"hip-hop"});
        row("hip_hop", {"hip-hop"});
        row("HipHop", {"hip-hop"});
        row("Hip-Hop/Rap", {"hip-hop"});
        row("R&B", {"r-and-b"});
        row("RnB", {"r-and-b"});
        row("R & B", {"r-and-b"});
        row("Rhythm and Blues", {"r-and-b"});
        row("Rock & Roll", {"rock"});
        row("Rock'n'Roll", {"rock"});
        row("Drum & Bass", {"drum-and-bass"});
        row("DnB", {"drum-and-bass"});
        row("Electronica", {"electronic"});
        row("Классическая", {"classical"});
        row("рок", {"rock"});
        row("摇滚", {"rock"});
        row("ロック", {"rock"});
        row("ヒップホップ", {"hip-hop"});
        row("Хип-Хоп", {"hip-hop"});
        row("Soundtrack", {"soundtrack"});
        row("OST", {"soundtrack"});
        row("Alternative", {"alternative-rock"});
        row("Alt Rock", {"alternative-rock"});
        row("Post Rock", {"post-rock"});
        row("Café del Mar", {});          // not a genre
        row("Vaporwave", {});             // not in the closed vocabulary: ignored
        row("", {});
        row("???", {});
    }
    void namesAliasesAndSpellingsResolve()
    {
        QFETCH(QString, raw);
        QFETCH(QStringList, keys);
        static const GenreVocabulary vocabulary = GenreVocabulary::load().value();
        static const GenreMap map(vocabulary);
        QCOMPARE(map.resolve(raw), keys);
    }

    void numbersAndListsResolve()
    {
        const GenreVocabulary vocabulary = GenreVocabulary::load().value();
        const GenreMap map(vocabulary);
        QCOMPARE(map.resolve(QStringLiteral("17")), QStringList{QStringLiteral("rock")});
        QCOMPARE(map.resolve(QStringLiteral("(17)")), QStringList{QStringLiteral("rock")});
        QCOMPARE(map.resolve(QStringLiteral("(17)Rock")), QStringList{QStringLiteral("rock")});
        QCOMPARE(map.resolve(QStringLiteral("(17)(13)")), (QStringList{QStringLiteral("rock"), QStringLiteral("pop")}));
        QCOMPARE(map.resolve(QStringLiteral("(13)Something odd")), QStringList{QStringLiteral("pop")});
        QCOMPARE(map.resolve(QStringLiteral("255")), QStringList());
        QCOMPARE(map.resolve(QStringLiteral("(255)")), QStringList());
        QCOMPARE(map.resolve(QStringLiteral("12")), QStringList());  // "Other"

        QCOMPARE(map.resolve(QStringLiteral("Rock; Pop")), (QStringList{QStringLiteral("rock"), QStringLiteral("pop")}));
        QCOMPARE(map.resolve(QStringLiteral("Rock/Pop")), (QStringList{QStringLiteral("rock"), QStringLiteral("pop")}));
        QCOMPARE(map.resolve(QStringLiteral("Rock, Pop & Soul")),
                 (QStringList{QStringLiteral("rock"), QStringLiteral("pop"), QStringLiteral("soul")}));
        QCOMPARE(map.resolve(QStringLiteral("Rock; Vaporwave; Jazz")), (QStringList{QStringLiteral("rock"), QStringLiteral("jazz")}));
        QCOMPARE(map.resolve(QStringLiteral("Rock;Rock")), QStringList{QStringLiteral("rock")});  // no duplicates
        QCOMPARE(map.resolveAll({QStringLiteral("Rock"), QStringLiteral("17"), QStringLiteral("Pop")}),
                 (QStringList{QStringLiteral("rock"), QStringLiteral("pop")}));
    }

    void everyNameAliasAndId3EntryResolvesToItsGenre()
    {
        const GenreVocabulary vocabulary = GenreVocabulary::load().value();
        const GenreMap map(vocabulary);
        for (const GenreInfo& g : vocabulary.genres()) {
            QCOMPARE(map.resolve(g.key), QStringList{g.key});
            for (const QString& n : g.names)
                QVERIFY2(map.resolve(n) == QStringList{g.key}, qPrintable(g.key + " name " + n));
            for (const QString& a : g.aliases)
                QVERIFY2(map.resolve(a) == QStringList{g.key}, qPrintable(g.key + " alias " + a));
        }
        int mapped = 0;
        for (const Id3v1Genre& e : vocabulary.id3v1()) {
            const auto key = map.fromId3v1(e.id);
            QCOMPARE(key.has_value(), !e.key.isEmpty());
            if (key) {
                QCOMPARE(*key, e.key);
                QCOMPARE(map.resolve(QString::number(e.id)), QStringList{e.key});
                QCOMPARE(map.resolve(e.name), QStringList{e.key});  // the textual ID3v1 name works too
                ++mapped;
            }
        }
        QVERIFY(mapped > 150);
        QVERIFY(!map.fromId3v1(-1).has_value());
        QVERIFY(!map.fromId3v1(192).has_value());
        QVERIFY(!map.fromId3v1(255).has_value());
    }

    void theWellKnownId3v1NumbersMapAsExpected()
    {
        const GenreMap map(GenreVocabulary::load().value());
        const QList<QPair<int, QString>> expected = {{0, "blues"}, {2, "country"}, {7, "hip-hop"}, {8, "jazz"}, {9, "metal"},
                                                     {13, "pop"}, {14, "r-and-b"}, {17, "rock"}, {32, "classical"},
                                                     {52, "electronic"}, {79, "hard-rock"}, {137, "heavy-metal"},
                                                     {144, "thrash-metal"}, {189, "dubstep"}};
        for (const auto& [id, key] : expected)
            QCOMPARE(map.fromId3v1(id).value_or(QString()), key);
    }
};

QTEST_GUILESS_MAIN(TstGenres)
#include "tst_genres.moc"
