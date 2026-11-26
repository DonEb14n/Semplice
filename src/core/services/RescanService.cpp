#include "core/services/RescanService.h"

#include "core/db/AlbumRepo.h"
#include "core/db/PlaylistRepo.h"
#include "core/db/TrackRepo.h"
#include "core/storage/PathSanitizer.h"
#include "core/tags/ArtistStringSplitter.h"
#include "core/util/Logger.h"
#include "core/util/Normalizer.h"

#include <QCollator>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

#include <algorithm>

namespace semplice {

namespace {

struct DiskFile {
    QString relative;
    QString absolute;
    qint64 size = 0;
    qint64 mtime = 0;
};

QByteArray sha256Of(const QString& path, const JobContext& job, bool* ok)
{
    QFile f(path);
    *ok = false;
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!f.atEnd()) {
        if (job.cancelled())
            return {};
        hash.addData(f.read(1024 * 1024));
    }
    *ok = true;
    return hash.result();
}

}  // namespace

RescanService::RescanService(LibraryService& library, ITagService& tags, const GenreVocabulary& genres,
                             const ICodepageConverter& codepages, Database& db, IFileStorage& storage,
                             const LibraryLayout& layout, const IClock& clock)
    : m_library(library),
      m_tags(tags),
      m_genreMap(genres),
      m_fixer(codepages),
      m_db(db),
      m_storage(storage),
      m_layout(layout),
      m_clock(clock)
{
}

