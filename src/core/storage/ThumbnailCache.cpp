#include "core/storage/ThumbnailCache.h"

#include "core/storage/OwnFiles.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace semplice {

int ThumbnailCache::trim(const QString& folder, qint64 capBytes)
{
    struct Item {
        QString path;
        qint64 size;
        QDateTime used;
    };
    QList<Item> items;
    qint64 total = 0;
    const QFileInfoList files = QDir(folder).entryInfoList({QStringLiteral("*.png")}, QDir::Files);
    for (const QFileInfo& f : files) {
        items.append({f.absoluteFilePath(), f.size(), f.lastModified()});
        total += f.size();
    }
    if (total <= capBytes)
        return 0;
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.used < b.used; });
    const qint64 goal = capBytes / 5 * 4;
    int removed = 0;
    for (const Item& item : std::as_const(items)) {
        if (total <= goal)
            break;
        if (ownfiles::removeFile(item.path).ok()) {
            total -= item.size;
            ++removed;
        }
    }
    return removed;
}

void ThumbnailCache::touch(const QString& file)
{
    QFile f(file);
    const QDateTime now = QDateTime::currentDateTime();
    if (QFileInfo(file).lastModified().daysTo(now) >= 1 && f.open(QIODevice::ReadWrite))
        f.setFileTime(now, QFileDevice::FileModificationTime);
}

}  // namespace semplice
