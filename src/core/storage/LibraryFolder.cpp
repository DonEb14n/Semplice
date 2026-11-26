#include "core/storage/LibraryFolder.h"

#include "core/db/Migrator.h"
#include "core/storage/DbCommitLog.h"
#include "core/storage/OwnFiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

namespace semplice {

#define SEMPLICE_TRY(expr)                      \
    do {                                        \
        auto semplice_try_result = (expr);      \
        if (!semplice_try_result.ok())          \
            return semplice_try_result.error(); \
    } while (false)

namespace {

constexpr qint64 kBackupMaxAgeMs = 24LL * 60 * 60 * 1000;

Result<void> writeJson(const QString& path, const LibraryInfo& info)
{
    QJsonObject root;
    root.insert(QStringLiteral("app"), QString::fromLatin1(LibraryFolder::kAppId));
    root.insert(QStringLiteral("library_uid"), info.uid);
    root.insert(QStringLiteral("schema"), info.schema);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return Error::make(ErrorCode::IoError, QStringLiteral("cannot write ") + path);
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit())
        return Error::make(ErrorCode::IoError, QStringLiteral("cannot write ") + path);
    return {};
}

}  // namespace

LibraryLayout LibraryLayout::forRoot(const QString& root)
{
    LibraryLayout l;
    l.root = QDir::cleanPath(root);
    l.dotLibrary = l.root + QStringLiteral("/.library");
    l.dbPath = l.dotLibrary + QStringLiteral("/library.db");
    l.jsonPath = l.dotLibrary + QStringLiteral("/library.json");
    l.journalDir = l.dotLibrary + QStringLiteral("/journal");
    l.coversDir = l.dotLibrary + QStringLiteral("/covers");
    l.artistsDir = l.root + QStringLiteral("/Artists");
    l.playlistsDir = l.root + QStringLiteral("/Playlists");
    return l;
}

QString LibraryLayout::backupPath(int n) const
{
    return dbPath + QStringLiteral(".bak%1").arg(n);
}

