#include "core/services/ImportService.h"

#include "core/db/ArtistRepo.h"
#include "core/db/TrackRepo.h"
#include "core/storage/OwnFiles.h"
#include "core/tags/CoverDiscovery.h"
#include "core/util/Logger.h"
#include "core/util/Normalizer.h"

#include <QCollator>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QUuid>

#include <algorithm>

namespace semplice {

namespace {

#define SEMPLICE_TRY(expr)                      \
    do {                                        \
        auto semplice_try_result = (expr);      \
        if (!semplice_try_result.ok())          \
            return semplice_try_result.error(); \
    } while (false)

// Streaming SHA-256 of the source bytes (the hash that identifies a track in the library, SPEC 6.1).
Result<QByteArray> hashFile(const QString& path, const JobContext& job)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return Error::make(ErrorCode::IoError, QStringLiteral("cannot read ") + path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!f.atEnd()) {
        if (job.cancelled())
            return Error::make(ErrorCode::Cancelled);
        const QByteArray chunk = f.read(1024 * 1024);
        if (chunk.isEmpty() && f.error() != QFileDevice::NoError)
            return Error::make(ErrorCode::IoError, QStringLiteral("read error in ") + path);
        hash.addData(chunk);
    }
    return hash.result();
}

QStringList audioFilesIn(const QStringList& paths)
{
    QSet<QString> seen;
    QStringList files;
    auto add = [&](const QString& path) {
        const QFileInfo info(path);
        if (!TagService::isSupportedExtension(info.suffix()))
            return;
        const QString canonical = info.canonicalFilePath();
        if (canonical.isEmpty() || seen.contains(canonical.toLower()))
            return;
        seen.insert(canonical.toLower());
        files << info.absoluteFilePath();
    };
    for (const QString& p : paths) {
        const QFileInfo info(p);
        if (info.isDir()) {
            QDirIterator it(p, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext())
                add(it.next());
        } else if (info.isFile()) {
            add(p);
        }
    }
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(files.begin(), files.end(), [&](const QString& a, const QString& b) { return collator.compare(a, b) < 0; });
    return files;
}

// The most frequent value by identity (case/spacing insensitive); returns the first spelling seen.
QString mostFrequent(const QStringList& values)
{
    QHash<QString, int> counts;
    QHash<QString, QString> spelling;
    QStringList order;
    for (const QString& v : values) {
        const QString trimmed = Normalizer::display(v);
        if (trimmed.isEmpty())
            continue;
        const QString key = Normalizer::identityKey(trimmed);
        if (!counts.contains(key))
            order << key;
        ++counts[key];
        if (!spelling.contains(key))
            spelling.insert(key, trimmed);
    }
    QString best;
    int bestCount = 0;
    for (const QString& key : std::as_const(order)) {
        if (counts[key] > bestCount) {
            bestCount = counts[key];
            best = key;
        }
    }
    return best.isEmpty() ? QString() : spelling.value(best);
}

}  // namespace

ImportService::ImportService(LibraryService& library, ITagService& tags, const GenreVocabulary& genres,
                             const ICodepageConverter& codepages, Database& db, const IClock& clock, QString libraryRoot)
    : m_library(library),
      m_tags(tags),
      m_genres(genres),
      m_genreMap(genres),
      m_fixer(codepages),
      m_db(db),
      m_clock(clock),
      m_root(std::move(libraryRoot))
{
}

ArtistStringSplitter::Exists ImportService::existsFn() const
{
    return [this](const QString& name) {
        ArtistRepo repo(m_db);
        auto found = repo.findByName(name);
        return found.ok() && found.value().has_value();
    };
}

// ---------------------------------------------------------------------------------------------
// Scan
// ---------------------------------------------------------------------------------------------

Result<ImportScan> ImportService::scan(const QStringList& paths, const JobContext& job)
{
    ImportScan result;
    const QStringList files = audioFilesIn(paths);
    TrackRepo tracks(m_db);
    qint64 done = 0;
    for (const QString& path : files) {
        if (job.cancelled())
            return Error::make(ErrorCode::Cancelled);
        job.report(QStringLiteral("scan"), done++, files.size());

        auto read = m_tags.read(path);
        if (!read.ok()) {
            result.skipped.append({path, read.error().symbol() + QStringLiteral(": ") + read.error().devMessage});
            continue;
        }
        auto hash = hashFile(path, job);
        if (!hash.ok()) {
            if (hash.error().code == ErrorCode::Cancelled)
                return hash.error();
            result.skipped.append({path, hash.error().devMessage});
            continue;
        }
        const QFileInfo info(path);
        ScannedTrack st;
        st.sourcePath = info.absoluteFilePath();
        st.sourceFolder = info.absolutePath();
        st.extension = info.suffix().toLower();
        st.tags = std::move(read).value();
        st.repairedFields = m_fixer.fix(st.tags).repairedFields;
        st.hash = std::move(hash).value();
        st.size = info.size();
        st.mtime = info.lastModified().toMSecsSinceEpoch();
        if (auto dup = tracks.findByHash(st.hash); dup.ok() && dup.value())
            st.duplicateOf = dup.value()->id;
        result.tracks.append(std::move(st));
    }
    job.report(QStringLiteral("scan"), files.size(), files.size());
    return result;
}

