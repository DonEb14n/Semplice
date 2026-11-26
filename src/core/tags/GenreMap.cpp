#include "core/tags/GenreMap.h"

#include "core/util/Normalizer.h"

#include <QRegularExpression>

namespace semplice {

QString GenreMap::normalise(const QString& text)
{
    QString s = Normalizer::searchKey(text);
    s.replace(QLatin1Char('&'), QStringLiteral(" and "));
    QString out;
    out.reserve(s.size());
    bool lastSpace = true;
    for (const QChar c : std::as_const(s)) {
        if (c.isLetterOrNumber()) {
            out.append(c);
            lastSpace = false;
        } else if (!lastSpace) {
            out.append(QLatin1Char(' '));
            lastSpace = true;
        }
    }
    return out.trimmed();
}

GenreMap::GenreMap(const GenreVocabulary& vocabulary)
{
    for (const GenreInfo& g : vocabulary.genres()) {
        m_byName.insert(normalise(g.key), g.key);
        for (const QString& n : g.names)
            m_byName.insert(normalise(n), g.key);
        for (const QString& a : g.aliases)
            m_byName.insert(normalise(a), g.key);
    }
    for (const Id3v1Genre& e : vocabulary.id3v1()) {
        if (e.key.isEmpty())
            continue;
        m_byId.insert(e.id, e.key);
        m_byName.insert(normalise(e.name), e.key);
    }
}

std::optional<QString> GenreMap::fromId3v1(int id) const
{
    const auto it = m_byId.constFind(id);
    if (it == m_byId.constEnd())
        return std::nullopt;
    return it.value();
}

QStringList GenreMap::resolveAll(const QStringList& raw) const
{
    QStringList out;
    for (const QString& r : raw) {
        for (const QString& key : resolve(r)) {
            if (!out.contains(key))
                out << key;
        }
    }
    return out;
}

QStringList GenreMap::resolve(const QString& raw) const
{
    return resolveOnce(raw, 0);
}

QStringList GenreMap::resolveOnce(const QString& text, int splitDepth) const
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return {};

    // 1. the whole text is a known spelling
    const QString norm = normalise(trimmed);
    if (const auto it = m_byName.constFind(norm); it != m_byName.constEnd())
        return {it.value()};

    // 2. "17" or "(17)"
    static const QRegularExpression bare(QStringLiteral(R"(^\(?\s*(\d{1,3})\s*\)?$)"));
    if (const auto m = bare.match(trimmed); m.hasMatch()) {
        if (const auto key = fromId3v1(m.captured(1).toInt()))
            return {*key};
        return {};
    }

    // 3. ID3v2.3 style "(17)(13)Rock": numbers first, then optional free text
    static const QRegularExpression refs(QStringLiteral(R"(^((?:\(\d{1,3}\))+)(.*)$)"));
    if (const auto m = refs.match(trimmed); m.hasMatch()) {
        QStringList out;
        static const QRegularExpression number(QStringLiteral(R"(\((\d{1,3})\))"));
        for (auto it = number.globalMatch(m.captured(1)); it.hasNext();) {
            if (const auto key = fromId3v1(it.next().captured(1).toInt()); key && !out.contains(*key))
                out << *key;
        }
        const QString rest = m.captured(2).trimmed();
        if (!rest.isEmpty() && splitDepth < 2) {
            for (const QString& key : resolveOnce(rest, splitDepth + 1)) {
                if (!out.contains(key))
                    out << key;
            }
        }
        return out;
    }

    // 4. a list: "Rock; Pop", "Rock/Pop", "Rock, Pop & Soul"
    if (splitDepth < 2) {
        static const QRegularExpression separators(QStringLiteral(R"([;/,|+]|\s&\s|\x00)"));
        const QStringList parts = trimmed.split(separators, Qt::SkipEmptyParts);
        if (parts.size() > 1) {
            QStringList out;
            for (const QString& part : parts) {
                for (const QString& key : resolveOnce(part, splitDepth + 1)) {
                    if (!out.contains(key))
                        out << key;
                }
            }
            return out;
        }
    }
    return {};
}

}  // namespace semplice
