#include "ui/QueueVm.h"

#include <QUrl>

namespace semplice {

QString QueueVm::coverUrlOf(const PlayableTrack& t)
{
    if (t.hasEmbeddedCover)
        return QStringLiteral("image://cover/file?embedded=1&path=") + QString::fromLatin1(QUrl::toPercentEncoding(t.absolutePath));
    if (!t.coverPath.isEmpty())
        return QStringLiteral("image://cover/file?path=") + QString::fromLatin1(QUrl::toPercentEncoding(t.coverPath));
    return {};
}

void QueueVm::attach(PlaybackService* service, ITrackCatalog* catalog)
{
    if (m_service)
        m_service->disconnect(this);
    m_service = service;
    m_catalog = catalog;
    if (m_service) {
        connect(m_service, &PlaybackService::currentChanged, this, &QueueVm::rebuild);
        connect(m_service, &PlaybackService::queueChanged, this, &QueueVm::rebuild);
        connect(m_service, &PlaybackService::modesChanged, this, &QueueVm::rebuild);
    }
    rebuild();
}

std::optional<PlayableTrack> QueueVm::resolve(TrackId id) const
{
    if (!m_catalog)
        return std::nullopt;
    auto it = m_cache.find(id.value);
    if (it == m_cache.end())
        it = m_cache.insert(id.value, m_catalog->resolve(id));
    return it.value();
}

void QueueVm::rebuild()
{
    m_cache.clear();
    beginResetModel();
    m_entries.clear();
    m_indexes.clear();
    m_currentRow = -1;
    m_previous.reset();
    m_current.reset();
    m_next.reset();
    m_previous2.reset();
    m_next2.reset();
    if (m_service && !m_service->queue().isEmpty()) {
        const PlayQueue& q = m_service->queue();
        m_entries = q.currentGroupInOrder();
        m_indexes = q.currentGroupOrder();
        if (const auto cur = q.current()) {
            m_currentRow = static_cast<int>(m_indexes.indexOf(cur->index));
            m_current = resolve(q.group(cur->group).tracks.at(cur->index).track);
        }
        const auto before = q.playedBefore(2);
        if (!before.isEmpty())
            m_previous = resolve(before.first().track);
        if (before.size() > 1)
            m_previous2 = resolve(before.at(1).track);
        const auto after = q.upNext(2);
        if (!after.isEmpty())
            m_next = resolve(after.first().track);
        if (after.size() > 1)
            m_next2 = resolve(after.at(1).track);
    }
    endResetModel();
    emit changed();
}

QVariant QueueVm::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    const auto track = resolve(m_entries.at(index.row()).track);
    switch (role) {
    case TitleRole:
    case Qt::DisplayRole: return track ? track->title : QString();
    case ArtistRole: return track ? track->artist : QString();
    case CoverRole: return track ? coverUrlOf(*track) : QString();
    case IsCurrentRole: return index.row() == m_currentRow;
    case DurationRole: return track ? track->durationMs : 0;
    }
    return {};
}

QHash<int, QByteArray> QueueVm::roleNames() const
{
    return {{TitleRole, "title"}, {ArtistRole, "artist"}, {CoverRole, "cover"}, {IsCurrentRole, "isCurrent"}, {DurationRole, "duration"}};
}

void QueueVm::playPrevious()
{
    if (m_service)
        m_service->previousTrack();
}

void QueueVm::playNext()
{
    if (m_service)
        m_service->next();
}

void QueueVm::playPrevious2()
{
    if (m_service) {
        m_service->previousTrack();
        m_service->previousTrack();
    }
}

void QueueVm::playNext2()
{
    if (m_service) {
        m_service->next();
        m_service->next();
    }
}

void QueueVm::playRow(int row)
{
    if (m_service && row >= 0 && row < m_indexes.size())
        m_service->playAt(m_service->queue().currentGroupIndex(), m_indexes.at(row));
}

}  // namespace semplice