// ---------------------------------------------------------------------------------------------
// Proposal
// ---------------------------------------------------------------------------------------------

QList<CreditDraft> ImportService::creditsFor(const ScannedTrack& track, const QString& fallbackName) const
{
    QList<CreditDraft> out;
    for (const NamedCredit& c : ArtistStringSplitter::splitAll(track.tags.artists, existsFn()))
        out.append(CreditDraft(c.name, c.role));
    if (out.isEmpty() && !fallbackName.isEmpty())
        out.append(CreditDraft(fallbackName, CreditRole::Main));
    return out;
}

ImportProposal ImportService::propose(const ImportScan& scan, std::optional<ArtistId> preselectedArtist) const
{
    ImportProposal proposal;
    const int n = static_cast<int>(scan.tracks.size());
    if (n == 0)
        return proposal;

    // Order: track number (unknown last), then file name.
    QList<int> order;
    for (int i = 0; i < n; ++i)
        order << i;
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const int na = scan.tracks[a].tags.trackNumber > 0 ? scan.tracks[a].tags.trackNumber : 1 << 20;
        const int nb = scan.tracks[b].tags.trackNumber > 0 ? scan.tracks[b].tags.trackNumber : 1 << 20;
        if (na != nb)
            return na < nb;
        return collator.compare(scan.tracks[a].sourcePath, scan.tracks[b].sourcePath) < 0;
    });

    // "Do the tracks share album metadata, or does more than half feature the same artist?"
    QStringList albums;
    QStringList primaryArtists;
    QStringList mainSets;  // the main artists of each track, joined: "A, B" on every track makes both of them the album artists
    bool allHaveAlbum = true;
    QSet<QString> albumKeys;
    for (const ScannedTrack& t : scan.tracks) {
        if (t.tags.album.trimmed().isEmpty())
            allHaveAlbum = false;
        else
            albumKeys.insert(Normalizer::identityKey(t.tags.album));
        albums << t.tags.album;
        const auto credits = ArtistStringSplitter::splitAll(t.tags.artists, existsFn());
        if (!credits.isEmpty()) {
            primaryArtists << credits.first().name;
            QStringList mains;
            for (const NamedCredit& c : credits)
                if (c.role == CreditRole::Main)
                    mains << c.name;
            mainSets << mains.join(QLatin1Char('\x1f'));
        }
    }
    const bool sharedAlbum = allHaveAlbum && albumKeys.size() == 1;
    bool majorityArtist = false;
    QHash<QString, int> artistCounts;
    for (const QString& a : std::as_const(primaryArtists))
        ++artistCounts[Normalizer::identityKey(a)];
    for (auto it = artistCounts.cbegin(); it != artistCounts.cend(); ++it)
        majorityArtist = majorityArtist || it.value() * 2 > n;

    const bool isAlbum = preselectedArtist.has_value() || sharedAlbum || majorityArtist;

    auto titleOf = [](const ScannedTrack& t) {
        const QString title = Normalizer::display(t.tags.title);
        return title.isEmpty() ? QFileInfo(t.sourcePath).completeBaseName() : title;
    };

    if (!isAlbum) {
        proposal.kind = ImportKind::Playlist;
        QStringList folders;
        for (const ScannedTrack& t : scan.tracks)
            folders << t.sourceFolder;
        folders.removeDuplicates();
        if (folders.size() == 1)
            proposal.playlist.name = QFileInfo(folders.first()).fileName();
        for (int i : std::as_const(order)) {
            ImportTrackForm f;
            f.scanIndex = i;
            f.title = titleOf(scan.tracks[i]);
            f.credits = creditsFor(scan.tracks[i], PathSanitizer::fallbackFor(NameKind::Artist));
            proposal.playlist.tracks.append(f);
        }
        return proposal;
    }

    proposal.kind = ImportKind::Album;
    ImportAlbumForm& form = proposal.album;
    form.title = sharedAlbum ? Normalizer::display(scan.tracks.first().tags.album) : mostFrequent(albums);
    if (n == 1)
        form.title = titleOf(scan.tracks.first());  // a single track: the album is named like the track
    form.type = n <= 3 ? AlbumType::Single : (n <= 7 ? AlbumType::Ep : AlbumType::Album);

    // authors
    if (preselectedArtist) {
        ArtistRepo repo(m_db);
        if (auto a = repo.get(*preselectedArtist); a.ok() && a.value())
            form.authors << a.value()->name;
    } else {
        QStringList albumArtistValues;
        for (const ScannedTrack& t : scan.tracks) {
            const auto credits = ArtistStringSplitter::splitAll(t.tags.albumArtists, existsFn());
            QStringList names;
            for (const NamedCredit& c : credits)
                names << c.name;
            albumArtistValues << names.join(QStringLiteral("\x1f"));
        }
        const QString common = mostFrequent(albumArtistValues);
        if (!common.isEmpty())
            form.authors = common.split(QStringLiteral("\x1f"), Qt::SkipEmptyParts);
        else if (majorityArtist) {
            const QString mains = mostFrequent(mainSets);
            form.authors = mains.split(QLatin1Char('\x1f'), Qt::SkipEmptyParts);
            if (form.authors.isEmpty())
                form.authors << mostFrequent(primaryArtists);
        }
    }

    // date: the most common one
    QHash<QString, int> dateCounts;
    ReleaseDate bestDate;
    int bestDateCount = 0;
    for (const ScannedTrack& t : scan.tracks) {
        if (t.tags.date.year <= 0)
            continue;
        const QString key = QStringLiteral("%1-%2-%3").arg(t.tags.date.year).arg(t.tags.date.month).arg(t.tags.date.day);
        if (++dateCounts[key] > bestDateCount) {
            bestDateCount = dateCounts[key];
            bestDate = t.tags.date;
        }
    }
    form.date = bestDate;

    // tags: vocabulary genres found in the files, most frequent first
    QHash<QString, int> genreCounts;
    QStringList genreOrder;
    for (const ScannedTrack& t : scan.tracks) {
        for (const QString& key : m_genreMap.resolveAll(t.tags.genres)) {
            if (!genreCounts.contains(key))
                genreOrder << key;
            ++genreCounts[key];
        }
    }
    std::stable_sort(genreOrder.begin(), genreOrder.end(), [&](const QString& a, const QString& b) { return genreCounts[a] > genreCounts[b]; });
    form.tags = genreOrder;

    // cover: a file next to the music, else the picture of the first track
    QStringList folders;
    for (const ScannedTrack& t : scan.tracks)
        folders << t.sourceFolder;
    folders.removeDuplicates();
    const QString firstPath = scan.tracks[order.first()].sourcePath;
    if (folders.size() == 1) {
        form.coverPath = CoverDiscovery::findInFolder(folders.first());
    }
    if (form.coverPath.isEmpty()) {
        if (auto embedded = m_tags.readCover(firstPath); embedded.ok() && embedded.value())
            form.embeddedCover = embedded.value();
    }

    const QString primary = form.authors.isEmpty() ? QString() : form.authors.first();
    for (int i : std::as_const(order)) {
        ImportTrackForm f;
        f.scanIndex = i;
        f.title = titleOf(scan.tracks[i]);
        f.credits = creditsFor(scan.tracks[i], primary);
        form.tracks.append(f);
    }
    return proposal;
}

