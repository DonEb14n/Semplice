#pragma once

#include "core/cards/CardQueries.h"
#include "core/tags/GenreVocabulary.h"
#include "ui/HomeVm.h"
#include "ui/LibraryEngine.h"
#include "ui/LibraryListModel.h"

#include <QAbstractListModel>
#include <QObject>

namespace semplice {

// Shared text helpers of the cards.
QString albumTypeName(int albumType);                       // "Single", "Album", "EP", ... (translated)
QString durationWords(qint64 durationMs);                   // "8 min 20 sec", "1 h 8 min"
QString releaseDateText(const ReleaseDate& date, const QString& language);  // "30 November 2020", "November 2020", "2020"
QString sectionTitle(ArtistSection section);                // "Albums", "Feat's", ...
QString coverUrlFor(const QString& libraryRoot, const QString& relativeCover);
QStringList tagDisplayNames(const QStringList& keys, const GenreVocabulary* genres, const QString& language);

// The rows of an album / playlist table ("#", title with cover and artist, album, time).
class TrackTableModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
public:
    enum Role {
        TrackIdRole = Qt::UserRole + 1, PositionRole, TitleRole, ArtistRole, ArtistIdRole, AlbumRole, AlbumIdRole,
        DurationRole, CoverRole, CanRemoveRole, InPlaylistRole, ArtistsRole
    };
    explicit TrackTableModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(m_rows.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return static_cast<int>(m_rows.size()); }
    void setRows(QList<TrackRow> rows, const QString& libraryRoot);
    const QList<TrackRow>& rows() const { return m_rows; }
    Q_INVOKABLE qint64 trackAt(int row) const { return row >= 0 && row < m_rows.size() ? m_rows.at(row).id : 0; }

signals:
    void countChanged();

private:
    QList<TrackRow> m_rows;
    QString m_root;
};

// People shown as chips or round pictures (the artists of an album).
class ArtistRefModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
public:
    enum Role { IdRole = Qt::UserRole + 1, NameRole, LetterRole, AvatarRole, HiddenRole };
    explicit ArtistRefModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(m_rows.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return static_cast<int>(m_rows.size()); }
    void setRows(QList<ArtistRef> rows);
    const QList<ArtistRef>& rows() const { return m_rows; }

signals:
    void countChanged();

private:
    QList<ArtistRef> m_rows;
};

class CardBase : public QObject {
    Q_OBJECT
public:
    CardBase(const GenreVocabulary* genres, const QString& language, QObject* parent = nullptr)
        : QObject(parent), m_genres(genres), m_language(language)
    {
    }
    void setLibraryRoot(const QString& root) { m_root = root; }

protected:
    const GenreVocabulary* m_genres;
    QString m_language;
    QString m_root;
};

class ArtistCardVm : public CardBase {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY changed FINAL)
    Q_PROPERTY(qint64 artistId READ artistId NOTIFY changed FINAL)
    Q_PROPERTY(QString name READ name NOTIFY changed FINAL)
    Q_PROPERTY(QString letter READ letter NOTIFY changed FINAL)
    Q_PROPERTY(QString avatar READ avatar NOTIFY changed FINAL)
    Q_PROPERTY(bool hidden READ hidden NOTIFY changed FINAL)
    Q_PROPERTY(bool userHidden READ userHidden NOTIFY changed FINAL)   // hidden by hand (the "Hidden artist" label)
    Q_PROPERTY(QStringList tags READ tags NOTIFY changed FINAL)
    Q_PROPERTY(QString stats READ stats NOTIFY changed FINAL)
    Q_PROPERTY(QObject* sections READ sections CONSTANT FINAL)
public:
    using CardBase::CardBase;
    bool ready() const { return m_ready; }
    qint64 artistId() const { return m_id; }
    QString name() const { return m_name; }
    QString letter() const { return m_name.trimmed().left(1).toUpper(); }
    QString avatar() const { return m_avatar; }
    bool hidden() const { return m_hidden; }
    bool userHidden() const { return m_userHidden; }
    QStringList tags() const { return m_tags; }
    QString stats() const { return m_stats; }
    QObject* sections() { return &m_sections; }

    Q_INVOKABLE void open(qint64 id);
    Q_INVOKABLE void reload() { if (m_id > 0) emit requested(m_id); }
    void setCard(const ArtistCard& card);
    void reset();

