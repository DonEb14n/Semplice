#include "core/tags/TrackTags.h"

namespace semplice {

std::optional<CoverImage> CoverImage::fromBytes(const QByteArray& bytes)
{
    if (bytes.size() >= 3 && static_cast<quint8>(bytes[0]) == 0xFF && static_cast<quint8>(bytes[1]) == 0xD8 &&
        static_cast<quint8>(bytes[2]) == 0xFF)
        return CoverImage{bytes, QStringLiteral("image/jpeg")};
    if (bytes.size() >= 8 && bytes.startsWith(QByteArray::fromHex("89504E470D0A1A0A")))
        return CoverImage{bytes, QStringLiteral("image/png")};
    return std::nullopt;
}

QString CoverImage::extension() const
{
    return mimeType == QLatin1String("image/png") ? QStringLiteral("png") : QStringLiteral("jpg");
}

}  // namespace semplice