// ---------------------------------------------------------------------------------------------
// Commit
// ---------------------------------------------------------------------------------------------

QList<TrackInput> ImportService::inputsFor(const ImportScan& scan, const QList<ImportTrackForm>& forms, bool& ok) const
{
    ok = true;
    QList<TrackInput> inputs;
    for (const ImportTrackForm& f : forms) {
        if (f.scanIndex < 0 || f.scanIndex >= scan.tracks.size()) {
            ok = false;
            return {};
        }
        const ScannedTrack& s = scan.tracks[f.scanIndex];
        TrackInput in;
        TrackDraft& d = in.draft;
        d.title = f.title;
        d.durationMs = s.tags.durationMs;
        d.extension = s.extension;
        d.codec = s.tags.codec;
        d.contentHash = s.hash;
        d.fileSize = s.size;
        d.fileMtime = s.mtime;
        d.hasEmbeddedCover = s.tags.hasCover;
        d.credits = f.credits;
        d.sourcePath = s.sourcePath;
        inputs.append(in);
    }
    return inputs;
}

namespace {

// An embedded picture is written to .library/tmp first so the journaled storage can copy it like any file.
struct TempCover {
    QString path;
    ~TempCover()
    {
        if (!path.isEmpty())
            (void)ownfiles::removeFile(path);
    }
};

}  // namespace

