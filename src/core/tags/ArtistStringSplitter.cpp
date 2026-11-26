#include "core/tags/ArtistStringSplitter.h"

#include "core/util/Normalizer.h"

#include <QRegularExpression>

namespace semplice {

namespace {

QStringList splitPart(const QString& part, const ArtistStringSplitter::Exists& exists)
{
    const QString whole = part.trimmed();
    if (whole.isEmpty())
        return {};
    if (exists(whole))
        return {whole};
    QStringList out;
    for (const QString& piece : whole.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const QString p = piece.trimmed();
        if (p.isEmpty())
            continue;
        if (exists(p)) {
            out << p;
            continue;
        }
        static const QRegularExpression comma(QStringLiteral(R"(\s*,\s*)"));
        static const QRegularExpression ampersand(QStringLiteral(R"(\s+&\s+)"));
        for (const QString& commaPiece : p.split(comma, Qt::SkipEmptyParts)) {
            const QString c = commaPiece.trimmed();
            if (c.isEmpty())
                continue;
            if (exists(c)) {
                out << c;
                continue;
            }
            for (const QString& sub : c.split(ampersand, Qt::SkipEmptyParts)) {
                const QString s = sub.trimmed();
                if (!s.isEmpty())
                    out << s;
            }
        }
    }
    return out;
}

}  // namespace

QList<NamedCredit> ArtistStringSplitter::split(const QString& text, const Exists& exists)
{
    const QString t = text.trimmed();
    if (t.isEmpty())
        return {};
    if (exists(t))
        return {{t, CreditRole::Main}};

    // "Main Artist (feat. Guest)", "A ft. B & C", "A featuring B"
    static const QRegularExpression feat(QStringLiteral(R"(\s+[\(\[]?\s*(?:feat\.?|ft\.?|featuring)\s+)"),
                                         QRegularExpression::CaseInsensitiveOption);
    QString mainPart = t;
    QString featPart;
    if (const auto m = feat.match(t); m.hasMatch()) {
        mainPart = t.left(m.capturedStart());
        featPart = t.mid(m.capturedEnd());
        while (featPart.endsWith(QLatin1Char(')')) || featPart.endsWith(QLatin1Char(']')))
            featPart.chop(1);
    }

    QList<NamedCredit> out;
    for (const QString& name : splitPart(mainPart, exists))
        out.append({name, CreditRole::Main});
    for (const QString& name : splitPart(featPart, exists))
        out.append({name, CreditRole::Feat});
    return out;
}

QList<NamedCredit> ArtistStringSplitter::splitAll(const QStringList& values, const Exists& exists)
{
    QList<NamedCredit> out;
    QStringList seen;
    for (const QString& value : values) {
        for (const NamedCredit& c : split(value, exists)) {
            const QString key = Normalizer::identityKey(c.name);
            if (seen.contains(key))
                continue;
            seen << key;
            out.append(c);
        }
    }
    return out;
}

}  // namespace semplice
