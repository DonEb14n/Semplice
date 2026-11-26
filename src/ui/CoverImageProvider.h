#pragma once

#include <QCache>
#include <QMutex>
#include <QQuickAsyncImageProvider>

namespace semplice {

// image://cover/<library-relative path>?...           a picture file of the library (album cover, avatar, ...)
// image://cover/file?path=<absolute>[&embedded=1]     a file anywhere; embedded=1 reads the picture inside an audio file
// Both accept &shape=circle or &shape=rounded&r=<0..0.5>: the picture comes back cut square and masked (soft edge).
// Pictures are decoded on the global thread pool, scaled to the requested size, kept in a memory cache
// (about 64 MB) and written to `.library/covers` as small PNGs so the next start does not decode big JPEGs.
class CoverImageProvider : public QQuickAsyncImageProvider {
public:
    CoverImageProvider();
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

    void setLibraryRoot(const QString& root);

    // used by the response runnable
    QImage load(const QString& id, const QSize& requestedSize);

private:
    QString root() const;
    mutable QMutex m_mutex;
    QString m_root;
    QCache<QString, QImage> m_memory;
};

}  // namespace semplice
