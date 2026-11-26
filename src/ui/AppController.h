#pragma once

#include "core/browse/PlayRequestBuilder.h"
#include "core/browse/UiState.h"
#include "core/playback/LibraryPlayback.h"
#include "core/playback/PlaybackService.h"
#include "platform/QtAudioBackend.h"
#include "platform/WinSmtc.h"
#include "platform/WinThumbBar.h"
#include <QPointer>

#include "ui/BannerVm.h"
#include "ui/CardVms.h"
#include "ui/EditorVm.h"
#include "ui/PickerVm.h"
#include "ui/SettingsVm.h"
#include "ui/ShareVm.h"
#include "core/tags/GenreVocabulary.h"
#include "ui/CoverImageProvider.h"
#include "ui/HomeVm.h"
#include "ui/ShellVm.h"
#include "ui/LibraryEngine.h"
#include "ui/LibraryVm.h"
#include "ui/NavigationVm.h"
#include "ui/PlayerVm.h"
#include "ui/QueueVm.h"
#include "ui/SearchVm.h"

#include <QQuickWindow>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <memory>

namespace semplice {

// The root of the GUI object graph. Lives on the GUI thread; owns the engine thread (library, import, search
// index), the audio backend and playback service (GUI thread, see design note M6 section 1), and the
// view-models QML binds to.
class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(int startup READ startup NOTIFY startupChanged FINAL)
    Q_PROPERTY(QString libraryPath READ libraryPath NOTIFY startupChanged FINAL)
    Q_PROPERTY(QObject* library READ library CONSTANT FINAL)
    Q_PROPERTY(QObject* player READ player CONSTANT FINAL)
    Q_PROPERTY(QObject* queue READ queue CONSTANT FINAL)
    Q_PROPERTY(QObject* search READ search CONSTANT FINAL)
    Q_PROPERTY(QObject* nav READ nav CONSTANT FINAL)
    Q_PROPERTY(QObject* home READ home CONSTANT FINAL)
    Q_PROPERTY(QObject* category READ category CONSTANT FINAL)
    Q_PROPERTY(QObject* picker READ picker CONSTANT FINAL)
    Q_PROPERTY(QObject* settingsVm READ settingsVm CONSTANT FINAL)
    Q_PROPERTY(QObject* editor READ editor CONSTANT FINAL)
    Q_PROPERTY(QObject* artistEditor READ artistEditor CONSTANT FINAL)  // the "Add artist" window opened from another editor
    Q_PROPERTY(QObject* trackEditor READ trackEditor CONSTANT FINAL)  // the inline editor of one track row
    Q_PROPERTY(QObject* artistCard READ artistCard CONSTANT FINAL)
    Q_PROPERTY(QObject* albumCard READ albumCard CONSTANT FINAL)
    Q_PROPERTY(QObject* playlistCard READ playlistCard CONSTANT FINAL)
    Q_PROPERTY(QObject* banners READ banners CONSTANT FINAL)
    Q_PROPERTY(QObject* share READ share CONSTANT FINAL)
    Q_PROPERTY(bool devDropping READ devDropping NOTIFY devDroppingChanged FINAL)  // testing: show the drop-zone look
    Q_PROPERTY(QString popup READ popup NOTIFY popupChanged FINAL)  // "", "search" or "queue"
public:
    enum Startup { Loading, NoLibraryYet, PathMissing, Ready, Failed };
    Q_ENUM(Startup)

    AppController(ISettings& settings, ShellVm& shell, QObject* parent = nullptr);
    ~AppController() override;

    int startup() const { return m_startup; }
    QString libraryPath() const { return m_libraryPath; }
    QObject* library() { return &m_libraryVm; }
    QObject* player() { return &m_playerVm; }
    QObject* queue() { return &m_queueVm; }
    QObject* search() { return &m_searchVm; }
    QObject* nav() { return &m_nav; }
    QObject* home() { return &m_homeVm; }
    QObject* category() { return &m_categoryVm; }
    QObject* picker() { return &m_picker; }
    QObject* settingsVm() { return &m_settingsVm; }
    QObject* editor() { return &m_editor; }
    QObject* artistEditor() { return &m_artistEditor; }
    QObject* trackEditor() { return &m_trackEditor; }
    QObject* artistCard() { return &m_artistCard; }
    QObject* albumCard() { return &m_albumCard; }
    QObject* playlistCard() { return &m_playlistCard; }
    QObject* banners() { return &m_banners; }
    QObject* share() { return &m_shareVm; }
    Q_INVOKABLE void importShareFile(const QUrl& file);   // a *.smpl package: added to the library, what is there already is skipped
    QString popup() const { return m_popup; }
    bool devDropping() const { return m_devDropping; }
    void setDevDropping(bool on)
    {
        m_devDropping = on;
        emit devDroppingChanged();
    }
    // Ownership passes to the QML engine (addImageProvider); the controller keeps a plain pointer to set the root.
    CoverImageProvider* takeCoverProvider() { return m_coverProvider; }

    // Kicks off the library start-up on the engine thread.
    void start();
    // Needs the native window: SMTC, media keys and the taskbar buttons.
    void attachWindow(QQuickWindow* window);
    // Last chance to save the playback session (called when the app quits).
    void shutdown();