Result<RescanReport> RescanService::run(const RescanOptions& options, const JobContext& job)
{
    RescanReport report;
    TrackRepo tracks(m_db);

    // The folder must be there at all: an unplugged drive looks like "everything is missing".
    if (!QFileInfo(m_layout.artistsDir).isDir() && !QFileInfo(m_layout.playlistsDir).isDir())
        return Error::make(ErrorCode::NotALibrary, QStringLiteral("library folders are not available: ") + m_layout.root);

    // ---- 1. what is on disk ----
    job.report(QStringLiteral("walk"), 0, 0);
    QList<DiskFile> disk;
    for (const QString& base : {m_layout.artistsDir, m_layout.playlistsDir}) {
        if (!QFileInfo(base).isDir())
            continue;
        QDirIterator it(base, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (job.cancelled()) {
                report.cancelled = true;
                return report;
            }
            const QString path = it.next();
            if (!TagService::isSupportedExtension(QFileInfo(path).suffix()))
                continue;
            const QFileInfo info(path);
            disk.append({QDir(m_layout.root).relativeFilePath(path), info.absoluteFilePath(), info.size(),
                         info.lastModified().toMSecsSinceEpoch()});
        }
    }
    report.checked = static_cast<int>(disk.size());

    // ---- 2. compare with the database ----
    auto rows = tracks.allFiles();
    if (!rows.ok())
        return rows.error();
    QHash<QString, TrackRepo::FileRow> known;
    for (const auto& row : rows.value())
        known.insert(PathSanitizer::compareKey(row.path), row);

    QList<DiskFile> orphans;
    qint64 step = 0;
    for (const DiskFile& f : std::as_const(disk)) {
        if (job.cancelled()) {
            report.cancelled = true;
            return report;
        }
        job.report(QStringLiteral("compare"), step++, disk.size());
        const QString key = PathSanitizer::compareKey(f.relative);
        auto it = known.find(key);
        if (it == known.end()) {
            orphans.append(f);
            continue;
        }
        if (it->size != f.size || it->mtime != f.mtime) {  // changed behind our back: refresh the facts, keep our data
            (void)tracks.setFileInfo(it->id, f.size, f.mtime, m_clock.now().toMSecsSinceEpoch());
            if (auto media = m_tags.read(f.absolute); media.ok())
                (void)tracks.setMedia(it->id, media.value().durationMs, media.value().codec, media.value().hasCover);
            ++report.refreshed;
        }
        known.erase(it);  // what is left afterwards is missing
    }

    // ---- 3. rows whose file is gone ----
    QList<TrackId> missing;
    for (const auto& row : std::as_const(known))
        missing.append(row.id);
    // Removing a track renumbers (and renames) the files behind it in its folder. Go from the back of every
    // folder to the front so that a renaming never has to touch a file that is missing as well.
    {
        AlbumRepo albumRepo(m_db);
        PlaylistRepo playlistRepo(m_db);
        struct Key { int kind; qint64 container; int position; TrackId id; };
        QList<Key> keys;
        for (TrackId id : std::as_const(missing)) {
            Key k{0, 0, 0, id};
            if (auto t = tracks.get(id); t.ok() && t.value()) {
                k.kind = static_cast<int>(t.value()->home.kind);
                k.container = t.value()->home.id;
                auto pos = t.value()->home.kind == ContainerKind::Album ? albumRepo.positionOf(AlbumId(k.container), id)
                                                                         : playlistRepo.positionOf(PlaylistId(k.container), id);
                k.position = pos.ok() ? pos.value() : 0;
            }
            keys.append(k);
        }
        std::sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) {
            if (a.kind != b.kind) return a.kind < b.kind;
            if (a.container != b.container) return a.container < b.container;
            return a.position > b.position;
        });
        missing.clear();
        for (const Key& k : std::as_const(keys))
            missing.append(k.id);
    }
    const qint64 total = rows.value().size();
    if (!options.allowMassRemoval && missing.size() > std::max<qint64>(20, total / 2)) {
        report.missingBlocked = static_cast<int>(missing.size());
        report.notes << QStringLiteral("%1 of %2 files are missing: not removed (is the drive connected?)").arg(missing.size()).arg(total);
        SLOG_WARN(report.notes.last());
    } else {
        qint64 n = 0;
        for (TrackId id : std::as_const(missing)) {
            if (job.cancelled()) {
                report.cancelled = true;
                return report;
            }
            job.report(QStringLiteral("missing"), n++, missing.size());
            if (auto r = m_library.deleteTrack(id); r.ok())
                ++report.missingRemoved;
            else
                report.notes << QStringLiteral("could not remove track %1: %2").arg(id.value).arg(r.error().devMessage);
        }
    }

    // ---- 4. files nobody knows ----
    if (!orphans.isEmpty()) {
        QCollator collator;
        collator.setNumericMode(true);
        std::sort(orphans.begin(), orphans.end(), [&](const DiskFile& a, const DiskFile& b) { return collator.compare(a.relative, b.relative) < 0; });

        QList<TrackInput> inputs;
        QList<FileOp> duplicateTrash;
        qint64 n = 0;
        for (const DiskFile& f : std::as_const(orphans)) {
            if (job.cancelled()) {
                report.cancelled = true;
                return report;
            }
            job.report(QStringLiteral("adopt"), n++, orphans.size());
            auto read = m_tags.read(f.absolute);
            bool hashed = false;
            const QByteArray hash = sha256Of(f.absolute, job, &hashed);
            if (!read.ok() || !hashed) {
                ++report.skipped;
                report.notes << QStringLiteral("left in place: %1").arg(f.relative);
                continue;
            }
            auto existing = tracks.findByHash(hash);
            if (existing.ok() && existing.value()) {
                duplicateTrash.append({FileOp::Kind::Trash, f.relative, {}});  // recoverable from the Recycle Bin
                ++report.duplicatesTrashed;
                continue;
            }
            TrackTags tags = std::move(read).value();
            m_fixer.fix(tags);
            TrackInput in;
            TrackDraft& d = in.draft;
            d.title = Normalizer::display(tags.title).isEmpty() ? QFileInfo(f.absolute).completeBaseName() : tags.title;
            d.durationMs = tags.durationMs;
            d.extension = QFileInfo(f.absolute).suffix().toLower();
            d.codec = tags.codec;
            d.contentHash = hash;
            d.fileSize = f.size;
            d.fileMtime = f.mtime;
            d.hasEmbeddedCover = tags.hasCover;
            d.tags = m_genreMap.resolveAll(tags.genres);
            ArtistRepo artists(m_db);
            const auto exists = [&](const QString& name) {
                auto found = artists.findByName(name);
                return found.ok() && found.value().has_value();
            };
            for (const NamedCredit& c : ArtistStringSplitter::splitAll(tags.artists, exists))
                d.credits.append(CreditDraft(c.name, c.role));
            if (d.credits.isEmpty())
                d.credits.append(CreditDraft(PathSanitizer::fallbackFor(NameKind::Artist), CreditRole::Main));
            d.sourcePath = f.absolute;
            d.moveIntoLibrary = true;
            inputs.append(in);
        }
        if (!duplicateTrash.isEmpty()) {
            if (auto r = m_storage.apply(duplicateTrash); !r.ok()) {
                report.duplicatesTrashed = 0;
                report.notes << QStringLiteral("could not move duplicates to the Recycle Bin: ") + r.error().devMessage;
            }
        }
        if (!inputs.isEmpty()) {
            PlaylistRepo playlists(m_db);
            const QString base = QStringLiteral("Rescan ") + m_clock.now().toString(QStringLiteral("yyyy-MM-dd HH-mm"));
            QString name = base;
            for (int i = 2; ; ++i) {
                auto exists = playlists.findByName(name);
                if (!exists.ok() || !exists.value())
                    break;
                name = QStringLiteral("%1 (%2)").arg(base).arg(i);
            }
            PlaylistDraft draft;
            draft.name = name;
            draft.tracks = inputs;
            auto created = m_library.createPlaylist(draft, job);
            if (created.ok()) {
                report.rescanPlaylist = created.value();
                report.adopted = static_cast<int>(m_library.lastReport().tracksCreated.size());
            } else if (created.error().code == ErrorCode::Cancelled) {
                report.cancelled = true;
                return report;
            } else {
                report.notes << QStringLiteral("could not adopt the new files: ") + created.error().devMessage;
            }
        }
    }

    // ---- 5. tags that were out of sync ----
    auto dirtyBefore = tracks.dirtyTracks();
    const int dirtyCount = dirtyBefore.ok() ? static_cast<int>(dirtyBefore.value().size()) : 0;
    auto failed = m_library.syncDirtyTags(job);
    if (!failed.ok()) {
        if (failed.error().code == ErrorCode::Cancelled) {
            report.cancelled = true;
            return report;
        }
        report.notes << QStringLiteral("tag sync failed: ") + failed.error().devMessage;
    } else {
        report.tagsFailed = static_cast<int>(failed.value().size());
        report.tagsSynced = dirtyCount - report.tagsFailed;
    }

    // ---- 6. housekeeping ----
    job.report(QStringLiteral("maintenance"), 0, 0);
    auto maintenance = Maintenance::run(m_layout, m_db, m_clock);
    if (maintenance.ok())
        report.maintenance = maintenance.value();
    else
        report.notes << QStringLiteral("maintenance failed: ") + maintenance.error().devMessage;
    return report;
}

}  // namespace semplice
