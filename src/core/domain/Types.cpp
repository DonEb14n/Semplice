#include "core/domain/Types.h"

namespace semplice {

QString toDbString(AlbumType type)
{
    switch (type) {
    case AlbumType::Single: return QStringLiteral("single");
    case AlbumType::Album: return QStringLiteral("album");
    case AlbumType::Ep: return QStringLiteral("ep");
    case AlbumType::Live: return QStringLiteral("live");
    case AlbumType::Compilation: return QStringLiteral("compilation");
    case AlbumType::Other: return QStringLiteral("other");
    }
    return QStringLiteral("other");
}

std::optional<AlbumType> albumTypeFromString(const QString& text)
{
    for (AlbumType t : {AlbumType::Single, AlbumType::Album, AlbumType::Ep, AlbumType::Live,
                        AlbumType::Compilation, AlbumType::Other}) {
        if (toDbString(t) == text)
            return t;
    }
    return std::nullopt;
}

QString toDbString(CreditRole role)
{
    return role == CreditRole::Main ? QStringLiteral("main") : QStringLiteral("feat");
}

std::optional<CreditRole> creditRoleFromString(const QString& text)
{
    if (text == QLatin1String("main"))
        return CreditRole::Main;
    if (text == QLatin1String("feat"))
        return CreditRole::Feat;
    return std::nullopt;
}

QString toDbString(ContainerKind kind)
{
    return kind == ContainerKind::Album ? QStringLiteral("album") : QStringLiteral("playlist");
}

}  // namespace semplice
