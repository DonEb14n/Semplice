#pragma once

#include "core/share/ShareService.h"
#include "core/tags/GenreVocabulary.h"

#include <QAbstractListModel>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>

#include <memory>

namespace semplice {

// The Share page, for both directions: before a file is made (export) and before a file is taken in (import) it shows everything
// that is in it as cards: the artists (with the status they get in the library), the albums or playlists with their tags and
// tracks. Everything has a check box; what is not ticked stays out. The tree comes from the engine thread, the work is a job there.
class ShareVm : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY changed FINAL)
    Q_PROPERTY(int kind READ kind NOTIFY changed FINAL)                   // ShareKind: 0 artist, 1 album, 2 playlist
    Q_PROPERTY(bool importing READ importing NOTIFY changed FINAL)        // the content of a file that is about to be imported
    Q_PROPERTY(bool ready READ ready NOTIFY changed FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY changed FINAL)
    Q_PROPERTY(double progress READ progress NOTIFY changed FINAL)
    Q_PROPERTY(bool canGo READ canGo NOTIFY changed FINAL)                // something is ticked: the export / import button works
    Q_PROPERTY(QString suggestedName READ suggestedName NOTIFY changed FINAL)
public:
    ShareVm(const GenreVocabulary* genres, const QString& language, QObject* parent = nullptr)
        : QAbstractListModel(parent), m_genres(genres), m_language(language)
    {
    }

    QString title() const { return m_tree.title; }
    int kind() const { return static_cast<int>(m_tree.kind); }
    bool importing() const { return m_tree.importing; }
    // the rows: headings and cards (an artist, an album, a playlist); the tracks are part of their card (role `tracks`)
    enum Role { TypeRole = Qt::UserRole + 1, NodeIndexRole, TitleRole, LabelRole, SubtitleRole, CoverRole, AuthorsRole, GenresRole,
                LanguagesRole, MoodsRole, DateTextRole, CheckedRole, ExpandedRole, MainRole, HiddenRole, ExistingRole, TracksRole };
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(m_rows.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool ready() const { return m_ready; }
    bool busy() const { return m_busy; }
    double progress() const { return m_total > 0 ? double(m_done) / double(m_total) : 0.0; }
    bool canGo() const;
    QString suggestedName() const;
    void setLibraryRoot(const QString& root) { m_root = root; }

    Q_INVOKABLE void open(int kind, qint64 id);      // the content of the library item, to be exported
    Q_INVOKABLE void openFile(const QUrl& file);     // the content of a .smpl file, to be imported
    Q_INVOKABLE void toggle(int index);              // a check box
    Q_INVOKABLE void toggleExpanded(int index);      // a card opens or closes its details
    Q_INVOKABLE void setHidden(int index, bool hidden);   // the status of an artist in the library: Hide / Show
    Q_INVOKABLE void exportTo(const QUrl& file);
    Q_INVOKABLE void importNow();

    // from the engine
    void setTree(const ShareTree& tree);
    void setProgress(qint64 done, qint64 total);
    void finished();                                  // the job is over (the page stays)

signals:
    void changed();
    void treeRequested(int kind, qint64 id);
    void fileRequested(const QString& path, const QString& previewDir);
    void exportRequested(const QString& path, const semplice::ShareSelection& selection);
    void importRequested(const QString& path, const semplice::ImportSelection& selection);

private:
    enum Type { ArtistCard = 0, AlbumCard = 1, TrackRow = 2, Heading = 3, PlaylistCard = 4 };
    struct Node {
        int type = ArtistCard;
        qint64 id = 0;            // the library id (export)
        QString key;              // the place in the file (import): "album:0", "track:0:1", "artist:Name" ...
        int parent = -1;          // a track: the index of its card
        QString title;
        QString label;            // "Artist", "Hidden Artist", "EP", "Playlist"
        QString subtitle;         // a track: "A, B"
        QString cover;
        QStringList authors;      // an album: the artists (chips)
        QStringList genres, languages, moods;
        QString dateText;
        qint64 durationMs = 0;
        bool checked = true;
        bool expanded = false;
        bool main = false;
        bool hidden = false;      // an artist: status in the library
        bool existing = false;    // import: the library has it already
    };
    void addTags(Node& node, const QStringList& keys) const;
    QString coverUrl(const QString& path) const;
    QVariantList tracksOf(int nodeIndex) const;
    int rowOf(int nodeIndex) const { return static_cast<int>(m_rows.indexOf(nodeIndex)); }
    void touch(int nodeIndex, const QList<int>& roles);
    ShareSelection exportSelection() const;
    ImportSelection importSelection() const;

    const GenreVocabulary* m_genres;
    QString m_language;
    ShareTree m_tree;
    QList<Node> m_nodes;
    QList<int> m_rows;            // the nodes that are rows (everything but the tracks)
    std::unique_ptr<QTemporaryDir> m_preview;   // the pictures of the package being looked at
    QString m_root;
    QString m_file;               // the file being imported
    bool m_ready = false;
    bool m_busy = false;
    qint64 m_done = 0;
    qint64 m_total = 0;
};

}  // namespace semplice
