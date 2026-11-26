#include "core/share/ShareService.h"

#include "core/services/LibraryServiceInternal.h"
#include "core/share/SmplFile.h"
#include "core/util/Clock.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>

namespace semplice {

namespace {

QJsonArray toJson(const QStringList& list)
{
    QJsonArray a;
    for (const QString& s : list)
        a.append(s);
    return a;
}

QStringList toList(const QJsonValue& v)
{
    QStringList out;
    for (const QJsonValue& item : v.toArray())
        out << item.toString();
    return out;
}

QString extensionOf(const QString& path)
{
    return QFileInfo(path).suffix().toLower();
}

}  // namespace

ShareService::ShareService(Database& db, LibraryService& library, QString libraryRoot)
    : m_db(db), m_library(library), m_root(std::move(libraryRoot))
{
}

QString ShareService::absolute(const QString& relative) const
{
    return relative.isEmpty() ? QString() : QDir(m_root).filePath(relative);
}

QString ShareService::artistNames(TrackId track)
{
    ArtistRepo artists(m_db);
    TrackRepo tracks(m_db);
    QStringList names;
    if (auto credits = tracks.credits(track); credits.ok())
        for (const TrackCredit& c : credits.value())
            if (auto a = artists.get(c.artist); a.ok() && a.value())
                names << a.value()->name;
    return names.join(QStringLiteral(", "));
}

// ------------------------------------------------------------------------------------------ describe

Result<ShareTree> ShareService::describe(ShareKind kind, qint64 id)
{
    ArtistRepo artists(m_db);
    AlbumRepo albums(m_db);
    TrackRepo tracks(m_db);
    PlaylistRepo playlists(m_db);
    ShareTree tree;
    tree.kind = kind;
    tree.id = id;

    auto trackNode = [&](TrackId t, const QString& cover) -> std::optional<ShareTrackNode> {
        auto found = tracks.get(t);
        if (!found.ok() || !found.value())
            return std::nullopt;
        const Track& track = *found.value();
        return ShareTrackNode{t.value, track.title, artistNames(t), track.durationMs, cover};
    };
    auto albumNode = [&](const Album& a) -> Result<ShareAlbumNode> {
        ShareAlbumNode node;
        node.id = a.id.value;
        node.title = a.title;
        node.cover = a.coverPath;
        node.type = static_cast<int>(a.type);
        node.year = a.date.year;
        node.month = a.date.month;
        node.day = a.date.day;
        if (auto t = TagRepo(m_db).tags(TagOwner::Album, a.id.value); t.ok())
            node.tags = t.value();
        QStringList names;
        if (auto p = artists.get(a.primaryArtist); p.ok() && p.value())
            names << p.value()->name;
        SEMPLICE_ASSIGN(co, albums.coAuthors(a.id));
        for (const CoAuthor& c : co)
            if (auto x = artists.get(c.artist); x.ok() && x.value())
                names << x.value()->name;
        node.artists = names.join(QStringLiteral(", "));
        node.authors = names;
        SEMPLICE_ASSIGN(ids, albums.tracks(a.id));
        for (TrackId t : ids)
            if (auto n = trackNode(t, a.coverPath))
                node.tracks.append(*n);
        return node;
    };
    auto addArtist = [&](ArtistId a, bool main) -> Result<void> {
        for (const ShareArtistNode& n : std::as_const(tree.artists))
            if (n.id == a.value)
                return {};
        SEMPLICE_ASSIGN(artist, artists.get(a));
        if (!artist)
            return {};
        ShareArtistNode node{a.value, artist->name, artist->avatarPath, main, artist->userHidden, {}, false};
        if (auto t = TagRepo(m_db).tags(TagOwner::Artist, a.value); t.ok())
            node.tags = t.value();
        tree.artists.append(node);
        return {};
    };

    switch (kind) {
    case ShareKind::Album: {
        SEMPLICE_ASSIGN(album, albums.get(AlbumId(id)));
        if (!album || album->isUnsorted)
            return Error::make(ErrorCode::NotFound, QStringLiteral("album"));
        SEMPLICE_ASSIGN(node, albumNode(*album));
        tree.title = album->title;
        tree.cover = album->coverPath;
        SEMPLICE_TRY(addArtist(album->primaryArtist, true));
        SEMPLICE_ASSIGN(co, albums.coAuthors(album->id));
        for (const CoAuthor& c : co)
            SEMPLICE_TRY(addArtist(c.artist, false));
        tree.albums.append(node);
        break;
    }
    case ShareKind::Artist: {
        SEMPLICE_ASSIGN(artist, artists.get(ArtistId(id)));
        if (!artist)
            return Error::make(ErrorCode::NotFound, QStringLiteral("artist"));
        tree.title = artist->name;
        tree.cover = artist->avatarPath;
        SEMPLICE_TRY(addArtist(artist->id, true));
        QList<Album> mine;
        SEMPLICE_ASSIGN(primary, albums.byPrimary(artist->id));
        SEMPLICE_ASSIGN(featured, albums.byCoAuthor(artist->id));
        for (const Album& a : primary)
            if (!a.isUnsorted)
                mine.append(a);
        for (const Album& a : featured)
            if (!a.isUnsorted)
                mine.append(a);
        for (const Album& a : std::as_const(mine)) {
            SEMPLICE_ASSIGN(node, albumNode(a));
            tree.albums.append(node);
            SEMPLICE_TRY(addArtist(a.primaryArtist, false));
            SEMPLICE_ASSIGN(co, albums.coAuthors(a.id));
            for (const CoAuthor& c : co)
                SEMPLICE_TRY(addArtist(c.artist, false));
        }
        break;
    }
    case ShareKind::Playlist: {
        SEMPLICE_ASSIGN(playlist, playlists.get(PlaylistId(id)));
        if (!playlist)
            return Error::make(ErrorCode::NotFound, QStringLiteral("playlist"));
        tree.title = playlist->name;
        tree.cover = playlist->coverPath;
        SEMPLICE_ASSIGN(ids, playlists.tracks(playlist->id));
        for (TrackId t : ids)
            if (auto n = trackNode(t, {}))
                tree.tracks.append(*n);
        break;
    }
    }
    return tree;
}

// ------------------------------------------------------------------------------------------ export

Result<void> ShareService::exportTo(const QString& path, const ShareSelection& selection, const JobContext& job)
{
    ArtistRepo artists(m_db);
    AlbumRepo albums(m_db);
    TrackRepo tracks(m_db);
    TagRepo tags(m_db);
    PlaylistRepo playlists(m_db);
    SmplWriter writer;

    auto blobFor = [&](const QString& relative, const QString& prefix, int n) -> Result<QString> {
        if (relative.isEmpty())
            return QString();
        const QString id = prefix + QString::number(n);
        SEMPLICE_TRY(writer.addFile(id, QFileInfo(relative).fileName(), absolute(relative)));
        return id;
    };
    int counter = 0;
    auto trackJson = [&](TrackId t) -> Result<QJsonObject> {
        SEMPLICE_ASSIGN(track, tracks.get(t));
        if (!track)
            return Error::make(ErrorCode::NotFound, QStringLiteral("track"));
        const QString hex = QString::fromLatin1(track->contentHash.toHex());
        const QString blob = QStringLiteral("a-") + hex;  // one copy of a file, however many times it is listed
        SEMPLICE_TRY(writer.addFile(blob, QFileInfo(track->filePath).fileName(), absolute(track->filePath)));
        QJsonArray credits;
        SEMPLICE_ASSIGN(list, tracks.credits(t));
        for (const TrackCredit& c : list)
            if (auto a = artists.get(c.artist); a.ok() && a.value())
                credits.append(QJsonObject{{QStringLiteral("name"), a.value()->name}, {QStringLiteral("role"), toDbString(c.role)}});
        SEMPLICE_ASSIGN(own, tags.tags(TagOwner::Track, t.value));
        job.report(QStringLiteral("scan"), ++counter);
        return QJsonObject{{QStringLiteral("title"), track->title},
                           {QStringLiteral("artists"), credits},
                           {QStringLiteral("durationMs"), static_cast<double>(track->durationMs)},
                           {QStringLiteral("extension"), extensionOf(track->filePath)},
                           {QStringLiteral("codec"), track->codec},
                           {QStringLiteral("hash"), hex},
                           {QStringLiteral("size"), static_cast<double>(track->fileSize)},
                           {QStringLiteral("blob"), blob},
                           {QStringLiteral("embeddedCover"), track->hasEmbeddedCover},
                           {QStringLiteral("tags"), toJson(own)}};
    };
    auto albumJson = [&](const Album& a, int n) -> Result<QJsonObject> {
        QStringList authors;
        if (auto p = artists.get(a.primaryArtist); p.ok() && p.value())
            authors << p.value()->name;
        SEMPLICE_ASSIGN(co, albums.coAuthors(a.id));
        for (const CoAuthor& c : co)
            if (auto x = artists.get(c.artist); x.ok() && x.value())
                authors << x.value()->name;
        SEMPLICE_ASSIGN(ids, albums.tracks(a.id));
        QJsonArray trackList;
        for (TrackId t : ids) {
            if (!selection.tracks.contains(t.value))
                continue;
            SEMPLICE_ASSIGN(tj, trackJson(t));
            trackList.append(tj);
        }
        SEMPLICE_ASSIGN(albumTags, tags.tags(TagOwner::Album, a.id.value));
        QString cover;
        if (selection.includeCover || selection.kind == ShareKind::Artist) {
            SEMPLICE_ASSIGN(c, blobFor(a.coverPath, QStringLiteral("c-album-"), n));
            cover = c;
        }
        return QJsonObject{{QStringLiteral("title"), a.title},
                           {QStringLiteral("type"), toDbString(a.type)},
                           {QStringLiteral("year"), a.date.year},
                           {QStringLiteral("month"), a.date.month},
                           {QStringLiteral("day"), a.date.day},
                           {QStringLiteral("tags"), toJson(albumTags)},
                           {QStringLiteral("authors"), toJson(authors)},
                           {QStringLiteral("cover"), cover.isEmpty() ? QJsonValue() : QJsonValue(cover)},
                           {QStringLiteral("tracks"), trackList}};
    };

    QJsonObject body;
    body.insert(QStringLiteral("app"), QStringLiteral("Semplice"));
    body.insert(QStringLiteral("created"), static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
    QJsonArray albumList, artistList, playlistList;
    QString name;

    // the albums (album and artist kind)
    QList<Album> chosen;
    if (selection.kind == ShareKind::Album) {
        SEMPLICE_ASSIGN(album, albums.get(AlbumId(selection.id)));
        if (!album || album->isUnsorted)
            return Error::make(ErrorCode::NotFound, QStringLiteral("album"));
        chosen.append(*album);
        name = album->title;
    } else if (selection.kind == ShareKind::Artist) {
        SEMPLICE_ASSIGN(artist, artists.get(ArtistId(selection.id)));
        if (!artist)
            return Error::make(ErrorCode::NotFound, QStringLiteral("artist"));
        name = artist->name;
        for (qint64 id : std::as_const(selection.albums))
            if (auto a = albums.get(AlbumId(id)); a.ok() && a.value() && !a.value()->isUnsorted)
                chosen.append(*a.value());
    }
    int n = 0;
    QSet<qint64> artistIds;
    for (const Album& a : std::as_const(chosen)) {
        SEMPLICE_ASSIGN(aj, albumJson(a, ++n));
        albumList.append(aj);
        artistIds.insert(a.primaryArtist.value);
        SEMPLICE_ASSIGN(co, albums.coAuthors(a.id));
        for (const CoAuthor& c : co)
            artistIds.insert(c.artist.value);
        if (job.cancelled())
            return Error::make(ErrorCode::Cancelled);
    }
    // the playlist
    if (selection.kind == ShareKind::Playlist) {
        SEMPLICE_ASSIGN(playlist, playlists.get(PlaylistId(selection.id)));
        if (!playlist)
            return Error::make(ErrorCode::NotFound, QStringLiteral("playlist"));
        name = playlist->name;
        SEMPLICE_ASSIGN(ids, playlists.tracks(playlist->id));
        QJsonArray trackList;
        for (TrackId t : ids) {
            if (!selection.tracks.contains(t.value))
                continue;
            SEMPLICE_ASSIGN(tj, trackJson(t));
            trackList.append(tj);
            if (job.cancelled())
                return Error::make(ErrorCode::Cancelled);
        }
        QString cover;
        if (selection.includeCover) {
            SEMPLICE_ASSIGN(c, blobFor(playlist->coverPath, QStringLiteral("c-playlist-"), 1));
            cover = c;
        }
        playlistList.append(QJsonObject{{QStringLiteral("name"), playlist->name},
                                        {QStringLiteral("cover"), cover.isEmpty() ? QJsonValue() : QJsonValue(cover)},
                                        {QStringLiteral("tracks"), trackList}});
    }
    // the artists: their tags, and their picture when it was ticked
    for (qint64 id : std::as_const(artistIds)) {
        SEMPLICE_ASSIGN(artist, artists.get(ArtistId(id)));
        if (!artist)
            continue;
        SEMPLICE_ASSIGN(artistTags, tags.tags(TagOwner::Artist, id));
        QString avatar;
        if (selection.avatarArtists.contains(id)) {
            SEMPLICE_ASSIGN(a, blobFor(artist->avatarPath, QStringLiteral("c-artist-"), static_cast<int>(id)));
            avatar = a;
        }
        const bool hiddenStatus = selection.artistHidden.value(id, artist->userHidden);
        artistList.append(QJsonObject{{QStringLiteral("name"), artist->name},
                                      {QStringLiteral("hidden"), hiddenStatus},
                                      {QStringLiteral("tags"), toJson(artistTags)},
                                      {QStringLiteral("avatar"), avatar.isEmpty() ? QJsonValue() : QJsonValue(avatar)}});
    }
    body.insert(QStringLiteral("kind"), selection.kind == ShareKind::Artist ? QStringLiteral("artist")
                                         : selection.kind == ShareKind::Album ? QStringLiteral("album") : QStringLiteral("playlist"));
    body.insert(QStringLiteral("name"), name);
    body.insert(QStringLiteral("albums"), albumList);
    body.insert(QStringLiteral("artists"), artistList);
    body.insert(QStringLiteral("playlists"), playlistList);
    return writer.write(path, body, job);
}

// ------------------------------------------------------------------------------------------ import

Result<ShareTree> ShareService::describeFile(const QString& path, const QString& previewDir)
{
    SmplReader reader;
    SEMPLICE_TRY(reader.open(path));
    const QJsonObject manifest = reader.manifest();
    int previewCounter = 0;
    auto preview = [&](const QString& blob) -> QString {
        if (previewDir.isEmpty() || blob.isEmpty() || !reader.hasBlob(blob))
            return {};
        const QString target = QDir(previewDir).filePath(QStringLiteral("p%1").arg(++previewCounter));
        return reader.extract(blob, target).ok() ? target : QString();
    };
    ArtistRepo artists(m_db);
    AlbumRepo albums(m_db);
    TrackRepo tracks(m_db);
    PlaylistRepo playlists(m_db);

    ShareTree tree;
    tree.importing = true;
    const QString kind = manifest.value(QStringLiteral("kind")).toString();
    tree.kind = kind == QLatin1String("artist") ? ShareKind::Artist : kind == QLatin1String("playlist") ? ShareKind::Playlist : ShareKind::Album;
    tree.title = manifest.value(QStringLiteral("name")).toString();

    auto trackNode = [&](const QJsonObject& t) -> Result<ShareTrackNode> {
        ShareTrackNode node;
        node.title = t.value(QStringLiteral("title")).toString();
        node.durationMs = static_cast<qint64>(t.value(QStringLiteral("durationMs")).toDouble());
        QStringList names;
        for (const QJsonValue& c : t.value(QStringLiteral("artists")).toArray())
            names << c.toObject().value(QStringLiteral("name")).toString();
        node.artists = names.join(QStringLiteral(", "));
        SEMPLICE_ASSIGN(known, tracks.findByHash(QByteArray::fromHex(t.value(QStringLiteral("hash")).toString().toLatin1())));
        node.existing = known.has_value();
        return node;
    };

    QString mainArtist = kind == QLatin1String("artist") ? tree.title : QString();
    for (const QJsonValue& av : manifest.value(QStringLiteral("albums")).toArray()) {
        const QJsonObject a = av.toObject();
        ShareAlbumNode node;
        node.title = a.value(QStringLiteral("title")).toString();
        node.type = static_cast<int>(albumTypeFromString(a.value(QStringLiteral("type")).toString()).value_or(AlbumType::Album));
        node.year = a.value(QStringLiteral("year")).toInt();
        node.month = a.value(QStringLiteral("month")).toInt();
        node.day = a.value(QStringLiteral("day")).toInt();
        node.tags = toList(a.value(QStringLiteral("tags")));
        node.authors = toList(a.value(QStringLiteral("authors")));
        node.cover = preview(a.value(QStringLiteral("cover")).toString());
        node.artists = node.authors.join(QStringLiteral(", "));
        if (mainArtist.isEmpty() && !node.authors.isEmpty())
            mainArtist = node.authors.first();
        if (!node.authors.isEmpty()) {
            SEMPLICE_ASSIGN(primary, artists.findByName(node.authors.first()));
            if (primary) {
                SEMPLICE_ASSIGN(clash, albums.findByTitle(primary->id, node.title));
                node.existing = clash.has_value();
            }
        }
        for (const QJsonValue& tv : a.value(QStringLiteral("tracks")).toArray()) {
            SEMPLICE_ASSIGN(t, trackNode(tv.toObject()));
            node.tracks.append(t);
        }
        tree.albums.append(node);
    }
    for (const QJsonValue& pv : manifest.value(QStringLiteral("playlists")).toArray()) {
        const QJsonObject p = pv.toObject();
        SharePlaylistNode node;
        node.name = p.value(QStringLiteral("name")).toString();
        node.cover = preview(p.value(QStringLiteral("cover")).toString());
        SEMPLICE_ASSIGN(clash, playlists.findByName(node.name));
        node.existing = clash.has_value();
        for (const QJsonValue& tv : p.value(QStringLiteral("tracks")).toArray()) {
            SEMPLICE_ASSIGN(t, trackNode(tv.toObject()));
            node.tracks.append(t);
        }
        tree.playlists.append(node);
    }
    for (const QJsonValue& av : manifest.value(QStringLiteral("artists")).toArray()) {
        const QJsonObject o = av.toObject();
        ShareArtistNode node;
        node.name = o.value(QStringLiteral("name")).toString();
        node.tags = toList(o.value(QStringLiteral("tags")));
        node.hidden = o.value(QStringLiteral("hidden")).toBool();
        node.main = node.name == mainArtist;
        node.avatar = preview(o.value(QStringLiteral("avatar")).toString());
        SEMPLICE_ASSIGN(found, artists.findByName(node.name));
        node.existing = found.has_value();
        if (found)
            node.hidden = found->userHidden;   // what the library says wins for an artist it has
        tree.artists.append(node);
    }
    std::stable_sort(tree.artists.begin(), tree.artists.end(), [](const ShareArtistNode& a, const ShareArtistNode& b) { return a.main && !b.main; });
    return tree;
}


Result<ShareImportReport> ShareService::importFrom(const QString& path, const JobContext& job, const ImportSelection& selection)
{
    SmplReader reader;
    SEMPLICE_TRY(reader.open(path));
    SEMPLICE_TRY(reader.verifyAll(job));  // nothing changes in the library before the whole file is known to be good
    const QJsonObject manifest = reader.manifest();

    QTemporaryDir temp;  // the audio is unpacked here, the library copies it from here
    if (!temp.isValid())
        return Error::make(ErrorCode::IoError, QStringLiteral("no temporary folder"));
    int fileCounter = 0;
    auto unpack = [&](const QString& blob, const QString& extension) -> Result<QString> {
        if (blob.isEmpty() || !reader.hasBlob(blob))
            return QString();
        const QString target = temp.filePath(QString::number(++fileCounter) + (extension.isEmpty() ? QString() : QLatin1Char('.') + extension));
        SEMPLICE_TRY(reader.extract(blob, target, job));
        return target;
    };

    ArtistRepo artists(m_db);
    AlbumRepo albums(m_db);
    TrackRepo tracks(m_db);
    ShareImportReport report;

    // a track of the file: an existing track (same audio) is only linked, a new one is unpacked
    auto trackInput = [&](const QJsonObject& t, bool& isNew) -> Result<TrackInput> {
        TrackInput in;
        const QByteArray hash = QByteArray::fromHex(t.value(QStringLiteral("hash")).toString().toLatin1());
        SEMPLICE_ASSIGN(known, tracks.findByHash(hash));
        if (known) {
            in.existing = known->id;
            isNew = false;
            return in;
        }
        isNew = true;
        const QString extension = t.value(QStringLiteral("extension")).toString();
        SEMPLICE_ASSIGN(file, unpack(t.value(QStringLiteral("blob")).toString(), extension));
        if (file.isEmpty())
            return Error::make(ErrorCode::InvalidArgument, QStringLiteral("smpl: a track has no file"));
        in.draft.title = t.value(QStringLiteral("title")).toString();
        in.draft.durationMs = static_cast<qint64>(t.value(QStringLiteral("durationMs")).toDouble());
        in.draft.extension = extension;
        in.draft.codec = t.value(QStringLiteral("codec")).toString();
        in.draft.contentHash = hash;
        in.draft.sourceHash = QByteArray::fromHex(reader.blobHash(t.value(QStringLiteral("blob")).toString()).toLatin1());
        in.draft.fileSize = static_cast<qint64>(t.value(QStringLiteral("size")).toDouble());
        in.draft.hasEmbeddedCover = t.value(QStringLiteral("embeddedCover")).toBool();
        in.draft.sourcePath = file;
        in.draft.tags = toList(t.value(QStringLiteral("tags")));
        for (const QJsonValue& c : t.value(QStringLiteral("artists")).toArray()) {
            const QJsonObject o = c.toObject();
            const auto role = creditRoleFromString(o.value(QStringLiteral("role")).toString());
            in.draft.credits.append(CreditDraft(o.value(QStringLiteral("name")).toString(), role.value_or(CreditRole::Main)));
        }
        return in;
    };

    const QJsonArray albumList = manifest.value(QStringLiteral("albums")).toArray();
    const QJsonArray playlistList = manifest.value(QStringLiteral("playlists")).toArray();
    // the artists the library does not know yet: only they get the status chosen on the import page
    QSet<QString> knownBefore;
    for (const QJsonValue& av : manifest.value(QStringLiteral("artists")).toArray()) {
        const QString name = av.toObject().value(QStringLiteral("name")).toString();
        SEMPLICE_ASSIGN(found, artists.findByName(name));
        if (found)
            knownBefore.insert(name);
    }
    int step = 0;
    const int steps = static_cast<int>(albumList.size() + playlistList.size());

    for (int albumIndex = 0; albumIndex < albumList.size(); ++albumIndex) {
        job.report(QStringLiteral("import"), step++, steps);
        if (!selection.has(QStringLiteral("album:%1").arg(albumIndex)))
            continue;
        const QJsonObject a = albumList.at(albumIndex).toObject();
        const QString title = a.value(QStringLiteral("title")).toString();
        const QStringList authors = toList(a.value(QStringLiteral("authors")));
        QJsonArray trackList;
        {
            const QJsonArray all = a.value(QStringLiteral("tracks")).toArray();
            for (int j = 0; j < all.size(); ++j)
                if (selection.has(QStringLiteral("track:%1:%2").arg(albumIndex).arg(j)))
                    trackList.append(all.at(j));
        }
        if (title.isEmpty() || authors.isEmpty() || trackList.isEmpty())
            continue;
        // an album that is there already (same title, same first artist) is skipped
        SEMPLICE_ASSIGN(primary, artists.findByName(authors.first()));
        if (primary) {
            SEMPLICE_ASSIGN(existing, albums.findByTitle(primary->id, title));
            if (existing) {
                ++report.albumsSkipped;
                report.tracksSkipped += static_cast<int>(trackList.size());
                continue;
            }
        }
        AlbumDraft draft;
        draft.primaryArtistName = authors.first();
        draft.coAuthorNames = authors.mid(1);
        draft.title = title;
        draft.type = albumTypeFromString(a.value(QStringLiteral("type")).toString()).value_or(AlbumType::Album);
        draft.date = {a.value(QStringLiteral("year")).toInt(), a.value(QStringLiteral("month")).toInt(), a.value(QStringLiteral("day")).toInt()};
        draft.tags = toList(a.value(QStringLiteral("tags")));
        SEMPLICE_ASSIGN(cover, unpack(a.value(QStringLiteral("cover")).toString(), QStringLiteral("jpg")));
        draft.coverPath = cover;
        int newTracks = 0;
        for (const QJsonValue& tv : trackList) {
            bool isNew = false;
            SEMPLICE_ASSIGN(input, trackInput(tv.toObject(), isNew));
            draft.tracks.append(input);
            isNew ? ++newTracks : ++report.tracksSkipped;
        }
        auto created = m_library.createAlbum(draft, job);
        if (!created.ok())
            return created.error();
        ++report.albumsAdded;
        report.tracksAdded += newTracks;
    }

    for (int playlistIndex = 0; playlistIndex < playlistList.size(); ++playlistIndex) {
        job.report(QStringLiteral("import"), step++, steps);
        if (!selection.has(QStringLiteral("playlist:%1").arg(playlistIndex)))
            continue;
        const QJsonObject p = playlistList.at(playlistIndex).toObject();
        QJsonArray trackList;
        {
            const QJsonArray all = p.value(QStringLiteral("tracks")).toArray();
            for (int j = 0; j < all.size(); ++j)
                if (selection.has(QStringLiteral("ptrack:%1:%2").arg(playlistIndex).arg(j)))
                    trackList.append(all.at(j));
        }
        if (trackList.isEmpty())
            continue;
        PlaylistRepo playlists(m_db);
        QString name = p.value(QStringLiteral("name")).toString();
        for (int n = 2;; ++n) {  // a playlist of that name exists: "Name (2)"
            SEMPLICE_ASSIGN(clash, playlists.findByName(name));
            if (!clash)
                break;
            name = p.value(QStringLiteral("name")).toString() + QStringLiteral(" (%1)").arg(n);
        }
        PlaylistDraft draft;
        draft.name = name;
        SEMPLICE_ASSIGN(cover, unpack(p.value(QStringLiteral("cover")).toString(), QStringLiteral("jpg")));
        draft.coverPath = cover;
        int newTracks = 0;
        for (const QJsonValue& tv : trackList) {
            bool isNew = false;
            SEMPLICE_ASSIGN(input, trackInput(tv.toObject(), isNew));
            draft.tracks.append(input);
            isNew ? ++newTracks : ++report.tracksSkipped;
        }
        auto created = m_library.createPlaylist(draft, job);
        if (!created.ok())
            return created.error();
        ++report.playlistsAdded;
        report.tracksAdded += newTracks;
    }

    // the artists of the file: a picture and tags only for those that have none yet
    for (const QJsonValue& av : manifest.value(QStringLiteral("artists")).toArray()) {
        const QJsonObject o = av.toObject();
        const QString artistName = o.value(QStringLiteral("name")).toString();
        SEMPLICE_ASSIGN(artist, artists.findByName(artistName));
        if (!artist)
            continue;
        ArtistPatch patch;
        if (!knownBefore.contains(artistName)) {   // a new artist: the status chosen for it (the file suggests one)
            const bool hide = selection.artistHidden.value(artistName, o.value(QStringLiteral("hidden")).toBool());
            if (hide)
                patch.userHidden = true;
        }
        const QString avatarBlob = o.value(QStringLiteral("avatar")).toString();
        if (!avatarBlob.isEmpty() && artist->avatarPath.isEmpty() && !artist->hidden) {
            SEMPLICE_ASSIGN(file, unpack(avatarBlob, QStringLiteral("jpg")));
            if (!file.isEmpty())
                patch.avatarPath = file;
        }
        const QStringList wanted = toList(o.value(QStringLiteral("tags")));
        TagRepo tagRepo(m_db);
        SEMPLICE_ASSIGN(have, tagRepo.tags(TagOwner::Artist, artist->id.value));
        if (have.isEmpty() && !wanted.isEmpty())
            patch.tags = wanted;
        if (patch.avatarPath || patch.tags || patch.userHidden) {
            if (auto r = m_library.updateArtist(artist->id, patch, job); r.ok())
                ++report.artistsUpdated;
        }
    }
    job.report(QStringLiteral("import"), steps, steps);
    return report;
}

}  // namespace semplice
