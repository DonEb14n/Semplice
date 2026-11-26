#pragma once

#include "core/playback/PlaybackService.h"

#include <QAbstractListModel>
#include <QHash>
#include <QPointer>

namespace semplice {

// The queue as the side panel (previous / current / next) and the hotbar popup (the whole current group,
// top to bottom) show it. As a list model it is the popup's content.
class QueueVm : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool hasPrevious READ hasPrevious NOTIFY changed FINAL)
    Q_PROPERTY(QString previousTitle READ previousTitle NOTIFY changed FINAL)
    Q_PROPERTY(QString previousArtist READ previousArtist NOTIFY changed FINAL)
    Q_PROPERTY(QString previousCover READ previousCover NOTIFY changed FINAL)
    Q_PROPERTY(bool hasCurrent READ hasCurrent NOTIFY changed FINAL)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY changed FINAL)
    Q_PROPERTY(QString currentArtist READ currentArtist NOTIFY changed FINAL)
    Q_PROPERTY(QString currentCover READ currentCover NOTIFY changed FINAL)
    // the two tracks beyond the neighbours (the collapsed player shows five tracks)
    Q_PROPERTY(bool hasPrevious2 READ hasPrevious2 NOTIFY changed FINAL)
    Q_PROPERTY(QString previous2Title READ previous2Title NOTIFY changed FINAL)
    Q_PROPERTY(QString previous2Artist READ previous2Artist NOTIFY changed FINAL)
    Q_PROPERTY(QString previous2Cover READ previous2Cover NOTIFY changed FINAL)
    Q_PROPERTY(bool hasNext2 READ hasNext2 NOTIFY changed FINAL)
    Q_PROPERTY(QString next2Title READ next2Title NOTIFY changed FINAL)
    Q_PROPERTY(QString next2Artist READ next2Artist NOTIFY changed FINAL)
    Q_PROPERTY(QString next2Cover READ next2Cover NOTIFY changed FINAL)
    Q_PROPERTY(bool hasNext READ hasNext NOTIFY changed FINAL)
    Q_PROPERTY(QString nextTitle READ nextTitle NOTIFY changed FINAL)
    Q_PROPERTY(QString nextArtist READ nextArtist NOTIFY changed FINAL)
    Q_PROPERTY(QString nextCover READ nextCover NOTIFY changed FINAL)
    Q_PROPERTY(int currentRow READ currentRow NOTIFY changed FINAL)
    Q_PROPERTY(int count READ count NOTIFY changed FINAL)
public:
    enum Role { TitleRole = Qt::UserRole + 1, ArtistRole, CoverRole, IsCurrentRole, DurationRole };
    explicit QueueVm(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    void attach(PlaybackService* service, ITrackCatalog* catalog);

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(m_entries.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return static_cast<int>(m_entries.size()); }

    bool hasPrevious() const { return m_previous.has_value(); }
    QString previousTitle() const { return m_previous ? m_previous->title : QString(); }
    QString previousArtist() const { return m_previous ? m_previous->artist : QString(); }
    QString previousCover() const { return coverOf(m_previous); }
    bool hasCurrent() const { return m_current.has_value(); }
    QString currentTitle() const { return m_current ? m_current->title : QString(); }
    QString currentArtist() const { return m_current ? m_current->artist : QString(); }
    QString currentCover() const { return coverOf(m_current); }
    bool hasNext() const { return m_next.has_value(); }
    QString nextTitle() const { return m_next ? m_next->title : QString(); }
    QString nextArtist() const { return m_next ? m_next->artist : QString(); }
    QString nextCover() const { return coverOf(m_next); }
    bool hasPrevious2() const { return m_previous2.has_value(); }
    QString previous2Title() const { return m_previous2 ? m_previous2->title : QString(); }
    QString previous2Artist() const { return m_previous2 ? m_previous2->artist : QString(); }
    QString previous2Cover() const { return coverOf(m_previous2); }
    bool hasNext2() const { return m_next2.has_value(); }
    QString next2Title() const { return m_next2 ? m_next2->title : QString(); }
    QString next2Artist() const { return m_next2 ? m_next2->artist : QString(); }
    QString next2Cover() const { return coverOf(m_next2); }
    int currentRow() const { return m_currentRow; }

    Q_INVOKABLE void playPrevious();
    Q_INVOKABLE void playNext();
    Q_INVOKABLE void playPrevious2();
    Q_INVOKABLE void playNext2();
    Q_INVOKABLE void playRow(int row);

    static QString coverUrlOf(const PlayableTrack& track);

signals:
    void changed();

private:
    void rebuild();
    static QString coverOf(const std::optional<PlayableTrack>& t) { return t ? coverUrlOf(*t) : QString(); }
    std::optional<PlayableTrack> resolve(TrackId id) const;

    QPointer<PlaybackService> m_service;
    ITrackCatalog* m_catalog = nullptr;
    QList<QueueTrack> m_entries;
    QList<int> m_indexes;  // index in the group of every row (for playAt)
    int m_currentRow = -1;
    std::optional<PlayableTrack> m_previous, m_current, m_next, m_previous2, m_next2;
    mutable QHash<qint64, std::optional<PlayableTrack>> m_cache;
};

}  // namespace semplice
