#include "core/home/CategoryTypes.h"

namespace semplice {

QString CategorySpec::key() const
{
    switch (type) {
    case CategoryType::ArtistDiscography: return QStringLiteral("artist:") + arg;
    case CategoryType::Year: return QStringLiteral("year:") + arg;
    case CategoryType::Decade: return QStringLiteral("decade:") + arg;
    case CategoryType::Genre: return QStringLiteral("genre:") + arg;
    case CategoryType::YourPlaylists: return QStringLiteral("playlists");
    case CategoryType::YourAlbums: return QStringLiteral("albums");
    }
    return {};
}

std::optional<CategorySpec> CategorySpec::fromKey(const QString& key)
{
    if (key == QLatin1String("playlists"))
        return CategorySpec{CategoryType::YourPlaylists, {}, {}};
    if (key == QLatin1String("albums"))
        return CategorySpec{CategoryType::YourAlbums, {}, {}};
    const int colon = static_cast<int>(key.indexOf(QLatin1Char(':')));
    if (colon <= 0 || colon == key.size() - 1)
        return std::nullopt;
    const QString kind = key.left(colon);
    const QString arg = key.mid(colon + 1);
    if (kind == QLatin1String("artist"))
        return CategorySpec{CategoryType::ArtistDiscography, arg, {}};
    if (kind == QLatin1String("year"))
        return CategorySpec{CategoryType::Year, arg, {}};
    if (kind == QLatin1String("decade"))
        return CategorySpec{CategoryType::Decade, arg, {}};
    if (kind == QLatin1String("genre"))
        return CategorySpec{CategoryType::Genre, arg, {}};
    return std::nullopt;
}

}  // namespace semplice