signals:
    void changed();
    void requested(qint64 id);

private:
    bool m_ready = false;
    qint64 m_id = 0;
    QString m_name, m_avatar, m_stats;
    bool m_hidden = false;
    bool m_userHidden = false;
    QStringList m_tags;
    CategoryListModel m_sections;
};

class AlbumCardVm : public CardBase {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY changed FINAL)
    Q_PROPERTY(qint64 albumId READ albumId NOTIFY changed FINAL)
    Q_PROPERTY(QString title READ title NOTIFY changed FINAL)
    Q_PROPERTY(QString typeText READ typeText NOTIFY changed FINAL)
    Q_PROPERTY(QString stats READ stats NOTIFY changed FINAL)
    Q_PROPERTY(QString cover READ cover NOTIFY changed FINAL)
    Q_PROPERTY(QStringList tags READ tags NOTIFY changed FINAL)
    Q_PROPERTY(QString dateText READ dateText NOTIFY changed FINAL)
    Q_PROPERTY(bool editable READ editable NOTIFY changed FINAL)
    Q_PROPERTY(qint64 hiddenOwner READ hiddenOwner NOTIFY changed FINAL)
    Q_PROPERTY(QObject* artists READ artists CONSTANT FINAL)
    Q_PROPERTY(QObject* tracks READ tracks CONSTANT FINAL)
public:
    using CardBase::CardBase;
    bool ready() const { return m_ready; }
    qint64 albumId() const { return m_id; }
    QString title() const { return m_title; }
    QString typeText() const { return m_type; }
    QString stats() const { return m_stats; }
    QString cover() const { return m_cover; }
    QStringList tags() const { return m_tags; }
    QString dateText() const { return m_date; }
    bool editable() const { return m_id > 0; }  // the virtual Unsorted tracks of a hidden artist cannot be edited
    qint64 hiddenOwner() const { return m_id < 0 ? -m_id : 0; }
    QObject* artists() { return &m_artists; }
    QObject* tracks() { return &m_tracks; }

    Q_INVOKABLE void open(qint64 id);
    Q_INVOKABLE void reload() { if (m_id != 0) emit requested(m_id); }
    void setCard(const AlbumCard& card);
    void reset();

signals:
    void changed();
    void requested(qint64 id);

private:
    bool m_ready = false;
    qint64 m_id = 0;
    QString m_title, m_type, m_stats, m_cover, m_date;
    QStringList m_tags;
    ArtistRefModel m_artists;
    TrackTableModel m_tracks;
};

class PlaylistCardVm : public CardBase {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY changed FINAL)
    Q_PROPERTY(qint64 playlistId READ playlistId NOTIFY changed FINAL)
    Q_PROPERTY(QString name READ name NOTIFY changed FINAL)
    Q_PROPERTY(QString stats READ stats NOTIFY changed FINAL)
    Q_PROPERTY(QString cover READ cover NOTIFY changed FINAL)
    Q_PROPERTY(QObject* tracks READ tracks CONSTANT FINAL)
public:
    using CardBase::CardBase;
    bool ready() const { return m_ready; }
    qint64 playlistId() const { return m_id; }
    QString name() const { return m_name; }
    QString stats() const { return m_stats; }
    QString cover() const { return m_cover; }
    QObject* tracks() { return &m_tracks; }

    Q_INVOKABLE void open(qint64 id);
    Q_INVOKABLE void reload() { if (m_id > 0) emit requested(m_id); }
    void setCard(const PlaylistCard& card);
    void reset();

signals:
    void changed();
    void requested(qint64 id);

private:
    bool m_ready = false;
    qint64 m_id = 0;
    QString m_name, m_stats, m_cover;
    TrackTableModel m_tracks;
};

}  // namespace semplice
