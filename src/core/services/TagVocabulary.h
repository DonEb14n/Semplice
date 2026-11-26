#pragma once

#include <QSet>
#include <QString>
#include <QStringList>

namespace semplice {

// The closed set of tag keys (SPEC 5.4). M3 loads it from resources/tags/genres.json;
// M1 only needs "is this key allowed".
class TagVocabulary {
public:
    TagVocabulary() = default;
    explicit TagVocabulary(const QStringList& keys) : m_keys(keys.begin(), keys.end()) {}

    bool contains(const QString& key) const { return m_keys.contains(key); }
    // First key that is not in the vocabulary, or an empty string if all are known.
    QString firstUnknown(const QStringList& keys) const
    {
        for (const QString& k : keys) {
            if (!m_keys.contains(k))
                return k;
        }
        return {};
    }

private:
    QSet<QString> m_keys;
};

}  // namespace semplice
