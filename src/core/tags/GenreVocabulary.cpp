#include "core/tags/GenreVocabulary.h"

#include "core/tags/GenreMap.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

namespace semplice {

namespace {

Error invalid(const QString& what)
{
    return Error::make(ErrorCode::InvalidArgument, QStringLiteral("genres.json: ") + what);
}

QStringList stringList(const QJsonValue& v)
{
    QStringList out;
    for (const QJsonValue& item : v.toArray())
        out << item.toString();
    return out;
}

}  // namespace

const QStringList& GenreVocabulary::languages()
{
    static const QStringList kLanguages = {QStringLiteral("en"), QStringLiteral("ru"), QStringLiteral("zh_CN"),
                                           QStringLiteral("ja")};
    return kLanguages;
}

Result<GenreVocabulary> GenreVocabulary::load(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return Error::make(ErrorCode::IoError, QStringLiteral("cannot read ") + path);
    auto v = fromJson(f.readAll());
    if (!v.ok() || path != QString::fromLatin1(kResourcePath))
        return v;
    GenreVocabulary vocabulary = std::move(v).value();
    // the application's vocabulary also has the languages and the moods (a test's own genre file does not need them)
    for (const auto& [file, prefix] : {std::pair{QString::fromLatin1(kLanguagesPath), QStringLiteral("lang:")}, std::pair{QString::fromLatin1(kMoodsPath), QStringLiteral("mood:")}}) {
        QFile d(file);
        if (!d.open(QIODevice::ReadOnly))
            return Error::make(ErrorCode::IoError, QStringLiteral("cannot read ") + file);
        if (auto r = vocabulary.addDescriptors(d.readAll(), prefix); !r.ok())
            return r.error();
    }
    return vocabulary;
}

Result<void> GenreVocabulary::addDescriptors(const QByteArray& json, const QString& prefix)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject() || doc.object().value(QStringLiteral("version")).toInt() != 1)
        return Error::make(ErrorCode::InvalidArgument, QStringLiteral("language or mood file: not version 1"));
    for (const QJsonValue& value : doc.object().value(QStringLiteral("tags")).toArray()) {
        const QJsonObject o = value.toObject();
        GenreInfo info;
        info.key = prefix + o.value(QStringLiteral("key")).toString();
        if (info.key.size() <= prefix.size() || m_descriptorIndex.contains(info.key))
            return Error::make(ErrorCode::InvalidArgument, QStringLiteral("language or mood file: bad or repeated key ") + info.key);
        const QJsonObject names = o.value(QStringLiteral("names")).toObject();
        for (const QString& language : languages()) {
            if (names.value(language).toString().isEmpty())
                return Error::make(ErrorCode::InvalidArgument, QStringLiteral("language or mood file: no ") + language + QStringLiteral(" name for ") + info.key);
            info.names.insert(language, names.value(language).toString());
        }
        m_descriptorIndex.insert(info.key, static_cast<int>(m_descriptors.size()));
        m_descriptors.append(info);
    }
    return {};
}

