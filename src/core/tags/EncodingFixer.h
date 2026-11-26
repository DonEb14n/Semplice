#pragma once

#include "core/tags/TrackTags.h"

#include <QList>
#include <QString>

#include <optional>

namespace semplice {

enum class CodePage { Utf8, Cp1251, Cp936 /*GBK*/, Cp932 /*Shift-JIS*/, Cp949 /*EUC-KR*/, Cp950 /*Big5*/ };

// Byte <-> text conversion for the legacy Windows code pages (SPEC 6.3). The Windows implementation
// (platform/) uses MultiByteToWideChar; Qt itself has no such codecs without ICU. Tests use a table fake.
class ICodepageConverter {
public:
    virtual ~ICodepageConverter() = default;
    // std::nullopt if the bytes are not valid in that code page.
    virtual std::optional<QString> decode(const QByteArray& bytes, CodePage page) const = 0;
    // std::nullopt if a character cannot be represented. Used to build test fixtures.
    virtual std::optional<QByteArray> encode(const QString& text, CodePage page) const = 0;
};

struct FixCandidate {
    CodePage page = CodePage::Utf8;
    QString text;
    double score = 0;  // higher = more plausible
};

struct FixResult {
    QString text;                      // the repaired text, or the input if nothing plausible was found
    bool changed = false;
    std::optional<CodePage> page;      // the code page that was applied
    QList<FixCandidate> alternatives;  // the other plausible readings, best first (for the UI)
};

// Repairs "mojibake": text that was written in a legacy code page but labelled ISO-8859-1 (SPEC 6.3).
//
// Only text whose characters are all below U+0100 is considered (anything else is already real
// Unicode), and only if it looks broken: at least two non-ASCII letters making up 60 % or more of
// its letters. Correct Western text ("Café", "Björk", "Ñandú") therefore never changes.
// Known limit: Han-only text can be valid in several Chinese/Japanese/Korean code pages (and Hangul
// bytes are valid GBK); the first plausible page in `order` wins and the rest are reported as alternatives.
class EncodingFixer {
public:
    static QList<CodePage> defaultOrder();
    explicit EncodingFixer(const ICodepageConverter& converter, QList<CodePage> order = defaultOrder());

    FixResult fixText(const QString& text) const;

    struct Report {
        QStringList repairedFields;  // names from tagfield::*
    };
    // Repairs every field flagged in tags.legacy8bit; UTF-8/UTF-16 fields are trusted as they are.
    Report fix(TrackTags& tags) const;

private:
    const ICodepageConverter& m_converter;
    QList<CodePage> m_order;
};

}  // namespace semplice