Result<LibraryInfo> LibraryFolder::create(const QString& root, IFileAttributes& attributes)
{
    const LibraryLayout layout = LibraryLayout::forRoot(root);
    const QDir dir(layout.root);
    if (dir.exists()) {
        if (!dir.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
            return Error::make(ErrorCode::FolderNotEmpty, layout.root);
    } else if (!QDir().mkpath(layout.root)) {
        return Error::make(ErrorCode::IoError, QStringLiteral("cannot create ") + layout.root);
    }

    for (const QString& sub : {layout.dotLibrary, layout.journalDir, layout.coversDir, layout.artistsDir,
                               layout.playlistsDir}) {
        if (!QDir().mkpath(sub))
            return Error::make(ErrorCode::IoError, QStringLiteral("cannot create ") + sub);
    }
    SEMPLICE_TRY(attributes.setHidden(layout.dotLibrary));

    LibraryInfo info;
    info.uid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    info.schema = Migrator::latestVersion();
    SEMPLICE_TRY(writeJson(layout.jsonPath, info));
    return info;
}

Result<LibraryInfo> LibraryFolder::inspect(const QString& root)
{
    const LibraryLayout layout = LibraryLayout::forRoot(root);
    QFile f(layout.jsonPath);
    if (!f.open(QIODevice::ReadOnly))
        return Error::make(ErrorCode::NotALibrary, QStringLiteral("no library.json in ") + layout.root);
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return Error::make(ErrorCode::NotALibrary, QStringLiteral("library.json is not valid"));
    const QJsonObject o = doc.object();
    if (o.value(QStringLiteral("app")).toString() != QLatin1String(kAppId))
        return Error::make(ErrorCode::NotALibrary, QStringLiteral("library.json belongs to another application"));

    LibraryInfo info;
    info.uid = o.value(QStringLiteral("library_uid")).toString();
    info.schema = o.value(QStringLiteral("schema")).toInt();
    if (info.uid.isEmpty())
        return Error::make(ErrorCode::NotALibrary, QStringLiteral("library.json has no library_uid"));
    if (info.schema > Migrator::latestVersion())
        return Error::make(ErrorCode::SchemaTooNew,
                           QStringLiteral("library schema %1 is newer than %2").arg(info.schema).arg(Migrator::latestVersion()));
    return info;
}

Result<void> LibraryFolder::quickCheck(Database& db)
{
    auto r = db.run(QStringLiteral("PRAGMA quick_check"));
    if (!r.ok())
        return Error::make(ErrorCode::DatabaseCorrupt, r.error().devMessage);
    QSqlQuery q = std::move(r).value();
    if (!q.next() || q.value(0).toString() != QLatin1String("ok"))
        return Error::make(ErrorCode::DatabaseCorrupt, QStringLiteral("quick_check: ") + q.value(0).toString());
    return {};
}

Result<bool> LibraryFolder::rotateBackupsIfDue(const LibraryLayout& layout, Database& db, const IClock& clock)
{
    const QFileInfo newest(layout.backupPath(1));
    const qint64 nowMs = clock.now().toMSecsSinceEpoch();
    if (newest.exists() && nowMs - newest.lastModified().toMSecsSinceEpoch() <= kBackupMaxAgeMs)
        return false;

    const QString fresh = layout.dbPath + QStringLiteral(".bak-new");
    SEMPLICE_TRY(ownfiles::removeFile(fresh));
    SEMPLICE_TRY(db.exec(QStringLiteral("VACUUM INTO ?"), {fresh}));

    // bak3 <- bak2 <- bak1 <- fresh; the oldest backup falls off the end.
    SEMPLICE_TRY(ownfiles::removeFile(layout.backupPath(LibraryLayout::kBackupCount)));
    for (int n = LibraryLayout::kBackupCount - 1; n >= 1; --n) {
        if (QFileInfo::exists(layout.backupPath(n)) && !QFile::rename(layout.backupPath(n), layout.backupPath(n + 1)))
            return Error::make(ErrorCode::IoError, QStringLiteral("cannot rotate ") + layout.backupPath(n));
    }
    if (!QFile::rename(fresh, layout.backupPath(1)))
        return Error::make(ErrorCode::IoError, QStringLiteral("cannot create ") + layout.backupPath(1));
    QFile stamp(layout.backupPath(1));
    if (stamp.open(QIODevice::ReadWrite))
        stamp.setFileTime(clock.now(), QFileDevice::FileModificationTime);
    return true;
}

Result<int> LibraryFolder::restoreNewestGoodBackup(const LibraryLayout& layout, const IClock& clock)
{
    for (int n = 1; n <= LibraryLayout::kBackupCount; ++n) {
        const QString backup = layout.backupPath(n);
        if (!QFileInfo::exists(backup))
            continue;
        {
            auto probe = Database::open(backup, true);
            if (!probe.ok() || !LibraryFolder::quickCheck(*probe.value()).ok())
                continue;
            auto version = Migrator::currentVersion(*probe.value());
            if (!version.ok() || version.value() > Migrator::latestVersion())
                continue;
        }
        // Keep the damaged files next to it: nothing is deleted.
        const QString stamp = clock.now().toString(QStringLiteral("yyyyMMddHHmmss"));
        for (const QString& suffix : {QString(), QStringLiteral("-wal"), QStringLiteral("-shm")}) {
            const QString current = layout.dbPath + suffix;
            if (QFileInfo::exists(current) &&
                !QFile::rename(current, layout.dbPath + QStringLiteral(".corrupt-") + stamp + suffix))
                return Error::make(ErrorCode::IoError, QStringLiteral("cannot move aside ") + current);
        }
        if (!QFile::copy(backup, layout.dbPath))
            return Error::make(ErrorCode::IoError, QStringLiteral("cannot restore from ") + backup);
        return n;
    }
    return Error::make(ErrorCode::DatabaseCorrupt, QStringLiteral("no usable backup"));
}

// ---------------------------------------------------------------------------------------------

namespace {

Result<OpenedLibrary> openImpl(const QString& root, bool createDatabase, ITrash& trash, const IClock& clock,
                               FaultInjector* injector)
{
    OpenedLibrary lib;
    lib.layout = LibraryLayout::forRoot(root);
    auto info = LibraryFolder::inspect(root);
    if (!info.ok())
        return info.error();
    lib.info = info.value();

    if (!createDatabase && !QFileInfo::exists(lib.layout.dbPath))
        return Error::make(ErrorCode::NotALibrary, QStringLiteral("library.db is missing in ") + lib.layout.root);

    auto db = Database::open(lib.layout.dbPath);
    if (!db.ok())
        return Error::make(ErrorCode::DatabaseCorrupt, db.error().devMessage);
    lib.db = std::move(db).value();

    if (auto r = Migrator::migrate(*lib.db); !r.ok()) {
        // A damaged file also fails here; only "too new" is a different story.
        return r.error().code == ErrorCode::SchemaTooNew ? r.error()
                                                          : Error::make(ErrorCode::DatabaseCorrupt, r.error().devMessage);
    }
    SEMPLICE_TRY(LibraryFolder::quickCheck(*lib.db));

    if (lib.info.schema != Migrator::latestVersion()) {
        lib.info.schema = Migrator::latestVersion();
        SEMPLICE_TRY(writeJson(lib.layout.jsonPath, lib.info));
    }

    auto backup = LibraryFolder::rotateBackupsIfDue(lib.layout, *lib.db, clock);
    if (!backup.ok())
        return backup.error();
    lib.backupCreated = backup.value();

    lib.storage = std::make_unique<FileStorageService>(lib.layout.root, lib.layout.journalDir, trash, clock);
    lib.storage->setFaultInjector(injector);
    Database* dbPtr = lib.db.get();
    auto recovery = lib.storage->recover([dbPtr](const QString& id) { return DbCommitLog::isCommitted(*dbPtr, id); });
    if (!recovery.ok())
        return recovery.error();
    lib.recovery = std::move(recovery).value();
    SEMPLICE_TRY(DbCommitLog::purgeExcept(*lib.db, lib.storage->pendingOperationIds()));
    return lib;
}

}  // namespace

Result<OpenedLibrary> openLibrary(const QString& root, ITrash& trash, const IClock& clock, FaultInjector* injector)
{
    return openImpl(root, false, trash, clock, injector);
}

Result<OpenedLibrary> createLibrary(const QString& root, IFileAttributes& attributes, ITrash& trash, const IClock& clock)
{
    auto info = LibraryFolder::create(root, attributes);
    if (!info.ok())
        return info.error();
    return openImpl(root, true, trash, clock, nullptr);
}

}  // namespace semplice
