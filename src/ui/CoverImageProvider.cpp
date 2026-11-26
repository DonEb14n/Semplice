#include "ui/CoverImageProvider.h"

#include "core/storage/ThumbnailCache.h"
#include "core/tags/TagService.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QQuickImageResponse>
#include <QRunnable>
#include <QSaveFile>
#include <QThreadPool>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace semplice {

namespace {

class Response : public QQuickImageResponse, public QRunnable {
public:
    Response(CoverImageProvider* provider, QString id, QSize size) : m_provider(provider), m_id(std::move(id)), m_size(size)
    {
        setAutoDelete(false);
    }
    QQuickTextureFactory* textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(m_image); }
    void run() override
    {
        m_image = m_provider->load(m_id, m_size);
        QMetaObject::invokeMethod(this, &QQuickImageResponse::finished, Qt::QueuedConnection);
    }

private:
    CoverImageProvider* m_provider;
    QString m_id;
    QSize m_size;
    QImage m_image;
};

QSize fitted(QSize source, QSize wanted)
{
    if (!wanted.isValid() || wanted.isEmpty() || source.isEmpty())
        return source;
    // never enlarge; keep the aspect ratio, the longer side wins so the view can crop
    const double scale = std::min(1.0, std::max(double(wanted.width()) / source.width(), double(wanted.height()) / source.height()));
    return QSize(std::max(1, int(source.width() * scale)), std::max(1, int(source.height() * scale)));
}

// The picture cut to a centred square and masked to a circle or a rounded square with a soft edge. Done here, once, so the
// QML needs no off-screen layer and no mask effect per picture (those were very slow on some graphics drivers).
QImage shaped(const QImage& source, const QString& shape, double ratio, QSize requested)
{
    const int side0 = std::min(source.width(), source.height());
    if (side0 <= 0)
        return source;
    QImage square = source.copy((source.width() - side0) / 2, (source.height() - side0) / 2, side0, side0);
    int side = side0;
    const int want = requested.isValid() ? std::min(requested.width(), requested.height()) : 0;
    if (want > 0 && side > want) {
        square = square.scaled(want, want, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        side = want;
    }
    QImage out(side, side, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainterPath path;
    if (shape == QLatin1String("circle"))
        path.addEllipse(QRectF(0, 0, side, side));
    else
        path.addRoundedRect(QRectF(0, 0, side, side), ratio * side, ratio * side);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawPath(path);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);  // the picture only where the shape is
    painter.drawImage(0, 0, square);
    painter.end();
    return out;
}

}  // namespace

CoverImageProvider::CoverImageProvider() : m_memory(64 * 1024 * 1024) {}

void CoverImageProvider::setLibraryRoot(const QString& root)
{
    QMutexLocker lock(&m_mutex);
    m_root = root;
    m_memory.clear();
}

QString CoverImageProvider::root() const
{
    QMutexLocker lock(&m_mutex);
    return m_root;
}

QQuickImageResponse* CoverImageProvider::requestImageResponse(const QString& id, const QSize& requestedSize)
{
    auto* response = new Response(this, id, requestedSize);
    QThreadPool::globalInstance()->start(response);
    return response;
}

QImage CoverImageProvider::load(const QString& id, const QSize& requestedSize)
{
    const QString libRoot = root();
    QString path;
    bool embedded = false;
    QString idPath = id;
    QString shape;
    double ratio = 0.2;
    auto readShape = [&](const QUrlQuery& query) {
        shape = query.queryItemValue(QStringLiteral("shape"));
        bool ok = false;
        const double r = query.queryItemValue(QStringLiteral("r")).toDouble(&ok);
        if (ok)
            ratio = std::clamp(r, 0.0, 0.5);
    };
    const int q = id.indexOf(QLatin1Char('?'));
    if (id.startsWith(QLatin1String("file?"))) {
        const QUrlQuery query(id.mid(5));
        readShape(query);
        path = query.queryItemValue(QStringLiteral("path"), QUrl::FullyDecoded);
        embedded = query.queryItemValue(QStringLiteral("embedded")) == QLatin1String("1");
    } else {
        idPath = q >= 0 ? id.left(q) : id;
        if (q >= 0)
            readShape(QUrlQuery(id.mid(q + 1)));
        path = QDir(libRoot).filePath(QUrl::fromPercentEncoding(idPath.toUtf8()));
    }
    if (path.isEmpty())
        return {};
    const QFileInfo info(path);
    if (!info.isFile())
        return {};

    const QString key = QStringLiteral("%1|%2|%3x%4|%5|%6|%7").arg(path).arg(embedded).arg(requestedSize.width()).arg(requestedSize.height()).arg(info.lastModified().toMSecsSinceEpoch()).arg(shape).arg(ratio);
    {
        QMutexLocker lock(&m_mutex);
        if (const QImage* hit = m_memory.object(key))
            return *hit;
    }

    // disk cache: small PNGs next to the database (the folder can be emptied at any time)
    QString cacheFile;
    if (!libRoot.isEmpty() && requestedSize.isValid() && !requestedSize.isEmpty()) {
        const QByteArray digest = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex();
        cacheFile = QDir(libRoot).filePath(QStringLiteral(".library/covers/") + QString::fromLatin1(digest) + QStringLiteral(".png"));
        if (QFileInfo::exists(cacheFile)) {
            QImage cached(cacheFile);
            if (!cached.isNull()) {
                ThumbnailCache::touch(cacheFile);
                QMutexLocker lock(&m_mutex);
                m_memory.insert(key, new QImage(cached), static_cast<int>(cached.sizeInBytes()));
                return cached;
            }
        }
    }

    QImage image;
    if (embedded) {
        PortableAtomicReplace replace;
        TagService tags(replace);
        auto cover = tags.readCover(path);
        if (cover.ok() && cover.value())
            image = QImage::fromData(cover.value()->data);
        if (!image.isNull() && requestedSize.isValid() && !requestedSize.isEmpty())
            image = image.scaled(fitted(image.size(), requestedSize), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    } else {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QSize full = reader.size();
        if (full.isValid())
            reader.setScaledSize(fitted(full, requestedSize));
        image = reader.read();
    }
    if (image.isNull())
        return {};
    if (!shape.isEmpty())
        image = shaped(image, shape, ratio, requestedSize);

    if (!cacheFile.isEmpty()) {
        QDir().mkpath(QFileInfo(cacheFile).absolutePath());
        QSaveFile out(cacheFile);
        if (out.open(QIODevice::WriteOnly)) {
            image.save(&out, "PNG");
            out.commit();
        }
    }
    QMutexLocker lock(&m_mutex);
    m_memory.insert(key, new QImage(image), static_cast<int>(image.sizeInBytes()));
    return image;
}

}  // namespace semplice
