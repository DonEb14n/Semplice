#pragma once

#include "core/domain/Types.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace semplice {

struct NamedCredit {
    QString name;
    CreditRole role = CreditRole::Main;
    bool operator==(const NamedCredit&) const = default;
};

// Splits the artist text of a tag into credits (SPEC 5.3, DEFAULT rule):
//  * `;`, `,`, " feat. ", " ft. ", " featuring " (also "(feat. X)" / "[ft. X]") and " & " separate artists;
//  * artists before the feat separator are `main`, those after it `feat`;
//  * a part that already is an existing artist is never split ("Simon & Garfunkel" stays whole once it exists);
//  * `,` also separates artists (owner decision, 2026-10); `/` is never split (AC/DC); a name that already exists stays whole;
// The result is only a proposal: the import form shows the credits so the user can correct them.
class ArtistStringSplitter {
public:
    using Exists = std::function<bool(const QString& name)>;

    static QList<NamedCredit> split(const QString& text, const Exists& artistExists);
    // Several values of one field (multi-value tags): each is split, duplicates dropped (case-insensitive).
    static QList<NamedCredit> splitAll(const QStringList& values, const Exists& artistExists);
};

}  // namespace semplice
