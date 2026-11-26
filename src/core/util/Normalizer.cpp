#include "core/util/Normalizer.h"

namespace semplice {

QString Normalizer::display(const QString& text)
{
    return text.trimmed().normalized(QString::NormalizationForm_C);
}

QString Normalizer::identityKey(const QString& text)
{
    const QString nfc = display(text);
    // Collapse whitespace runs (also NBSP etc.) to a single ASCII space.
    QString collapsed;
    collapsed.reserve(nfc.size());
    bool lastSpace = false;
    for (const QChar c : nfc) {
        if (c.isSpace()) {
            if (!lastSpace)
                collapsed.append(QLatin1Char(' '));
            lastSpace = true;
        } else {
            collapsed.append(c);
            lastSpace = false;
        }
    }
    return collapsed.toCaseFolded().normalized(QString::NormalizationForm_C);
}

QString Normalizer::searchKey(const QString& text)
{
    const QString decomposed = identityKey(text).normalized(QString::NormalizationForm_D);
    QString out;
    out.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        const char16_t u = c.unicode();
        if (u >= 0x0300 && u <= 0x036F)
            continue;
        out.append(c);
    }
    return out.normalized(QString::NormalizationForm_C);
}

}  // namespace semplice
