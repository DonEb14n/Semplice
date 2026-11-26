#include "ui/PlayerVm.h"

#include "ui/NavigationVm.h"

#include <QCoreApplication>
#include <QUrl>

#include <algorithm>

namespace semplice {

void PlayerVm::attach(PlaybackService* service)
{
    if (m_service)
        m_service->disconnect(this);
    m_service = service;
    if (m_service) {
        connect(m_service, &PlaybackService::currentChanged, this, &PlayerVm::changed);
        connect(m_service, &PlaybackService::stateChanged, this, &PlayerVm::changed);
        connect(m_service, &PlaybackService::queueChanged, this, &PlayerVm::changed);
        connect(m_service, &PlaybackService::modesChanged, this, &PlayerVm::changed);
        connect(m_service, &PlaybackService::volumeChanged, this, &PlayerVm::changed);
        connect(m_service, &PlaybackService::durationChanged, this, &PlayerVm::changed);
        connect(m_service, &PlaybackService::positionChanged, this, &PlayerVm::positionChanged);
    }
    emit changed();
    emit positionChanged();
}

QString PlayerVm::headerText() const
{
    const auto origin = m_service ? m_service->currentOrigin() : std::nullopt;
    if (!origin)
        return {};
    if (origin->kind == OriginKind::Recommendations)
        return QCoreApplication::translate("semplice::PlayerVm", "Recommendations");
    return origin->title;
}

bool PlayerVm::headerIsDiscography() const
{
    const auto origin = m_service ? m_service->currentOrigin() : std::nullopt;
    return origin && origin->kind == OriginKind::Discography;
}

int PlayerVm::headerKind() const
{
    const auto origin = m_service ? m_service->currentOrigin() : std::nullopt;
    if (!origin)
        return NavigationVm::Home;
    switch (origin->kind) {
    case OriginKind::Album: return NavigationVm::Album;
    case OriginKind::Playlist: return NavigationVm::Playlist;
    case OriginKind::Discography: return NavigationVm::Artist;
    case OriginKind::Carousel: return NavigationVm::Category;
    case OriginKind::Recommendations: break;
    }
    return NavigationVm::Home;
}

QString PlayerVm::headerKey() const
{
    const auto origin = m_service ? m_service->currentOrigin() : std::nullopt;
    return origin ? origin->key : QString();
}

qint64 PlayerVm::headerId() const
{
    const auto origin = m_service ? m_service->currentOrigin() : std::nullopt;
    return origin ? origin->id : 0;
}

bool PlayerVm::headerClickable() const
{
    const auto origin = m_service ? m_service->currentOrigin() : std::nullopt;
    return origin && origin->kind != OriginKind::Recommendations && (origin->id > 0 || !origin->key.isEmpty());
}

QString PlayerVm::title() const
{
    return hasTrack() ? m_service->currentTrack()->title : QCoreApplication::translate("semplice::PlayerVm", "Nothing is playing");
}

QString PlayerVm::artist() const
{
    return hasTrack() ? m_service->currentTrack()->artist : QString();
}

int PlayerVm::titleKind() const
{
    if (!hasTrack())
        return NavigationVm::Home;
    return m_service->currentTrack()->homeKind == ContainerKind::Album ? NavigationVm::Album : NavigationVm::Playlist;
}

qint64 PlayerVm::titleId() const
{
    return hasTrack() ? m_service->currentTrack()->homeId : 0;
}

qint64 PlayerVm::artistId() const
{
    return hasTrack() && !m_service->currentTrack()->artistIds.isEmpty() ? m_service->currentTrack()->artistIds.first() : 0;
}

QString PlayerVm::coverUrl() const
{
    if (!hasTrack())
        return {};
    // UI doc p.21: the picture inside the file first, then the album cover, then the placeholder (empty url).
    const PlayableTrack& t = *m_service->currentTrack();
    if (t.hasEmbeddedCover)
        return QStringLiteral("image://cover/file?embedded=1&path=") + QString::fromLatin1(QUrl::toPercentEncoding(t.absolutePath));
    if (!t.coverPath.isEmpty())
        return QStringLiteral("image://cover/file?path=") + QString::fromLatin1(QUrl::toPercentEncoding(t.coverPath));
    return {};
}

double PlayerVm::volume() const
{
    if (m_service)
        return m_service->volume();
    return m_settings ? m_settings->volume() : m_volume;
}

bool PlayerVm::muted() const
{
    if (m_service)
        return m_service->muted();
    return m_settings ? m_settings->muted() : m_muted;
}

void PlayerVm::setVolume(double volume01)
{
    volume01 = std::clamp(volume01, 0.0, 1.0);
    if (m_service) {
        if (volume01 > 0 && m_service->muted())
            m_service->setMuted(false);
        m_service->setVolume(volume01);
        return;
    }
    m_volume = volume01;
    if (m_settings)
        m_settings->setVolume(volume01);
    emit changed();
}

void PlayerVm::toggleMute()
{
    if (m_service) {
        m_service->setMuted(!m_service->muted());
        return;
    }
    m_muted = !muted();
    if (m_settings)
        m_settings->setMuted(m_muted);
    emit changed();
}

void PlayerVm::cycleRepeat()
{
    if (!m_service)
        return;
    const RepeatMode next = m_service->repeat() == RepeatMode::Off ? RepeatMode::All
                            : m_service->repeat() == RepeatMode::All ? RepeatMode::One
                                                                      : RepeatMode::Off;
    m_service->setRepeat(next);
}

}  // namespace semplice

namespace semplice {

QVariantList PlayerVm::artists() const
{
    QVariantList out;
    if (!hasTrack())
        return out;
    const PlayableTrack& t = *m_service->currentTrack();
    for (qsizetype i = 0; i < t.artistNames.size(); ++i)
        out.append(QVariantMap{{QStringLiteral("id"), t.artistIds.value(i)}, {QStringLiteral("name"), t.artistNames.at(i)}});
    return out;
}

}  // namespace semplice
