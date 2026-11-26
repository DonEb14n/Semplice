#include "SyntheticLibrary.h"

#include "core/domain/Types.h"
#include "core/util/Normalizer.h"

#include <QCryptographicHash>
#include <QSqlError>

#include <algorithm>
#include <random>

namespace semplice::testing {

namespace {

// Word lists mix Latin, accented Latin, Cyrillic and CJK so search and sorting see real-world text.
const QStringList kAdjectives = {
    QStringLiteral("Silver"),  QStringLiteral("Midnight"), QStringLiteral("Electric"), QStringLiteral("Golden"),
    QStringLiteral("Broken"),  QStringLiteral("Quiet"),    QStringLiteral("Wild"),     QStringLiteral("Frozen"),
    QStringLiteral("Café"),    QStringLiteral("Señor"),    QStringLiteral("Björk's"),  QStringLiteral("Northern"),
    QStringLiteral("Дикий"),   QStringLiteral("Тихий"),    QStringLiteral("Зимний"),   QStringLiteral("星の"),
    QStringLiteral("静かな"),  QStringLiteral("夜の"),     QStringLiteral("Lost"),     QStringLiteral("Neon")};
const QStringList kNouns = {
    QStringLiteral("Fox"),     QStringLiteral("River"),   QStringLiteral("Machine"), QStringLiteral("Garden"),
    QStringLiteral("Echo"),    QStringLiteral("Parade"),  QStringLiteral("Orchestra"), QStringLiteral("Highway"),
    QStringLiteral("Ocean"),   QStringLiteral("Lantern"), QStringLiteral("Дорога"),  QStringLiteral("Ветер"),
    QStringLiteral("海"),      QStringLiteral("空"),      QStringLiteral("Zebra"),   QStringLiteral("Mirror"),
    QStringLiteral("Thunder"), QStringLiteral("Velvet"),  QStringLiteral("Cathedral"), QStringLiteral("Signal")};
const QStringList kGenres = {QStringLiteral("rock"),  QStringLiteral("pop"),     QStringLiteral("jazz"),
                             QStringLiteral("metal"), QStringLiteral("hip-hop"), QStringLiteral("classical"),
                             QStringLiteral("folk"),  QStringLiteral("electronic"), QStringLiteral("blues"),
                             QStringLiteral("punk"),  QStringLiteral("ambient"),  QStringLiteral("soul")};

class Rng {
public:
    explicit Rng(quint32 seed) : m_gen(seed) {}
    int below(int n) { return n <= 1 ? 0 : static_cast<int>(m_gen() % static_cast<quint32>(n)); }
    bool chance(double p) { return (m_gen() % 100000) < static_cast<quint32>(p * 100000.0); }

private:
    std::mt19937 m_gen;
};

QString phrase(Rng& rng, int serial)
{
    return QStringLiteral("%1 %2 %3")
        .arg(kAdjectives[rng.below(static_cast<int>(kAdjectives.size()))],
             kNouns[rng.below(static_cast<int>(kNouns.size()))])
        .arg(serial);  // the serial keeps every name unique
}

QString dirOf(const QString& name)
{
    QString out = name;
    for (QChar& c : out) {
        if (QStringLiteral("<>:\"/\\|?*").contains(c))
            c = QLatin1Char('_');
    }
    return out;
}

QByteArray hashOf(qint64 serial)
{
    return QCryptographicHash::hash(QByteArray::number(serial), QCryptographicHash::Sha256);
}

struct Prepared {
    QSqlQuery q;
    Prepared(QSqlDatabase db, const QString& sql) : q(db) { ok = q.prepare(sql); }
    bool ok = false;
    bool run(const QVariantList& args)
    {
        for (int i = 0; i < args.size(); ++i)
            q.bindValue(i, args[i]);
        return q.exec();
    }
};

}  // namespace

Result<SyntheticStats> generateSyntheticLibrary(Database& db, const SyntheticSpec& spec)
{
    Rng rng(spec.seed);
    SyntheticStats stats;
    QSqlDatabase qdb = db.qt();

    auto fail = [&](const QString& what, const QSqlError& e) {
        return Result<SyntheticStats>(Error::make(ErrorCode::DatabaseError, what + QStringLiteral(": ") + e.text()));
    };

    Transaction tx(db);
    if (auto r = tx.begin(); !r.ok())
        return r.error();

    Prepared insArtist(qdb, QStringLiteral("INSERT INTO artist(id,name,name_key,dir_name,hidden,tags_seeded,created_at,"
                                           "updated_at) VALUES(?,?,?,?,?,?,?,?)"));
    Prepared insAlbum(qdb, QStringLiteral("INSERT INTO album(id,primary_artist_id,title,title_key,dir_name,type,"
                                          "is_unsorted,year,month,day,created_at,updated_at) VALUES(?,?,?,?,?,?,0,?,?,?,?,?)"));
    Prepared insCo(qdb, QStringLiteral("INSERT INTO album_artist(album_id,artist_id,position) VALUES(?,?,?)"));
    Prepared insAlbumTag(qdb, QStringLiteral("INSERT OR IGNORE INTO album_tag(album_id,tag_key) VALUES(?,?)"));
    Prepared insArtistTag(qdb, QStringLiteral("INSERT OR IGNORE INTO artist_tag(artist_id,tag_key) VALUES(?,?)"));
    Prepared setSeeded(qdb, QStringLiteral("UPDATE artist SET tags_seeded = 1 WHERE id = ?"));
    Prepared insTrack(qdb, QStringLiteral("INSERT INTO track(id,title,duration_ms,file_path,file_size,file_mtime,codec,"
                                          "content_hash,home_kind,home_id,created_at,updated_at) "
                                          "VALUES(?,?,?,?,?,?,'mp3',?,?,?,?,?)"));
    Prepared insCredit(qdb, QStringLiteral("INSERT OR IGNORE INTO track_artist(track_id,artist_id,position,role) "
                                           "VALUES(?,?,?,?)"));
    Prepared insAlbumTrack(qdb, QStringLiteral("INSERT INTO album_track(album_id,track_id,position,added_at) "
                                               "VALUES(?,?,?,?)"));
    Prepared insPlaylist(qdb, QStringLiteral("INSERT INTO playlist(id,name,name_key,dir_name,created_at,updated_at) "
                                             "VALUES(?,?,?,?,?,?)"));
    Prepared insPlaylistTrack(qdb, QStringLiteral("INSERT INTO playlist_track(playlist_id,track_id,position,added_at) "
                                                  "VALUES(?,?,?,?)"));
    for (const Prepared* p : {&insArtist, &insAlbum, &insCo, &insAlbumTag, &insArtistTag, &setSeeded, &insTrack,
                              &insCredit, &insAlbumTrack, &insPlaylist, &insPlaylistTrack}) {
        if (!p->ok)
            return fail(QStringLiteral("prepare"), p->q.lastError());
    }

    const qint64 base = 1700000000000;  // fixed epoch so output is repeatable
    qint64 clock = base;

    // ---- planning numbers ----
    const int playlistOnly = std::max(spec.playlists, static_cast<int>(spec.tracks * spec.playlistOnlyTrackFraction));
    const int albumTracks = spec.tracks - playlistOnly;
    const int hiddenCount = std::min(static_cast<int>(spec.artists * spec.hiddenArtistFraction), playlistOnly);
    const int visibleCount = spec.artists - hiddenCount;
    if (visibleCount < 1 || albumTracks < spec.albums || spec.playlists < 1)
        return Error::make(ErrorCode::InvalidArgument, QStringLiteral("spec too small"));

    // ---- artists ----
    QList<QString> artistDir(spec.artists + 1);
    for (int i = 1; i <= spec.artists; ++i) {
        const bool hidden = i > visibleCount;
        const QString name = phrase(rng, i);
        artistDir[i] = hidden ? QString() : dirOf(name);
        if (!insArtist.run({i, name, Normalizer::identityKey(name), hidden ? QVariant() : QVariant(artistDir[i]),
                            hidden ? 1 : 0, 0, ++clock, clock}))
            return fail(QStringLiteral("artist"), insArtist.q.lastError());
        ++stats.artists;
        stats.hiddenArtists += hidden ? 1 : 0;
    }

    // ---- albums and their tracks ----
    const int baseTracks = albumTracks / spec.albums;
    int extraTracks = albumTracks % spec.albums;
    qint64 trackId = 0;
    QList<qint64> albumTrackIds;  // for playlists to borrow from
    albumTrackIds.reserve(albumTracks);
    QList<bool> seeded(spec.artists + 1, false);

    for (int a = 1; a <= spec.albums; ++a) {
        const int primary = 1 + rng.below(visibleCount);
        const QString title = phrase(rng, a);
        const QString dir = dirOf(title);
        static const char* types[] = {"album", "album", "album", "album", "album", "album", "single", "ep",
                                      "ep",    "live",  "compilation", "other"};
        const QString type = QString::fromLatin1(types[rng.below(12)]);
        const int year = 1960 + rng.below(67);
        const int month = rng.chance(0.5) ? 1 + rng.below(12) : 0;
        const int day = month != 0 && rng.chance(0.5) ? 1 + rng.below(28) : 0;
        if (!insAlbum.run({a, primary, title, Normalizer::identityKey(title), dir, type, year,
                           month == 0 ? QVariant() : QVariant(month), day == 0 ? QVariant() : QVariant(day), ++clock,
                           clock}))
            return fail(QStringLiteral("album"), insAlbum.q.lastError());
        ++stats.albums;

        QList<int> authors{primary};
        if (rng.chance(0.1)) {
            const int count = 1 + rng.below(2);
            for (int c = 0; c < count; ++c) {
                const int co = 1 + rng.below(visibleCount);
                if (authors.contains(co))
                    continue;
                authors << co;
                if (!insCo.run({a, co, static_cast<int>(authors.size()) - 1}))
                    return fail(QStringLiteral("co-author"), insCo.q.lastError());
            }
        }
        QStringList tags;
        for (int t = rng.below(4); t > 0; --t) {
            const QString tag = kGenres[rng.below(static_cast<int>(kGenres.size()))];
            if (tags.contains(tag))
                continue;
            tags << tag;
            if (!insAlbumTag.run({a, tag}))
                return fail(QStringLiteral("album tag"), insAlbumTag.q.lastError());
        }
        if (!tags.isEmpty()) {
            for (int author : std::as_const(authors)) {
                if (seeded[author])
                    continue;
                seeded[author] = true;
                for (const QString& tag : std::as_const(tags)) {
                    if (!insArtistTag.run({author, tag}))
                        return fail(QStringLiteral("artist tag"), insArtistTag.q.lastError());
                }
                if (!setSeeded.run({author}))
                    return fail(QStringLiteral("seeded"), setSeeded.q.lastError());
            }
        }

        const int count = baseTracks + (extraTracks > 0 ? 1 : 0);
        if (extraTracks > 0)
            --extraTracks;
        for (int pos = 1; pos <= count; ++pos) {
            ++trackId;
            const QString trackTitle = phrase(rng, static_cast<int>(trackId));
            const QString path = QStringLiteral("Artists/%1/%2/%3 - %4.mp3")
                                     .arg(artistDir[primary], dir)
                                     .arg(pos, 2, 10, QLatin1Char('0'))
                                     .arg(dirOf(trackTitle));
            if (!insTrack.run({trackId, trackTitle, 120000 + rng.below(300000), path, 4000000 + rng.below(8000000),
                               base, hashOf(trackId), QStringLiteral("album"), a, ++clock, clock}))
                return fail(QStringLiteral("track"), insTrack.q.lastError());
            if (!insCredit.run({trackId, primary, 0, QStringLiteral("main")}))
                return fail(QStringLiteral("credit"), insCredit.q.lastError());
            if (rng.chance(0.1)) {  // a featured artist (visible or hidden)
                const int feat = 1 + rng.below(spec.artists);
                if (!insCredit.run({trackId, feat, 1, QStringLiteral("feat")}))
                    return fail(QStringLiteral("feat credit"), insCredit.q.lastError());
            }
            if (!insAlbumTrack.run({a, trackId, pos, ++clock}))
                return fail(QStringLiteral("album track"), insAlbumTrack.q.lastError());
            albumTrackIds << trackId;
            ++stats.tracks;
        }
    }

    // ---- playlists: their own tracks first, then borrowed album tracks ----
    QList<int> playlistPosition(spec.playlists + 1, 0);
    QList<QString> playlistDir(spec.playlists + 1);
    for (int p = 1; p <= spec.playlists; ++p) {
        const QString name = phrase(rng, p);
        playlistDir[p] = dirOf(name);
        if (!insPlaylist.run({p, name, Normalizer::identityKey(name), playlistDir[p], ++clock, clock}))
            return fail(QStringLiteral("playlist"), insPlaylist.q.lastError());
        ++stats.playlists;
    }
    for (int n = 0; n < playlistOnly; ++n) {
        ++trackId;
        const int p = 1 + n % spec.playlists;
        // hidden artists first so every one of them is referenced; then random visible artists
        const int owner = n < hiddenCount ? visibleCount + 1 + n : 1 + rng.below(visibleCount);
        const QString trackTitle = phrase(rng, static_cast<int>(trackId));
        const QString path = QStringLiteral("Playlists/%1/%2 - Artist %3.mp3").arg(playlistDir[p], dirOf(trackTitle)).arg(owner);
        if (!insTrack.run({trackId, trackTitle, 120000 + rng.below(300000), path, 4000000, base, hashOf(trackId),
                           QStringLiteral("playlist"), p, ++clock, clock}))
            return fail(QStringLiteral("playlist track"), insTrack.q.lastError());
        if (!insCredit.run({trackId, owner, 0, QStringLiteral("main")}))
            return fail(QStringLiteral("playlist credit"), insCredit.q.lastError());
        if (!insPlaylistTrack.run({p, trackId, ++playlistPosition[p], ++clock}))
            return fail(QStringLiteral("playlist membership"), insPlaylistTrack.q.lastError());
        ++stats.tracks;
    }
    for (int p = 1; p <= spec.playlists; ++p) {
        QSet<qint64> used;
        for (int n = 0; n < spec.playlistExtraTracks && !albumTrackIds.isEmpty(); ++n) {
            const qint64 t = albumTrackIds[rng.below(static_cast<int>(albumTrackIds.size()))];
            if (used.contains(t))
                continue;
            used.insert(t);
            if (!insPlaylistTrack.run({p, t, ++playlistPosition[p], ++clock}))
                return fail(QStringLiteral("borrowed track"), insPlaylistTrack.q.lastError());
        }
    }

    if (auto r = tx.commit(); !r.ok())
        return r.error();
    return stats;
}

}  // namespace semplice::testing
