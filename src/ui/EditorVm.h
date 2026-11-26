#pragma once

#include "core/tags/GenreVocabulary.h"
#include "ui/CardVms.h"
#include "ui/EditorTypes.h"
#include "ui/LibraryEngine.h"

#include <QAbstractListModel>
#include <QObject>
#include <QUrl>
#include <QVariantList>

namespace semplice {

struct DraftCredit {
    qint64 id = 0;         // 0: a name that does not exist yet
    QString name;
    int role = 0;          // 0 main, 1 feat
    bool hidden = false;
    QString avatar;        // picture url (may be empty: the chip shows the first letter)
};

// The artists of the album / track being edited, shown as chips.
class CreditModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
public:
    enum Role { IdRole = Qt::UserRole + 1, NameRole, LetterRole, RoleRole, HiddenRole, AvatarRole };
    explicit CreditModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(m_rows.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return static_cast<int>(m_rows.size()); }
    const QList<DraftCredit>& rows() const { return m_rows; }
    void setRows(QList<DraftCredit> rows);
    bool add(const DraftCredit& credit);   // false if that artist is already there
    void removeAt(int row);

signals:
    void countChanged();

private:
    QList<DraftCredit> m_rows;
};

struct EditRow {
    int scanIndex = -1;       // new file: index in the engine's scan
    qint64 trackId = 0;       // existing track
    QString title;
    QStringList names;        // credits (new files)
    QList<int> roles;
    QString artistsText;
    qint64 durationMs = 0;
    QString cover;
    bool duplicate = false;
    bool canRemove = false;   // existing track that also lives elsewhere: "Remove" is possible
};

// The tracks of the album / playlist being edited: those already there and the new files.
class EditRowModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
public:
    enum Role { ScanIndexRole = Qt::UserRole + 1, TrackIdRole, TitleRole, ArtistRole, DurationRole, CoverRole, IsNewRole, DuplicateRole, PositionRole, CanRemoveRole };
    explicit EditRowModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(m_rows.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return static_cast<int>(m_rows.size()); }
    QList<EditRow>& rows() { return m_rows; }
    const QList<EditRow>& rows() const { return m_rows; }
    void reset(QList<EditRow> rows);
    void append(const QList<EditRow>& rows);
    void removeAt(int row);
    void setTitleAt(int row, const QString& title);

signals:
    void countChanged();

private:
    QList<EditRow> m_rows;
};

