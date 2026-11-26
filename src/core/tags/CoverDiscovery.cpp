#include "core/tags/CoverDiscovery.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace semplice {

std::optional<CoverImage> CoverDiscovery::loadImageFile(const QString& path)
{
    QFile f(path);
    if (!f.exists() || f.size() > kMaxCoverBytes || !f.open(QIODevice::ReadOnly))
        return std::nullopt;
    return CoverImage::fromBytes(f.readAll());
}

QString CoverDiscovery::findInFolder(const QString& folder)
{
    const QDir dir(folder);
    if (!dir.exists())
        return {};
    const QStringList files = dir.entryList(QDir::Files | QDir::Hidden, QDir::Name);
    // Case-insensitive match on the base name, extensions in a fixed order.
    for (const QString& base : {QStringLiteral("cover"), QStringLiteral("album_cover")}) {
        for (const QString& ext : {QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png")}) {
            for (const QString& name : files) {
                if (name.compare(base + QLatin1Char('.') + ext, Qt::CaseInsensitive) != 0)
                    continue;
                const QString path = dir.filePath(name);
                if (loadImageFile(path))
                    return path;
            }
        }
    }
    return {};
}

QString CoverDiscovery::storedName(CoverOwner owner, const CoverImage& image)
{
    QString base;
    switch (owner) {
    case CoverOwner::Album: base = QStringLiteral("album_cover"); break;
    case CoverOwner::Artist: base = QStringLiteral("artist_cover"); break;
    case CoverOwner::Playlist: base = QStringLiteral("playlist_cover"); break;
    }
    return base + QLatin1Char('.') + image.extension();
}

Result<CoverSource> CoverDiscovery::resolve(const QString& userChosenPath, const QString& sourceFolder, ITagService& tags,
                                            const QStringList& trackPathsInOrder)
{
    CoverSource out;
    if (!userChosenPath.isEmpty()) {
        const auto image = loadImageFile(userChosenPath);
        if (!image)
            return Error::make(ErrorCode::InvalidArgument, QStringLiteral("not a usable cover image: ") + userChosenPath);
        out.kind = CoverSource::Kind::UserChosen;
        out.path = userChosenPath;
        out.image = image;
        return out;
    }
    if (!sourceFolder.isEmpty()) {
        const QString file = findInFolder(sourceFolder);
        if (!file.isEmpty()) {
            out.kind = CoverSource::Kind::FolderFile;
            out.path = file;
            out.image = loadImageFile(file);
            return out;
        }
    }
    if (!trackPathsInOrder.isEmpty()) {
        auto embedded = tags.readCover(trackPathsInOrder.first());
        if (embedded.ok() && embedded.value()) {
            out.kind = CoverSource::Kind::Embedded;
            out.image = embedded.value();
            return out;
        }
    }
    return out;  // Kind::None: the UI draws the placeholder
}

Result<void> CoverDiscovery::writeFile(const CoverImage& image, const QString& absolutePath)
{
    if (QFileInfo::exists(absolutePath))
        return Error::make(ErrorCode::AlreadyExists, absolutePath);
    QSaveFile f(absolutePath);
    if (!f.open(QIODevice::WriteOnly))
        return Error::make(ErrorCode::IoError, QStringLiteral("cannot write ") + absolutePath);
    f.write(image.data);
    if (!f.commit())
        return Error::make(ErrorCode::IoError, QStringLiteral("cannot write ") + absolutePath);
    return {};
}

}  // namespace semplice