Result<GenreVocabulary> GenreVocabulary::fromJson(const QByteArray& json)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (!doc.isObject())
        return invalid(QStringLiteral("not a JSON object: ") + parseError.errorString());
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("version")).toInt() != 1)
        return invalid(QStringLiteral("unknown version"));

    GenreVocabulary v;
    static const QRegularExpression slug(QStringLiteral("^[a-z0-9]+(-[a-z0-9]+)*$"));

    for (const QJsonValue& gv : root.value(QStringLiteral("genres")).toArray()) {
        const QJsonObject o = gv.toObject();
        GenreInfo g;
        g.key = o.value(QStringLiteral("key")).toString();
        if (!slug.match(g.key).hasMatch())
            return invalid(QStringLiteral("bad key '%1' (lower-case slug expected)").arg(g.key));
        if (v.m_index.contains(g.key))
            return invalid(QStringLiteral("duplicate key '%1'").arg(g.key));
        const QJsonObject names = o.value(QStringLiteral("names")).toObject();
        for (const QString& lang : languages()) {
            const QString n = names.value(lang).toString().trimmed();
            if (n.isEmpty())
                return invalid(QStringLiteral("'%1' has no %2 name").arg(g.key, lang));
            g.names.insert(lang, n);
        }
        for (auto it = names.begin(); it != names.end(); ++it) {
            if (!languages().contains(it.key()))
                return invalid(QStringLiteral("'%1' has a name for unknown language '%2'").arg(g.key, it.key()));
        }
        g.aliases = stringList(o.value(QStringLiteral("aliases")));
        g.related = stringList(o.value(QStringLiteral("related")));
        g.opposite = stringList(o.value(QStringLiteral("opposite")));
        v.m_index.insert(g.key, static_cast<int>(v.m_genres.size()));
        v.m_genres.append(g);
    }
    if (v.m_genres.isEmpty())
        return invalid(QStringLiteral("no genres"));

    // relations: known keys, no self reference, symmetric, related and opposite disjoint
    for (const GenreInfo& g : std::as_const(v.m_genres)) {
        for (const char* kind : {"related", "opposite"}) {
            const bool isRelated = QLatin1String(kind) == QLatin1String("related");
            const QStringList& list = isRelated ? g.related : g.opposite;
            QSet<QString> seen;
            for (const QString& other : list) {
                const GenreInfo* o = v.find(other);
                if (!o)
                    return invalid(QStringLiteral("'%1' %2 unknown genre '%3'").arg(g.key, QLatin1String(kind), other));
                if (other == g.key)
                    return invalid(QStringLiteral("'%1' lists itself as %2").arg(g.key, QLatin1String(kind)));
                if (seen.contains(other))
                    return invalid(QStringLiteral("'%1' lists '%2' twice as %3").arg(g.key, other, QLatin1String(kind)));
                seen.insert(other);
                const QStringList& back = isRelated ? o->related : o->opposite;
                if (!back.contains(g.key))
                    return invalid(QStringLiteral("%1 is not symmetric: '%2' -> '%3' but not back").arg(QLatin1String(kind), g.key, other));
            }
        }
        for (const QString& r : g.related) {
            if (g.opposite.contains(r))
                return invalid(QStringLiteral("'%1' lists '%2' as both related and opposite").arg(g.key, r));
        }
    }

    // ID3v1 table: ids 0..191 in order, keys known or empty
    int expectedId = 0;
    for (const QJsonValue& iv : root.value(QStringLiteral("id3v1")).toArray()) {
        const QJsonObject o = iv.toObject();
        Id3v1Genre e;
        e.id = o.value(QStringLiteral("id")).toInt(-1);
        e.name = o.value(QStringLiteral("name")).toString();
        e.key = o.value(QStringLiteral("key")).toString();
        if (e.id != expectedId++)
            return invalid(QStringLiteral("id3v1 table: expected id %1, found %2").arg(expectedId - 1).arg(e.id));
        if (e.name.isEmpty())
            return invalid(QStringLiteral("id3v1 %1 has no name").arg(e.id));
        if (!e.key.isEmpty() && !v.contains(e.key))
            return invalid(QStringLiteral("id3v1 %1 '%2' maps to unknown genre '%3'").arg(e.id).arg(e.name, e.key));
        v.m_id3v1.append(e);
    }
    if (v.m_id3v1.size() != 192)
        return invalid(QStringLiteral("id3v1 table has %1 entries, 192 expected").arg(v.m_id3v1.size()));

    // Every spelling (names in all languages, aliases, ID3v1 names) must mean exactly one genre.
    QHash<QString, QString> owner;
    auto claim = [&](const QString& text, const QString& key) -> Result<void> {
        const QString n = GenreMap::normalise(text);
        if (n.isEmpty())
            return invalid(QStringLiteral("'%1' (%2) normalises to nothing").arg(text, key));
        const auto it = owner.constFind(n);
        if (it != owner.constEnd() && it.value() != key)
            return invalid(QStringLiteral("'%1' is claimed by both '%2' and '%3'").arg(text, it.value(), key));
        owner.insert(n, key);
        return {};
    };
    for (const GenreInfo& g : std::as_const(v.m_genres)) {
        if (auto r = claim(g.key, g.key); !r.ok())
            return r.error();
        for (const QString& n : g.names) {
            if (auto r = claim(n, g.key); !r.ok())
                return r.error();
        }
        for (const QString& a : g.aliases) {
            if (auto r = claim(a, g.key); !r.ok())
                return r.error();
        }
    }
    for (const Id3v1Genre& e : std::as_const(v.m_id3v1)) {
        if (e.key.isEmpty())
            continue;
        if (auto r = claim(e.name, e.key); !r.ok())
            return r.error();
    }
    return v;
}

const GenreInfo* GenreVocabulary::find(const QString& key) const
{
    if (const auto d = m_descriptorIndex.constFind(key); d != m_descriptorIndex.constEnd())
        return &m_descriptors.at(d.value());
    const auto it = m_index.constFind(key);
    return it == m_index.constEnd() ? nullptr : &m_genres.at(it.value());
}

QStringList GenreVocabulary::keys() const
{
    QStringList out;
    for (const GenreInfo& g : m_genres)
        out << g.key;
    return out;
}

QStringList GenreVocabulary::languageKeys() const
{
    QStringList out;
    for (const GenreInfo& g : m_descriptors)
        if (isLanguageKey(g.key))
            out << g.key;
    return out;
}

QStringList GenreVocabulary::moodKeys() const
{
    QStringList out;
    for (const GenreInfo& g : m_descriptors)
        if (isMoodKey(g.key))
            out << g.key;
    return out;
}

QStringList GenreVocabulary::allKeys() const
{
    QStringList out = keys();
    for (const GenreInfo& g : m_descriptors)
        out << g.key;
    return out;
}

QString GenreVocabulary::name(const QString& key, const QString& language) const
{
    const GenreInfo* g = find(key);
    if (!g)
        return key;
    const auto it = g->names.constFind(language);
    return it != g->names.constEnd() ? it.value() : g->names.value(QStringLiteral("en"), key);
}

QStringList GenreVocabulary::related(const QString& key) const
{
    const GenreInfo* g = find(key);
    return g ? g->related : QStringList();
}

QStringList GenreVocabulary::opposite(const QString& key) const
{
    const GenreInfo* g = find(key);
    return g ? g->opposite : QStringList();
}

}  // namespace semplice