// One editor window (Add / Edit of artist, album, playlist, track). It holds the DRAFT; "Save" sends one command
// to the engine thread (see LibraryEngineEditors.cpp). The validation rules are SPEC 8.3 and UI doc p.36-41.
class EditorVm : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString entity READ entity NOTIFY changed FINAL)           // "artist", "album", "playlist", "track"
    Q_PROPERTY(bool editing READ editing NOTIFY changed FINAL)
    Q_PROPERTY(qint64 id READ id NOTIFY changed FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY changed FINAL)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY changed FINAL)
    Q_PROPERTY(QString picture READ picture NOTIFY changed FINAL)         // url shown (new file or the current one)
    Q_PROPERTY(bool hasPicture READ hasPicture NOTIFY changed FINAL)
    Q_PROPERTY(bool userHidden READ userHidden WRITE setUserHidden NOTIFY changed FINAL)   // artist: "Status in Library", a draft until saved
    Q_PROPERTY(int albumType READ albumType WRITE setAlbumType NOTIFY changed FINAL)
    Q_PROPERTY(int year READ year NOTIFY changed FINAL)
    Q_PROPERTY(int month READ month NOTIFY changed FINAL)
    Q_PROPERTY(int day READ day NOTIFY changed FINAL)
    Q_PROPERTY(bool dateValid READ dateValid NOTIFY changed FINAL)
    Q_PROPERTY(QStringList tags READ tags NOTIFY changed FINAL)           // display names
    Q_PROPERTY(QVariantList tagChoices READ tagChoices NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList pendingArtists READ pendingArtists NOTIFY changed FINAL)   // [{ id: 0, title }] named in the new files
    // the language of the lyrics and the mood are tags too (own lists, own vocabulary); `tags` and `tagChoices` are the genres
    Q_PROPERTY(QStringList languageTags READ languageTags NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList languageChoices READ languageChoices NOTIFY changed FINAL)
    Q_PROPERTY(QStringList moodTags READ moodTags NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList moodChoices READ moodChoices NOTIFY changed FINAL)
    Q_PROPERTY(QObject* authors READ authors CONSTANT FINAL)
    Q_PROPERTY(QObject* rows READ rows CONSTANT FINAL)
    Q_PROPERTY(QObject* albums READ albums CONSTANT FINAL)               // artist editor: the discography tiles
    Q_PROPERTY(bool hiddenArtist READ hiddenArtist WRITE setHiddenArtist NOTIFY changed FINAL)
    Q_PROPERTY(bool canSave READ canSave NOTIFY changed FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    Q_PROPERTY(double progress READ progress NOTIFY busyChanged FINAL)
    Q_PROPERTY(QString progressText READ progressText NOTIFY busyChanged FINAL)
    Q_PROPERTY(int skippedFiles READ skippedFiles NOTIFY changed FINAL)
public:
    EditorVm(const GenreVocabulary* genres, const QString& language, QObject* parent = nullptr);

    QString entity() const { return m_entity; }
    bool editing() const { return m_editing; }
    qint64 id() const { return m_id; }
    bool loading() const { return m_loading; }
    QString title() const { return m_title; }
    void setTitle(const QString& text);
    QString picture() const;
    bool hasPicture() const { return !picture().isEmpty(); }
    int albumType() const { return m_albumType; }
    void setAlbumType(int type);
    int year() const { return m_year; }
    int month() const { return m_month; }
    int day() const { return m_day; }
    bool dateValid() const;
    QStringList tags() const;
    QVariantList tagChoices() const;
    QVariantList pendingArtists() const;
    QStringList languageTags() const { return namesOf(1); }
    QVariantList languageChoices() const { return choicesOf(1); }
    QStringList moodTags() const { return namesOf(2); }
    QVariantList moodChoices() const { return choicesOf(2); }
    QObject* authors() { return &m_authors; }
    QObject* rows() { return &m_rows; }
    QObject* albums() { return &m_albums; }
    bool hiddenArtist() const { return m_hidden; }
    bool userHidden() const { return m_userHidden; }
    void setUserHidden(bool hidden) { if (hidden != m_userHidden) { m_userHidden = hidden; emit changed(); } }
    void setHiddenArtist(bool hidden) { m_hidden = hidden; emit changed(); }
    bool canSave() const;
    bool busy() const { return m_busy; }
    double progress() const { return m_total > 0 ? double(m_done) / double(m_total) : 0.0; }
    QString progressText() const { return m_phase; }
    int skippedFiles() const { return m_skipped; }

    void setLibraryRoot(const QString& root) { m_root = root; }
    void setCurrentYear(int year) { m_currentYear = year; }
    void setPreselectedArtist(qint64 id) { m_preselected = id; }

    // ---- start
    Q_INVOKABLE void beginAdd(const QString& entity, const QString& prefillName = {}, qint64 preselectedArtist = 0);
    Q_INVOKABLE void beginEdit(const QString& entity, qint64 id);
    // Add playlist started from a track menu: the track is the first row.
    void beginAddWithTrack(qint64 trackId, const QString& name);
    // Files from the drop zone / the file dialog (a first scan fills the form from the proposal).
    void applyScan(const ScanSummary& summary);

    // ---- edit
    Q_INVOKABLE void setDate(int year, int month, int day);
    Q_INVOKABLE void addAuthor(qint64 id, const QString& name, bool hidden = false, int role = 0, const QString& avatar = QString());
    Q_INVOKABLE void removeAuthor(int row);
    Q_INVOKABLE void moveAuthor(int from, int to);   // drag a chip onto another: the order of the authors is the order of the credits
    Q_INVOKABLE void addTag(const QString& key);
    Q_INVOKABLE void removeTag(int index);
    Q_INVOKABLE void removeLanguage(int index) { removeOf(1, index); }
    Q_INVOKABLE void removeMood(int index) { removeOf(2, index); }
    Q_INVOKABLE void copyTagsFromArtists();      // adds the tags of every credited artist (the tags already there stay)
    void addTags(const QStringList& keys);
    Q_INVOKABLE void choosePicture(const QUrl& file);
    Q_INVOKABLE void clearPicture();
    Q_INVOKABLE void addFiles(const QVariantList& urls);
    Q_INVOKABLE void removeRow(int row);
    Q_INVOKABLE void setRowTitle(int row, const QString& title);
    // Reads the rows of the album / playlist again (after a track was removed, deleted or edited) and keeps the rest of the draft.
    Q_INVOKABLE void refreshRows();

    // ---- finish
    Q_INVOKABLE void save();
    Q_INVOKABLE void cancelEdit() { clear(); }  // leave an inline edit without saving
    Q_INVOKABLE void cancelWork();     // stops a running import / save
    // from the engine
    void fillFromArtist(const ArtistCard& card);
    void fillFromAlbum(const AlbumCard& card);
    void fillFromPlaylist(const PlaylistCard& card);
    void fillFromTrack(const TrackEdit& track);
    void setProgress(const QString& phase, qint64 done, qint64 total);
    void saveFinished(bool ok);
    void clear();

signals:
    void changed();
    void busyChanged();
    void loadRequested(const QString& entity, qint64 id);
    void artistTagsRequested(const QVariantList& artistIds);
    void scanRequested(const QStringList& paths, qint64 preselectedArtist, bool reset);
    void saveRequested(const QVariantMap& command);
    void stopRequested();

private:
    // category 0 = genres, 1 = languages, 2 = moods
    QStringList keysOf(int category) const;
    QStringList namesOf(int category) const;
    QVariantList choicesOf(int category) const;
    void removeOf(int category, int index);
    QVariantMap buildCommand() const;
    bool m_rowsOnly = false;
    QList<EditRow> keepScanned(QList<EditRow> rows) const;  // files dropped while the entity was still loading stay in the list
    void touch() { emit changed(); }

    const GenreVocabulary* m_genres;
    QString m_language;
    QString m_root;
    int m_currentYear = 2100;

    QString m_entity;
    bool m_editing = false;
    qint64 m_id = 0;
    bool m_loading = false;
    bool m_scanned = false;     // the first scan already filled the form
    qint64 m_preselected = 0;
    QString m_title;
    QString m_originalTitle;
    QString m_currentPicture;   // url of what the entity has now (edit)
    QString m_newPicture;       // local file the user chose
    bool m_removePicture = false;
    int m_albumType = 1;
    bool m_typeTouched = false;
    int m_year = 0, m_month = 0, m_day = 0;
    QStringList m_tagKeys;
    bool m_hidden = false;
    bool m_userHidden = false;
    bool m_originalUserHidden = false;
    CreditModel m_authors;
    EditRowModel m_rows;
    LibraryListModel m_albums;
    int m_skipped = 0;
    qint64 m_pendingTrack = 0;

    bool m_busy = false;
    QString m_phase;
    qint64 m_done = 0, m_total = 0;
};

}  // namespace semplice