Result<ImportResult> ImportService::commitAlbum(const ImportScan& scan, const ImportAlbumForm& form, const JobContext& job)
{
    if (form.authors.isEmpty())
        return Error::make(ErrorCode::NoArtist);
    bool ok = false;
    QList<TrackInput> inputs = inputsFor(scan, form.tracks, ok);
    if (!ok)
        return Error::make(ErrorCode::InvalidArgument, QStringLiteral("form refers to a track that was not scanned"));
    for (TrackInput& in : inputs) {
        if (in.draft.credits.isEmpty())
            in.draft.credits.append(CreditDraft(form.authors.first(), CreditRole::Main));
    }

    TempCover temp;
    AlbumDraft draft;
    draft.primaryArtistName = form.authors.first();
    draft.coAuthorNames = form.authors.mid(1);
    draft.title = form.title;
    draft.type = form.type;
    draft.date = form.date;
    draft.tags = form.tags;
    draft.coverPath = form.coverPath;
    if (draft.coverPath.isEmpty() && form.embeddedCover) {
        const QString dir = QDir(m_root).filePath(QStringLiteral(".library/tmp"));
        QDir().mkpath(dir);
        temp.path = QDir(dir).filePath(QUuid::createUuid().toString(QUuid::Id128) + QLatin1Char('.') + form.embeddedCover->extension());
        SEMPLICE_TRY(CoverDiscovery::writeFile(*form.embeddedCover, temp.path));
        draft.coverPath = temp.path;
    }
    draft.tracks = inputs;

    auto album = m_library.createAlbum(draft, job);
    if (!album.ok())
        return album.error();
    ImportResult r;
    r.album = album.value();
    r.report = m_library.lastReport();
    r.imported = static_cast<int>(r.report.tracksCreated.size());
    r.duplicates = static_cast<int>(r.report.tracksReused.size());
    return r;
}

Result<ImportResult> ImportService::commitPlaylist(const ImportScan& scan, const ImportPlaylistForm& form, const JobContext& job)
{
    bool ok = false;
    QList<TrackInput> inputs = inputsFor(scan, form.tracks, ok);
    if (!ok)
        return Error::make(ErrorCode::InvalidArgument, QStringLiteral("form refers to a track that was not scanned"));
    for (TrackInput& in : inputs) {
        if (in.draft.credits.isEmpty())  // a playlist file is named "Title - Artist": never without an artist
            in.draft.credits.append(CreditDraft(PathSanitizer::fallbackFor(NameKind::Artist), CreditRole::Main));
    }
    PlaylistDraft draft;
    draft.name = form.name;
    draft.coverPath = form.coverPath;
    draft.tracks = inputs;

    auto playlist = m_library.createPlaylist(draft, job);
    if (!playlist.ok())
        return playlist.error();
    ImportResult r;
    r.playlist = playlist.value();
    r.report = m_library.lastReport();
    r.imported = static_cast<int>(r.report.tracksCreated.size());
    r.duplicates = static_cast<int>(r.report.tracksReused.size());
    return r;
}

Result<ImportResult> ImportService::appendToAlbum(AlbumId album, const ImportScan& scan, const QList<ImportTrackForm>& tracks,
                                                  const JobContext& job)
{
    bool ok = false;
    const QList<TrackInput> inputs = inputsFor(scan, tracks, ok);
    if (!ok)
        return Error::make(ErrorCode::InvalidArgument, QStringLiteral("form refers to a track that was not scanned"));
    SEMPLICE_TRY(m_library.appendTracksToAlbum(album, inputs, job));
    ImportResult r;
    r.album = album;
    r.report = m_library.lastReport();
    r.imported = static_cast<int>(r.report.tracksCreated.size());
    r.duplicates = static_cast<int>(r.report.tracksReused.size());
    return r;
}

Result<ImportResult> ImportService::appendToPlaylist(PlaylistId playlist, const ImportScan& scan, const QList<ImportTrackForm>& tracks,
                                                     const JobContext& job)
{
    bool ok = false;
    const QList<TrackInput> inputs = inputsFor(scan, tracks, ok);
    if (!ok)
        return Error::make(ErrorCode::InvalidArgument, QStringLiteral("form refers to a track that was not scanned"));
    SEMPLICE_TRY(m_library.appendTracksToPlaylist(playlist, inputs, job));
    ImportResult r;
    r.playlist = playlist;
    r.report = m_library.lastReport();
    r.imported = static_cast<int>(r.report.tracksCreated.size());
    r.duplicates = static_cast<int>(r.report.tracksReused.size());
    return r;
}

}  // namespace semplice

namespace semplice {

std::optional<CoverImage> ImportService::embeddedCoverOf(const QString& sourcePath) const
{
    if (auto cover = m_tags.readCover(sourcePath); cover.ok() && cover.value())
        return cover.value();
    return std::nullopt;
}

}  // namespace semplice
