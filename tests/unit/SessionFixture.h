#pragma once

#include "FakeCodepageConverter.h"
#include "FakeTrash.h"
#include "IntegrityChecker.h"
#include "TestAudioFiles.h"
#include "TestLibrary.h"

#include "core/services/LibrarySession.h"
#include "core/storage/OwnFiles.h"

#include <QDirIterator>
#include <QTemporaryDir>
#include <QtTest>

namespace semplice::testing {

// A refusing/accepting stand-in for the audio backend: "the player holds this file open".
class SwitchableReleaser final : public IFileReleaser {
public:
    Result<void> releaseFile(const QString& path) override
    {
        released << path;
        if (refuse)
            return Error::make(ErrorCode::FileLocked, QStringLiteral("player holds the file"));
        return {};
    }
    void fileReady(const QString& path) override { ready << path; }
    bool refuse = false;
    QStringList released, ready;  // every path the storage asked us to let go of, and the ones given back
};

// Crashes the operation at the n-th checkpoint (n counted from 1); 0 = never.
class CrashAt final : public FaultInjector {
public:
    explicit CrashAt(int target = 0) : m_target(target) {}
    bool crashHere(const QString& checkpoint) override
    {
        ++m_seen;
        if (m_target > 0 && m_seen == m_target) {
            m_where = checkpoint;
            return true;
        }
        return false;
    }
    int seen() const { return m_seen; }
    QString where() const { return m_where; }

private:
    int m_target;
    int m_seen = 0;
    QString m_where;
};

// A real library folder in a temp dir with a real session on it, plus a "music folder" for sources.
struct SessionFixture {
    SessionFixture()
        : trash(dirPath(QStringLiteral("trash"))), deps{trash, replace, codepages, clock, attributes, &releaser, nullptr}
    {
        QVERIFY(dir.isValid());
        QDir().mkpath(libRoot());
        QDir().mkpath(srcDir());
    }

    QString dirPath(const QString& name) const { return QDir(root.path()).filePath(name); }
    QString libRoot() const { return dirPath(QStringLiteral("lib")); }
    QString srcDir() const { return dirPath(QStringLiteral("music")); }
    QString abs(const QString& relative) const { return QDir(libRoot()).filePath(relative); }

    // ---- session handling
    bool create()
    {
        auto r = LibrarySession::create(libRoot(), deps);
        QVERIFY2_RETURN(r.ok(), r.ok() ? QString() : r.error().symbol() + r.error().devMessage, false);
        session = std::move(r).value();
        return true;
    }
    bool open(FaultInjector* injector = nullptr)
    {
        session.reset();
        SessionDependencies d = deps;
        d.injector = injector;
        auto r = LibrarySession::open(libRoot(), d);
        QVERIFY2_RETURN(r.ok(), r.ok() ? QString() : r.error().symbol() + r.error().devMessage, false);
        session = std::move(r).value();
        return true;
    }

    // ---- sources
    // Writes a tiny audio file below srcDir()/subfolder with tags. `seed` makes the content unique.
    QString source(const QString& subfolder, const QString& baseName, int seed, const QString& title,
                   const QStringList& artists, const QString& album = {}, int trackNumber = 0,
                   const QStringList& genres = {}, AudioFormat format = AudioFormat::Mp3)
    {
        const QString folder = QDir(srcDir()).filePath(subfolder);
        const QString path = writeTinyAudio(folder, baseName, format, seed);
        QVERIFY2_RETURN(!path.isEmpty(), "cannot write source", QString());
        TagWrite w;
        w.title = title;
        w.artists = artists;
        if (!album.isEmpty()) {
            w.album = album;
            w.albumArtists = QStringList{artists.value(0)};
            w.date = ReleaseDate{2021, 3, 4};
        }
        if (trackNumber > 0)
            w.trackNumber = trackNumber;
        w.genres = genres;
        TagService tagger(replace);
        auto r = tagger.write(path, w);
        QVERIFY2_RETURN(r.ok(), r.ok() ? QString() : r.error().devMessage, QString());
        return path;
    }

    // N album tracks by one artist: "<album> 1".."<album> N".
    QString albumFolder(const QString& subfolder, const QString& artist, const QString& album, int count,
                        int seedBase, const QStringList& genres = {})
    {
        for (int i = 1; i <= count; ++i) {
            const QString p = source(subfolder, QStringLiteral("t%1").arg(i, 2, 10, QLatin1Char('0')), seedBase + i,
                                     QStringLiteral("%1 %2").arg(album).arg(i), {artist}, album, i, genres);
            QVERIFY2_RETURN(!p.isEmpty(), "source failed", QString());
        }
        return QDir(srcDir()).filePath(subfolder);
    }

