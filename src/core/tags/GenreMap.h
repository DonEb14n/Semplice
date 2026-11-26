#pragma once

#include "core/tags/GenreVocabulary.h"

#include <QHash>
#include <optional>

namespace semplice {

// Turns the genre text found in tag metadata into vocabulary keys (SPEC 5.4). Unknown text is
// ignored: it never creates a tag.
class GenreMap {
public:
    explicit GenreMap(const GenreVocabulary& vocabulary);

    // Case, diacritics and punctuation insensitive form used for lookups: "Hip-Hop/Rap" -> "hip hop rap",
    // "R&B" -> "r and b".
    static QString normalise(const QString& text);

    // One raw tag string -> zero or more keys. Understands names in any of the four languages,
    // aliases, "17", "(17)", "(17)Rock", and lists separated by ; / , | + or " & ".
    QStringList resolve(const QString& raw) const;
    QStringList resolveAll(const QStringList& raw) const;  // union, order kept, no duplicates

    std::optional<QString> fromId3v1(int id) const;  // none for unknown ids and for "Other"/"Pranks"/...

private:
    QStringList resolveOnce(const QString& text, int splitDepth) const;

    QHash<QString, QString> m_byName;  // normalised spelling -> key
    QHash<int, QString> m_byId;
};

}  // namespace semplice