    // ---- from QML
    Q_INVOKABLE void selectFolder(const QUrl& folder);
    Q_INVOKABLE void setPopup(const QString& name);
    Q_INVOKABLE void closePopups() { setPopup(QString()); }
    // kind: PlayKind (0 artist, 1 hidden artist, 2 album, 3 playlist, 4 track)
    Q_INVOKABLE void play(int kind, qint64 id);
    // Library / search items: open a page or start playing, following the rules of the UI doc.
    Q_INVOKABLE void open(int pageKind, qint64 id, const QString& text = QString());
    // Starts an album (or playlist) at a given track: a click on a row of its table.
    Q_INVOKABLE void playFrom(int kind, qint64 id, int index);
    // One write operation (OperationKind); the result arrives as a banner or a page change.
    // "Add as new playlist" in a track menu: opens the Add playlist window with that track in it (the name typed in the search box).
    Q_INVOKABLE void startPlaylistWithTrack(qint64 trackId, const QString& name);
    // Asks the engine what deleting would do; the answer arrives as deleteConfirmation (title + sentence).
    Q_INVOKABLE void requestDelete(int operation, qint64 id);
    // "Edit" in a track menu: that row turns into an editor (title, artists) right in the table.
    Q_INVOKABLE void editTrack(qint64 trackId);
    // testing: opens the editor of an album, moves its year by one and saves (what a user does, to measure freezes)
    void devSaveAlbum(qint64 albumId);
    // "Yes" on the merge question (see mergeConfirmation): the waiting artist save goes on.
    Q_INVOKABLE void confirmMerge();
    Q_INVOKABLE void cancelMerge() { m_mergeEditor = nullptr; }
    // Music dropped on the middle window: opens the right window for it (UI doc p.29, 44) and starts reading the files.
    Q_INVOKABLE void dropFiles(const QVariantList& urls);
    Q_INVOKABLE void operate(int operation, qint64 a, qint64 b = 0, const QString& text = QString());

signals:
    void startupChanged();
    void popupChanged();
    void devDroppingChanged();
    void operationCreated(int token, qint64 id);
    void deleteConfirmation(int operation, qint64 id, const QString& title, const QString& text);
    void mergeConfirmation(const QString& title, const QString& text);

private:
    void onOpened(const QString& root, const QString& dbPath, bool backupCreated, bool repaired);
    void onOpenFailed(int code, const QString& message, const QString& path);
    void onClosed();
    void onLibraryChanged(const ChangeSet& changes);
    void buildPlayback(const QString& root, const QString& dbPath);
    void tearDownPlayback();
    void rebuildSystemIntegration();
    void setStartup(Startup state);
    void requestList(int tab, int sortKey, int direction, int typeMask);
    void onOperationFinished(const OperationResult& result);
    void wireEditor(EditorVm& editor);
    void onPageChangedForEditors();
    struct PendingSave {
        EditorVm* editor = nullptr;
        QVariantMap command;
    };
    void finishSave(EditorVm& owner, const OperationResult& result);
    void pauseForSave(int token);
    void resumeAfterSave(int token);
    void requestCarousel(const QList<ContainerRef>& items, int index, bool all, const QString& title, const QString& key);
    // Builds a play queue on the request thread (it must never wait for a save that the library thread is busy with).
    void runRequest(std::function<Result<PlayRequest>(PlayRequestBuilder&)> build);

    ISettings& m_settings;
    UiState m_uiState;

    QThread m_thread;
    LibraryEngine* m_engine = nullptr;

    platform::QtAudioBackend m_backend;
    struct ReleaserProxy;
    std::unique_ptr<ReleaserProxy> m_releaser;
    struct RequestWorker;
    std::unique_ptr<RequestWorker> m_requests;  // thread + read connection that turn "play this" into a queue
    struct PlaybackStack;
    std::unique_ptr<PlaybackStack> m_playback;
    platform::WinSmtc m_smtc;
    platform::WinThumbBar m_thumbs;
    std::unique_ptr<SystemIntegration> m_systemIntegration;
    bool m_windowAttached = false;

    GenreVocabulary m_genres;
    LibraryVm m_libraryVm;
    HomeVm m_homeVm;
    CategoryVm m_categoryVm;
    ArtistCardVm m_artistCard;
    AlbumCardVm m_albumCard;
    PlaylistCardVm m_playlistCard;
    PickerVm m_picker;
    SettingsVm m_settingsVm;
    int m_rescanToken = 0;
    EditorVm m_editor;
    EditorVm m_artistEditor;
    EditorVm m_trackEditor;
    QTimer m_tagSyncTimer;  // after the player moved on: write the tags of the file that was playing
    QHash<int, EditorVm*> m_jobs;  // token of a scan / save -> the editor that asked
    QHash<int, QPointer<EditorVm>> m_tagJobs;  // token of "copy the artists' tags" -> the editor that asked
    QHash<int, PendingSave> m_mergeChecks;  // artist saves waiting for "does this name belong to another artist?"
    EditorVm* m_mergeEditor = nullptr;      // the editor whose save waits for the owner's yes
    bool m_mergeApproved = false;
    int m_nextToken = 0;
    QSet<int> m_saveTokens;  // saves in flight
    bool m_pausedForSave = false;
    QTimer m_savePauseTimer;
    qint64 m_pendingTrack = 0;
    PlayerVm m_playerVm;
    QueueVm m_queueVm;
    SearchVm m_searchVm;
    NavigationVm m_nav;
    BannerVm m_banners;
    ShareVm m_shareVm;
    QSet<int> m_shareTokens;   // the share jobs that are running (export and import)
    CoverImageProvider* m_coverProvider = new CoverImageProvider;

    Startup m_startup = Loading;
    QString m_libraryPath;
    QString m_popup;
    bool m_everReady = false;
    bool m_devDropping = false;
};

}  // namespace semplice
