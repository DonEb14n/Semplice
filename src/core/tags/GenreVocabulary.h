#pragma once

#include "core/services/TagVocabulary.h"
#include "core/util/Result.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace semplice {

struct GenreInfo {
    QString key;                   // canonical English slug, e.g. "hip-hop"; stored in the database, never changes
    QHash<QString, QString> names; // "en", "ru", "zh_CN", "ja"
    QStringList aliases;           // other spellings found in tags
    QStringList related;           // keys; symmetric
    QStringList opposite;          // keys; symmetric
};

// One row of the ID3v1 genre table (0-191, Winamp extensions included).
struct Id3v1Genre {
    int id = 0;
    QString name;
    QString key;  // empty = no sensible vocabulary genre ("Other", "Pranks", ...)
};

// The closed tag vocabulary (SPEC 5.4), loaded from resources/tags/genres.json.
class GenreVocabulary {
public:
    static constexpr const char* kResourcePath = ":/semplice/tags/genres.json";
    static const QStringList& languages();  // en, ru, zh_CN, ja
    // Tags that are not genres: the language of the lyrics ("lang:en") and the mood ("mood:sad"). Stored like genre keys, never written
    // into the genre of a file.
    static constexpr const char* kLanguagesPath = ":/semplice/tags/languages.json";
    static constexpr const char* kMoodsPath = ":/semplice/tags/moods.json";
    static bool isGenreKey(const QString& key) { return !key.contains(QLatin1Char(':')); }
    static bool isLanguageKey(const QString& key) { return key.startsWith(QStringLiteral("lang:")); }
    static bool isMoodKey(const QString& key) { return key.startsWith(QStringLiteral("mood:")); }

    // Loads and VALIDATES. A file that breaks any rule is refused with a message naming the problem.
    static Result<GenreVocabulary> load(const QString& path = QString::fromLatin1(kResourcePath));
    static Result<GenreVocabulary> fromJson(const QByteArray& json);

    const QList<GenreInfo>& genres() const { return m_genres; }
    const QList<Id3v1Genre>& id3v1() const { return m_id3v1; }
    bool contains(const QString& key) const { return m_index.contains(key) || m_descriptorIndex.contains(key); }
    const GenreInfo* find(const QString& key) const;
    QStringList keys() const;
    QStringList languageKeys() const;   // "lang:en", ...
    QStringList moodKeys() const;       // "mood:sad", ...
    QStringList allKeys() const;        // genres, languages and moods: everything a tag may be

    // Localised name; falls back to English for an unknown language, and to the key for an unknown genre.
    QString name(const QString& key, const QString& language) const;
    QStringList related(const QString& key) const;
    QStringList opposite(const QString& key) const;

    TagVocabulary toTagVocabulary() const { return TagVocabulary(allKeys()); }

private:
    // reads a language or mood file (version 1, "tags": [{ key, names }]) into m_descriptors, keys get `prefix`
    Result<void> addDescriptors(const QByteArray& json, const QString& prefix);
    QList<GenreInfo> m_genres;
    QList<Id3v1Genre> m_id3v1;
    QHash<QString, int> m_index;
    QList<GenreInfo> m_descriptors;          // languages and moods, keys with their prefix
    QHash<QString, int> m_descriptorIndex;
};

}  // namespace semplice
