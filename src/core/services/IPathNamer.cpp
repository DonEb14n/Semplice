#include "core/services/IPathNamer.h"

namespace semplice {

QString SanitizingPathNamer::dirName(const QString& name, NameKind kind) const
{
    return PathSanitizer::sanitizeDirName(name, kind);
}

QString SanitizingPathNamer::albumTrackFileName(int position, const QString& title, const QString& extension) const
{
    const QString title2 = title.trimmed().isEmpty() ? PathSanitizer::fallbackFor(NameKind::Track) : title;
    return PathSanitizer::sanitizeFileName(QStringLiteral("%1 - %2").arg(position, 2, 10, QLatin1Char('0')).arg(title2),
                                           extension);
}

QString SanitizingPathNamer::playlistTrackFileName(const QString& title, const QString& artist,
                                                   const QString& extension) const
{
    const QString title2 = title.trimmed().isEmpty() ? PathSanitizer::fallbackFor(NameKind::Track) : title;
    const QString artist2 = artist.trimmed().isEmpty() ? PathSanitizer::fallbackFor(NameKind::Artist) : artist;
    return PathSanitizer::sanitizeFileName(QStringLiteral("%1 - %2").arg(title2, artist2), extension);
}

}  // namespace semplice
