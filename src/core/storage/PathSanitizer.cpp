#include "core/storage/PathSanitizer.h"

#include "core/util/Normalizer.h"

#include <QStringList>

namespace semplice {

namespace {

bool isIllegal(QChar c)
{
    return c.unicode() < 32 || QStringLiteral("<>:\"/\\|?*").contains(c);
}

bool isTrailingJunk(QChar c)
{
    return isIllegal(c) || c == QLatin1Char('.') || c == QLatin1Char(' ');
}

// Cuts to at most `maxUnits` UTF-16 units without splitting a surrogate pair.
QString truncateUnits(const QString& s, int maxUnits)
{
    if (s.size() <= maxUnits)
        return s;
    int end = maxUnits;
    if (end > 0 && s.at(end - 1).isHighSurrogate())
        --end;  // the pair would be cut in half: drop the lone high surrogate too
    return s.left(end);
}

QString stripTrailingJunk(QString s)
{
    while (!s.isEmpty() && isTrailingJunk(s.at(s.size() - 1)))
        s.chop(1);
    return s;
}

// Steps 1-4 of the rules: NFC + trim, drop trailing illegal/dot/space, replace the remaining illegal characters.
QString clean(const QString& input)
{
    QString s = stripTrailingJunk(Normalizer::display(input));
    for (QChar& c : s) {
        if (isIllegal(c))
            c = QLatin1Char('_');
    }
    return s;
}

bool isReservedDeviceName(const QString& stemUpper)
{
    static const QStringList kNames = {QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"),
                                       QStringLiteral("NUL")};
    if (kNames.contains(stemUpper))
        return true;
    if (stemUpper.size() == 4 && (stemUpper.startsWith(QLatin1String("COM")) || stemUpper.startsWith(QLatin1String("LPT")))) {
        const QChar digit = stemUpper.at(3);
        return digit >= QLatin1Char('1') && digit <= QLatin1Char('9');
    }
    return false;
}

// Windows treats "CON", "CON.txt", "con.tar.gz" ... as the device. Append "_" to the part before the first dot.
QString avoidReserved(const QString& name)
{
    const qsizetype dot = name.indexOf(QLatin1Char('.'));
    const QString stem = dot < 0 ? name : name.left(dot);
    if (!isReservedDeviceName(stem.toUpper()))
        return name;
    return stem + QLatin1Char('_') + (dot < 0 ? QString() : name.mid(dot));
}

QString cleanExtension(const QString& extension)
{
    QString out;
    for (const QChar c : extension.toLower()) {
        if ((c >= QLatin1Char('a') && c <= QLatin1Char('z')) || (c >= QLatin1Char('0') && c <= QLatin1Char('9')))
            out.append(c);
    }
    return out.left(10);
}

}  // namespace

QString PathSanitizer::fallbackFor(NameKind kind)
{
    switch (kind) {
    case NameKind::Artist: return QStringLiteral("Unknown artist");
    case NameKind::Album: return QStringLiteral("Unknown album");
    case NameKind::Playlist:
    case NameKind::Track: return QStringLiteral("Untitled");
    }
    return QStringLiteral("Untitled");
}

QString PathSanitizer::sanitizeDirName(const QString& name, NameKind kind)
{
    QString s = clean(name);
    if (s.isEmpty())
        s = fallbackFor(kind);
    s = avoidReserved(s);
    s = stripTrailingJunk(truncateUnits(s, kMaxComponentLength));
    return s.isEmpty() ? fallbackFor(kind) : s;
}

QString PathSanitizer::sanitizeFileName(const QString& stem, const QString& extension, NameKind kind)
{
    const QString ext = cleanExtension(extension);
    const int budget = kMaxComponentLength - (ext.isEmpty() ? 0 : static_cast<int>(ext.size()) + 1);

    QString s = clean(stem);
    if (s.isEmpty())
        s = fallbackFor(kind);
    s = stripTrailingJunk(truncateUnits(s, budget));
    if (s.isEmpty())
        s = truncateUnits(fallbackFor(kind), budget);

    QString name = ext.isEmpty() ? s : s + QLatin1Char('.') + ext;
    name = avoidReserved(name);
    // The "_" added for a reserved name may push the length over the limit by one: cut the stem again.
    if (name.size() > kMaxComponentLength) {
        const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
        const QString tail = dot < 0 ? QString() : name.mid(dot);
        name = stripTrailingJunk(truncateUnits(name.left(dot < 0 ? name.size() : dot),
                                               kMaxComponentLength - static_cast<int>(tail.size()))) + tail;
    }
    return name;
}

QString PathSanitizer::compareKey(const QString& name)
{
    return Normalizer::display(name).toCaseFolded().normalized(QString::NormalizationForm_C);
}

QString PathSanitizer::withCollisionSuffix(const QString& name, int n, bool isFile)
{
    if (n < 2)
        return name;
    QString stem = name;
    QString ext;
    if (isFile) {
        const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
        if (dot > 0 && name.size() - dot <= 11) {
            stem = name.left(dot);
            ext = name.mid(dot);
        }
    }
    const QString suffix = QStringLiteral(" (%1)").arg(n);
    const int budget = kMaxComponentLength - static_cast<int>(suffix.size()) - static_cast<int>(ext.size());
    stem = stripTrailingJunk(truncateUnits(stem, budget));
    return stem + suffix + ext;
}

QString PathSanitizer::resolveCollision(const QString& name, bool isFile,
                                        const std::function<bool(const QString&)>& isTaken)
{
    QString candidate = name;
    for (int n = 2; isTaken(candidate); ++n)
        candidate = withCollisionSuffix(name, n, isFile);
    return candidate;
}

Result<void> PathSanitizer::checkPathLength(const QString& fullPath)
{
    if (fullPath.size() > kMaxFullPathLength)
        return Error::make(ErrorCode::PathTooLong, QStringLiteral("path has %1 characters").arg(fullPath.size()));
    return {};
}

}  // namespace semplice
