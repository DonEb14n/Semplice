#pragma once

#include "IntegrityChecker.h"

#include "core/db/Database.h"
#include "core/db/Migrator.h"
#include "core/services/LibraryService.h"

#include <QCryptographicHash>
#include <QtTest>
// QVERIFY2 variant for helper functions that return a value.
#define QVERIFY2_RETURN(cond, msg, retval)                                                              \
    do {                                                                                                \
        if (!QTest::qVerify(static_cast<bool>(cond), #cond, qPrintable(QString(msg)), __FILE__, __LINE__)) \
            return retval;                                                                              \
    } while (false)


namespace semplice::testing {

class FakeClock final : public IClock {
public:
    QDateTime now() const override { return m_now; }
    void advance(qint64 ms) { m_now = m_now.addMSecs(ms); }

private:
    QDateTime m_now = QDateTime(QDate(2026, 10, 19), QTime(12, 0), QTimeZone::UTC);
};

// A migrated in-memory database plus a LibraryService wired to recording fakes.
struct TestLibrary {
    // `externalStorage`: run the service against a real FileStorageService instead of the recording fake.
    explicit TestLibrary(IFileStorage* externalStorage = nullptr)
    {
        auto opened = Database::open(QStringLiteral(":memory:"));
        QVERIFY2(opened.ok(), qPrintable(opened.ok() ? QString() : opened.error().devMessage));
        db = std::move(opened).value();
        auto migrated = Migrator::migrate(*db);
        QVERIFY2(migrated.ok(), qPrintable(migrated.ok() ? QString() : migrated.error().devMessage));
        service = std::make_unique<LibraryService>(*db, externalStorage ? *externalStorage : static_cast<IFileStorage&>(storage), namer, clock,
                                                   TagVocabulary({QStringLiteral("rock"), QStringLiteral("pop"),
                                                                  QStringLiteral("jazz"), QStringLiteral("hip-hop")}));
    }

    // ---- helpers that keep the tests short; each fails the test on error
    ArtistId artist(const QString& name, bool hidden = false, const QStringList& tags = {})
    {
        auto r = service->createArtist({name, hidden, {}, tags});
        QVERIFY2_RETURN(r.ok(), r.ok() ? QString() : r.error().symbol(), ArtistId());
        return r.value();
    }

    TrackDraft draft(const QString& title, const QList<CreditDraft>& credits = {})
    {
        TrackDraft d;
        d.title = title;
        d.durationMs = 180000;
        d.extension = QStringLiteral("mp3");
        d.contentHash = QCryptographicHash::hash(QByteArray::number(++m_hashCounter), QCryptographicHash::Sha256);
        d.fileSize = 1000;
        d.credits = credits;
        return d;
    }

    // Album with N generated tracks (credited to the primary artist by default).
    AlbumId album(ArtistId primary, const QString& title, int tracks, const QList<ArtistId>& coAuthors = {},
                  const QStringList& tags = {}, ReleaseDate date = {2020, 5, 1})
    {
        AlbumDraft d;
        d.primaryArtist = primary;
        d.coAuthors = coAuthors;
        d.title = title;
        d.date = date;
        d.tags = tags;
        for (int i = 1; i <= tracks; ++i) {
            TrackInput in;
            in.draft = draft(QStringLiteral("%1 track %2").arg(title).arg(i));
            d.tracks.append(in);
        }
        auto r = service->createAlbum(d);
        QVERIFY2_RETURN(r.ok(), r.ok() ? QString() : r.error().symbol() + r.error().devMessage, AlbumId());
        return r.value();
    }

    PlaylistId playlist(const QString& name, const QList<TrackInput>& tracks)
    {
        auto r = service->createPlaylist({name, {}, tracks});
        QVERIFY2_RETURN(r.ok(), r.ok() ? QString() : r.error().symbol() + r.error().devMessage, PlaylistId());
        return r.value();
    }

    // A playlist with `count` brand-new tracks credited to `artist`.
    PlaylistId playlistOfNew(const QString& name, ArtistId artistId, int count)
    {
        QList<TrackInput> in;
        for (int i = 1; i <= count; ++i) {
            TrackInput t;
            t.draft = draft(QStringLiteral("%1 song %2").arg(name).arg(i), {{artistId, CreditRole::Main}});
            in.append(t);
        }
        return playlist(name, in);
    }

    TrackId trackAt(AlbumId a, int position)
    {
        AlbumRepo repo(*db);
        auto list = repo.tracks(a);
        QVERIFY2_RETURN(list.ok() && position >= 1 && position <= list.value().size(), "no such track", TrackId());
        return list.value().at(position - 1);
    }
    TrackId trackAt(PlaylistId p, int position)
    {
        PlaylistRepo repo(*db);
        auto list = repo.tracks(p);
        QVERIFY2_RETURN(list.ok() && position >= 1 && position <= list.value().size(), "no such track", TrackId());
        return list.value().at(position - 1);
    }

    Track track(TrackId id)
    {
        TrackRepo repo(*db);
        auto r = repo.get(id);
        return r.ok() && r.value() ? *r.value() : Track{};
    }
    Artist artistRow(ArtistId id)
    {
        ArtistRepo repo(*db);
        auto r = repo.get(id);
        return r.ok() && r.value() ? *r.value() : Artist{};
    }
    std::optional<Album> albumRow(AlbumId id)
    {
        AlbumRepo repo(*db);
        auto r = repo.get(id);
        return r.ok() ? r.value() : std::nullopt;
    }
    bool artistExists(ArtistId id)
    {
        ArtistRepo repo(*db);
        auto r = repo.get(id);
        return r.ok() && r.value().has_value();
    }
    bool albumExists(AlbumId id) { return albumRow(id).has_value(); }
    bool playlistExists(PlaylistId id)
    {
        PlaylistRepo repo(*db);
        auto r = repo.get(id);
        return r.ok() && r.value().has_value();
    }
    bool trackExists(TrackId id)
    {
        TrackRepo repo(*db);
        auto r = repo.get(id);
        return r.ok() && r.value().has_value();
    }

    QString integrity() { return checkLibraryIntegrity(*db).join(QLatin1Char('\n')); }

    FakeClock clock;
    RecordingFileStorage storage;
    SanitizingPathNamer namer;
    std::unique_ptr<Database> db;
    std::unique_ptr<LibraryService> service;

private:
    int m_hashCounter = 0;
};


}  // namespace semplice::testing
