#pragma once

#include <QString>

namespace semplice {

// Text normalisation shared by uniqueness checks, sorting and search (SPEC 3, 4.1, 5.1).
class Normalizer {
public:
    // What is shown and stored: trimmed, NFC.
    static QString display(const QString& text);

    // Identity for uniqueness: trimmed, NFC, runs of whitespace collapsed to one space, case-folded.
    // Diacritics are kept ("Beyonce" and "Beyoncé" are different names).
    static QString identityKey(const QString& text);

    // For the search box: identityKey with combining diacritical marks (U+0300..U+036F) removed.
    // Japanese voiced marks and Hangul are untouched. Cyrillic "й"/"ё" fold to "и"/"е".
    static QString searchKey(const QString& text);
};

}  // namespace semplice
