#include "SessionFixture.h"

#include "core/services/SettingsService.h"

using namespace semplice;
using namespace semplice::testing;

namespace {

// In-memory settings with only what the switcher needs.
class MemorySettings final : public ISettings {
public:
    QString language() const override { return QStringLiteral("en"); }
    void setLanguage(const QString&) override {}
    QString userName() const override { return {}; }
    void setUserName(const QString&) override {}
    QString theme() const override { return QStringLiteral("dark"); }
    void setTheme(const QString&) override {}
    double volume() const override { return 0.5; }
    void setVolume(double) override {}
    bool muted() const override { return false; }
    void setMuted(bool) override {}
    QString libraryPath() const override { return path; }
    void setLibraryPath(const QString& p) override { path = p; }
    WindowGeometry windowGeometry() const override { return {}; }
    void setWindowGeometry(const WindowGeometry&) override {}
    QString path;
};

}  // namespace

// M4: opening, creating and switching libraries (SPEC 6.6).
class TstLibrarySession : public QObject {
    Q_OBJECT

private slots:
    void anEmptyFolderStartsANewLibrary()
    {
        SessionFixture fx;
        MemorySettings settings;
        QCOMPARE(LibrarySwitcher::classify(fx.libRoot()), FolderState::Empty);
        auto r = LibrarySwitcher::select(fx.libRoot(), fx.deps, &settings);
        QVERIFY2(r.ok(), r.ok() ? "" : qPrintable(r.error().symbol()));
        QVERIFY(QFileInfo::exists(fx.abs(QStringLiteral(".library/library.json"))));
        QVERIFY(QFileInfo::exists(fx.abs(QStringLiteral("Artists"))));
        QVERIFY(QFileInfo::exists(fx.abs(QStringLiteral("Playlists"))));
        QCOMPARE(settings.path, QDir::cleanPath(fx.libRoot()));
        r.value().reset();
        QCOMPARE(LibrarySwitcher::classify(fx.libRoot()), FolderState::Library);
    }

    void aFolderWithOtherThingsIsRefused()
    {
        SessionFixture fx;
        QFile f(fx.abs(QStringLiteral("holiday.jpg")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        QCOMPARE(LibrarySwitcher::classify(fx.libRoot()), FolderState::Other);
        MemorySettings settings;
        auto r = LibrarySwitcher::select(fx.libRoot(), fx.deps, &settings);
        QVERIFY(!r.ok());
        QCOMPARE(r.error().code, ErrorCode::FolderNotEmpty);
        QVERIFY(settings.path.isEmpty());  // nothing is remembered for a refused folder
        QVERIFY(QFileInfo::exists(f.fileName()));
    }

    void aMissingFolderIsReported()
    {
        SessionFixture fx;
        const QString gone = fx.dirPath(QStringLiteral("nowhere"));
        QCOMPARE(LibrarySwitcher::classify(gone), FolderState::Missing);
        auto r = LibrarySwitcher::select(gone, fx.deps);
        QVERIFY(!r.ok());
        QCOMPARE(r.error().code, ErrorCode::NotFound);
    }

    void startupFollowsTheStoredPath()
    {
        SessionFixture fx;
        MemorySettings settings;
        // first launch
        auto first = LibrarySwitcher::openStored(settings, fx.deps);
        QCOMPARE(first.state, LibrarySwitcher::StartupState::NoLibraryYet);

        // create, remember, restart
        QVERIFY(LibrarySwitcher::select(fx.libRoot(), fx.deps, &settings).ok());
        auto again = LibrarySwitcher::openStored(settings, fx.deps);
        QCOMPARE(again.state, LibrarySwitcher::StartupState::Ready);
        QVERIFY(again.session != nullptr);
        again.session.reset();

        // the folder was moved away
        QVERIFY(QDir().rename(fx.libRoot(), fx.dirPath(QStringLiteral("moved"))));
        auto missing = LibrarySwitcher::openStored(settings, fx.deps);
        QCOMPARE(missing.state, LibrarySwitcher::StartupState::PathMissing);
        QCOMPARE(missing.path, QDir::cleanPath(fx.libRoot()));

        // "locate your library": pointing at the moved folder works and is remembered
        auto located = LibrarySwitcher::select(fx.dirPath(QStringLiteral("moved")), fx.deps, &settings);
        QVERIFY(located.ok());
        QCOMPARE(settings.path, QDir::cleanPath(fx.dirPath(QStringLiteral("moved"))));
    }

    void aLibraryFromANewerVersionFailsWithoutBeingTouched()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        fx.session.reset();
        const QString json = fx.abs(QStringLiteral(".library/library.json"));
        QByteArray text = SessionFixture::readAll(json);
        const QByteArray dbBefore = SessionFixture::readAll(fx.abs(QStringLiteral(".library/library.db")));
        QJsonObject o = QJsonDocument::fromJson(text).object();
        o.insert(QStringLiteral("schema"), 999);
        QFile f(json);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(o).toJson());
        f.close();

        MemorySettings settings;
        settings.path = fx.libRoot();
        auto r = LibrarySwitcher::openStored(settings, fx.deps);
        QCOMPARE(r.state, LibrarySwitcher::StartupState::Failed);
        QCOMPARE(r.error.code, ErrorCode::SchemaTooNew);
        QCOMPARE(SessionFixture::readAll(fx.abs(QStringLiteral(".library/library.db"))), dbBefore);
    }

    void openingSweepsLeftoversOfAnInterruptedRun()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        fx.session.reset();
        QDir().mkpath(fx.abs(QStringLiteral(".library/tmp")));
        QFile cover(fx.abs(QStringLiteral(".library/tmp/left-over.jpg")));
        QVERIFY(cover.open(QIODevice::WriteOnly));
        cover.close();
        QVERIFY(fx.open());
        QVERIFY(!QFileInfo::exists(fx.abs(QStringLiteral(".library/tmp/left-over.jpg"))));
    }

    void theSearchIndexFollowsChanges()
    {
        SessionFixture fx;
        QVERIFY(fx.create());
        const QString folder = fx.albumFolder(QStringLiteral("a"), QStringLiteral("Findable Band"), QStringLiteral("Needle Album"), 2, 10);
        auto& importer = fx.session->importer();
        auto scan = importer.scan({folder});
        QVERIFY(importer.commitAlbum(scan.value(), importer.propose(scan.value()).album).ok());
        const auto hits = fx.session->search().search(QStringLiteral("needle"));
        QVERIFY2(hits.albums.total == 1, "the album should be found right after the import");
        // a fresh session rebuilds the same index
        QVERIFY(fx.open());
        QCOMPARE(fx.session->search().search(QStringLiteral("needle")).albums.total, 1);
    }

    void settingsRememberTheLibraryPath()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ini = dir.filePath(QStringLiteral("settings.ini"));
        {
            SettingsService s(ini);
            QVERIFY(s.libraryPath().isEmpty());
            s.setLibraryPath(QStringLiteral("D:/Music/Library"));
        }
        SettingsService again(ini);
        QCOMPARE(again.libraryPath(), QStringLiteral("D:/Music/Library"));
    }
};

QTEST_GUILESS_MAIN(TstLibrarySession)
#include "tst_library_session.moc"