    // ---- observation
    static QSet<QString> audioFilesBelow(const QString& base, const QString& root)
    {
        QSet<QString> out;
        if (!QFileInfo(base).isDir())
            return out;
        QDirIterator it(base, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext())
            out.insert(QDir(root).relativeFilePath(it.next()));
        return out;
    }
    QSet<QString> filesOnDisk() const
    {
        return audioFilesBelow(abs(QStringLiteral("Artists")), libRoot()) + audioFilesBelow(abs(QStringLiteral("Playlists")), libRoot());
    }
    QSet<QString> filesInDatabase()
    {
        QSet<QString> out;
        auto rows = session->database().run(QStringLiteral("SELECT file_path FROM track UNION SELECT cover_path FROM album WHERE cover_path IS NOT NULL UNION SELECT cover_path FROM playlist WHERE cover_path IS NOT NULL UNION SELECT avatar_path FROM artist WHERE avatar_path IS NOT NULL"));
        if (!rows.ok())
            return out;
        QSqlQuery q = std::move(rows).value();
        while (q.next())
            out.insert(q.value(0).toString());
        return out;
    }
    QList<TrackId> allTracks()
    {
        QList<TrackId> out;
        auto rows = session->database().run(QStringLiteral("SELECT id FROM track ORDER BY id"));
        if (!rows.ok())
            return out;
        QSqlQuery q = std::move(rows).value();
        while (q.next())
            out.append(TrackId{q.value(0).toLongLong()});
        return out;
    }
    int count(const QString& table)
    {
        auto rows = session->database().run(QStringLiteral("SELECT COUNT(*) FROM ") + table);
        if (!rows.ok())
            return -1;
        QSqlQuery q = std::move(rows).value();
        return q.next() ? q.value(0).toInt() : -1;
    }

    // The invariant after EVERY operation: files == rows, no journal left over, database consistent, and
    // the tags inside each file say what the database says (for every track that is not marked dirty).
    // Returns an empty string when everything holds, otherwise a description.
    QString verify(bool checkTags = true)
    {
        QStringList problems;
        const QString integrity = checkLibraryIntegrity(session->database()).join(QLatin1Char('\n'));
        if (!integrity.isEmpty())
            problems << QStringLiteral("integrity: ") + integrity;
        const QSet<QString> disk = filesOnDisk();
        const QSet<QString> db = filesInDatabase();
        for (const QString& f : disk - db)
            problems << QStringLiteral("file not in database: ") + f;
        for (const QString& f : db - disk)
            problems << QStringLiteral("row without file: ") + f;
        if (!session->storage().pendingOperationIds().isEmpty())
            problems << QStringLiteral("journal not empty");
        if (checkTags) {
            for (TrackId id : allTracks()) {
                const Track t = trackRow(id);
                if (t.tagsDirty)
                    continue;
                auto want = session->library().buildTagWrite(id);
                auto got = session->tags().read(abs(t.filePath));
                if (!want.ok() || !got.ok()) {
                    problems << QStringLiteral("cannot compare tags of ") + t.filePath;
                    continue;
                }
                const TagWrite& w = want.value();
                const TrackTags& g = got.value();
                if (g.title != w.title)
                    problems << QStringLiteral("title of %1: file '%2' db '%3'").arg(t.filePath, g.title, w.title);
                if (g.artists != w.artists)
                    problems << QStringLiteral("artists of %1: file '%2' db '%3'").arg(t.filePath, g.artists.join(QLatin1Char('|')), w.artists.join(QLatin1Char('|')));
                if (w.album && g.album != *w.album)
                    problems << QStringLiteral("album of %1: file '%2' db '%3'").arg(t.filePath, g.album, *w.album);
                if (w.trackNumber && g.trackNumber != *w.trackNumber)
                    problems << QStringLiteral("track number of %1: file %2 db %3").arg(t.filePath).arg(g.trackNumber).arg(*w.trackNumber);
            }
        }
        return problems.join(QLatin1Char('\n'));
    }

    Track trackRow(TrackId id)
    {
        TrackRepo repo(session->database());
        auto r = repo.get(id);
        return r.ok() && r.value() ? *r.value() : Track{};
    }
    std::optional<Album> albumRow(AlbumId id)
    {
        AlbumRepo repo(session->database());
        auto r = repo.get(id);
        return r.ok() ? r.value() : std::nullopt;
    }
    std::optional<Artist> artistNamed(const QString& name)
    {
        ArtistRepo repo(session->database());
        auto r = repo.findByName(name);
        return r.ok() ? r.value() : std::nullopt;
    }

    static QByteArray readAll(const QString& path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

    QTemporaryDir dir;  // (declared first: everything else lives below it)
    QTemporaryDir& root = dir;
    FakeTrash trash;
    PortableAtomicReplace replace;
    FakeCodepageConverter codepages;
    FakeClock clock;
    NullFileAttributes attributes;
    SwitchableReleaser releaser;
    SessionDependencies deps;
    std::unique_ptr<LibrarySession> session;
};

}  // namespace semplice::testing
